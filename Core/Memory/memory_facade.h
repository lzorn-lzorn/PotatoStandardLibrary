
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <memory_resource>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>

#include "thread_cache.h"

namespace core::mem
{

class MemoryAllocatorEngine
{
public:
	explicit MemoryAllocatorEngine(const AllocatorConfig& InConfig)
		: Config(InConfig)
		, PageAllocator(Config)
		, CentralPool(PageAllocator)
	{
		ThreadCacheRegistry::bind(CentralPool, Config);
	}

	MemoryAllocatorEngine(const MemoryAllocatorEngine&) = delete;
	MemoryAllocatorEngine& operator=(const MemoryAllocatorEngine&) = delete;

	[[nodiscard]] static MemoryAllocatorEngine& Self()
	{
		static MemoryAllocatorEngine* engine = new MemoryAllocatorEngine(getGlobalConfig());
		SelfConstructed().store(true, std::memory_order_release);
		return *engine;
	}

	static void configureBeforeFirstUse(const AllocatorConfig& InConfig)
	{
		if (SelfConstructed().load(std::memory_order_acquire))
		{
			return;
		}

		getGlobalConfig() = InConfig;
	}

	[[nodiscard]] void* allocate(const AllocationDescriptor& Descriptor)
	{
		drainQuarantineToBudget();

		AllocationContext Context;
		Context.Descriptor = Descriptor;
		Context.Runtime.Owner = this;

		prepareContext(Context);
		if (!validateRequest(Context))
		{
			emitValidationFailure(Context);
			return nullptr;
		}

		if (!buildLayout(Context))
		{
			emitValidationFailure(Context);
			return nullptr;
		}

		if (!allocateRawBlock(Context))
		{
			return nullptr;
		}

		initializeLayout(Context);
		emitAllocationEvent(Context);
		Context.Stage = AllocationStage::Return;
		Context.Result.IsSuccess = true;
		return Context.Runtime.UserPtr;
	}

	void deallocate(void* Ptr, const AllocationDescriptor& Descriptor) noexcept
	{
		drainQuarantineToBudget();

		if (!Ptr)
		{
			return;
		}

		AllocationContext Context;
		Context.Descriptor = Descriptor;
		Context.Runtime.Owner = this;
		Context.Runtime.UserPtr = static_cast<std::byte*>(Ptr);

		prepareContext(Context);
		if (!validateRequest(Context))
		{
			emitValidationFailure(Context);
			return;
		}

		if (!buildLayout(Context))
		{
			emitValidationFailure(Context);
			return;
		}

		if (releaseDedicatedBlock(Context))
		{
			emitDeallocationEvent(Context);
			return;
		}

		Context.Runtime.RawPtr = Context.Runtime.UserPtr - Context.Layout.UserOffset;
		destroyLayout(Context);
		freeSmallBlock(Context);
		emitDeallocationEvent(Context);
	}

	MemoryEventBus& getEventBus() noexcept
	{
		return EventBus;
	}

	[[nodiscard]] std::uint32_t getCurrentFrame() const noexcept
	{
		return CurrentFrame.load(std::memory_order_relaxed);
	}

	void setCurrentFrame(const std::uint32_t Frame) noexcept
	{
		CurrentFrame.store(Frame, std::memory_order_relaxed);
	}

	void onThreadExit() noexcept
	{
		flushQuarantine();
		ThreadCacheRegistry::onThreadExit();
	}

	[[nodiscard]] std::size_t prewarm(const std::vector<MemoryPrewarmRequest>& Requests)
	{
		std::size_t warmed = 0;

		for (const MemoryPrewarmRequest& Request : Requests)
		{
			if (Request.Size == 0 || Request.Count == 0)
			{
				continue;
			}

			AllocationDescriptor Descriptor;
			Descriptor.Size = Request.Size;
			Descriptor.Alignment = Request.Alignment;
			Descriptor.ResourceId = Request.ResourceId;
			Descriptor.IsZeroMemory = Request.ZeroInitialize;
			Descriptor.Lifetime = Request.Lifetime;

			std::vector<void*> blocks;
			blocks.reserve(Request.Count);

			for (std::uint32_t i = 0; i < Request.Count; ++i)
			{
				void* Ptr = allocate(Descriptor);
				if (!Ptr)
				{
					break;
				}

				blocks.push_back(Ptr);
				++warmed;
			}

			for (void* Ptr : blocks)
			{
				deallocate(Ptr, Descriptor);
			}
		}

		flushQuarantine();
		return warmed;
	}

	void flushDeferredFrees() noexcept
	{
		flushQuarantine();
	}

private:
	struct DedicatedAllocation
	{
		VirtualRegion Region;
		std::size_t UserOffset = 0;
		std::size_t RequestedSize = 0;
		std::size_t RequestedAlignment = 0;
		AllocationLifetime Lifetime = AllocationLifetime::Default;
	};

	struct SmallBlockQuarantineEntry
	{
		std::byte* RawPtr = nullptr;
		std::size_t UserOffset = 0;
		std::size_t UserSize = 0;
		std::size_t BlockSize = 0;
		std::uint16_t SizeClassIndex = InvalidSizeClass;
		std::uint64_t AllocationId = 0;
		std::uint32_t FrameIndex = 0;
		std::uint32_t ResourceId = 0;
		AllocationLifetime Lifetime = AllocationLifetime::Default;
	};

	static AllocatorConfig& getGlobalConfig()
	{
		static AllocatorConfig Config;
		return Config;
	}

	static std::atomic<bool>& SelfConstructed()
	{
		static std::atomic<bool> Value = false;
		return Value;
	}

	void prepareContext(AllocationContext& Context)
	{
		Context.Stage = AllocationStage::Prepare;
		Context.Metadata.ThreadId = std::this_thread::get_id();
		Context.Metadata.Timestamp = timestampNs();
		Context.Metadata.AllocationId = NextAllocationId.fetch_add(1, std::memory_order_relaxed);
		Context.Metadata.ResourceId = Context.Descriptor.ResourceId;
		Context.Metadata.FrameIndex = Context.Descriptor.FrameIndex != 0
			? Context.Descriptor.FrameIndex
			: CurrentFrame.load(std::memory_order_relaxed);
		Context.Metadata.StackId = captureStackId();
	}

	[[nodiscard]] bool validateRequest(AllocationContext& Context)
	{
		Context.Stage = AllocationStage::Validate;

		if (Context.Descriptor.Size == 0)
		{
			Context.Result.ErrorCode = std::make_error_code(std::errc::invalid_argument);
			return false;
		}

		if (!isPowerOfTwo(Context.Descriptor.Alignment))
		{
			Context.Result.ErrorCode = std::make_error_code(std::errc::invalid_argument);
			return false;
		}

		if (Context.Descriptor.Alignment < alignof(void*))
		{
			Context.Descriptor.Alignment = alignof(void*);
		}

		return true;
	}

	[[nodiscard]] bool buildLayout(AllocationContext& Context)
	{
		Context.Stage = AllocationStage::Layout;
		Context.Layout = {};
		Context.Layout.RequestedSize = Context.Descriptor.Size;
		Context.Layout.RequestedAlignment = Context.Descriptor.Alignment;

		if (Config.EnableDebugGuards)
		{
			Context.Layout.HeaderSize = sizeof(DebugHeader);
			Context.Layout.FrontGuard = 16;
			Context.Layout.BackGuard = 16;
		}

		const std::size_t prefix = Context.Layout.HeaderSize + Context.Layout.FrontGuard;
		Context.Layout.UserOffset = alignUp(prefix, Context.Descriptor.Alignment);

		std::size_t Total = 0;
		if (!checkedAdd(Context.Layout.UserOffset, Context.Descriptor.Size, Total))
		{
			Context.Result.ErrorCode = std::make_error_code(std::errc::value_too_large);
			return false;
		}

		if (!checkedAdd(Total, Context.Layout.BackGuard, Total))
		{
			Context.Result.ErrorCode = std::make_error_code(std::errc::value_too_large);
			return false;
		}

		Context.Layout.TotalSize = Total;

		const bool small_alignment = Context.Descriptor.Alignment <= alignof(std::max_align_t);
		if (small_alignment)
		{
			const std::uint16_t classIndex = sizeToClassIndex(Total);
			if (classIndex != InvalidSizeClass)
			{
				const SizeClass classInfo = CentralPool.getSizeClassInfo(classIndex);
				Context.Layout.SizeClassIndex = classIndex;
				Context.Layout.BlockSize = classInfo.BlockSize;
				Context.Layout.TotalSize = classInfo.BlockSize;
				Context.Layout.IsSmallAllocation = true;
				return true;
			}
		}

		Context.Layout.IsSmallAllocation = false;
		const std::size_t pageSize = PageAllocator.getPageSize();
		std::size_t ReserveBytes = 0;
		if (!checkedAdd(Context.Layout.TotalSize, Context.Descriptor.Alignment, ReserveBytes))
		{
			Context.Result.ErrorCode = std::make_error_code(std::errc::value_too_large);
			return false;
		}

		Context.Layout.TotalSize = alignUp(ReserveBytes, pageSize);
		return true;
	}

	[[nodiscard]] bool allocateRawBlock(AllocationContext& Context)
	{
		Context.Stage = AllocationStage::Allocate;

		if (Context.Layout.IsSmallAllocation)
		{
			if (Context.Descriptor.Lifetime == AllocationLifetime::Persistent)
			{
				void* Block = nullptr;
				const std::uint32_t fetched = CentralPool.fetchBatch(
					Context.Layout.SizeClassIndex,
					&Block,
					1);

				if (fetched == 0 || !Block)
				{
					Context.Result.ErrorCode = std::make_error_code(std::errc::not_enough_memory);
					return false;
				}

				Context.Runtime.RawPtr = static_cast<std::byte*>(Block);
				Context.Runtime.UserPtr = Context.Runtime.RawPtr + Context.Layout.UserOffset;
				Context.Runtime.IsFromThreadCache = false;
				Context.Runtime.IsFromCentralPool = true;
				Context.Result.IsSuccess = true;
				return true;
			}

			ThreadCache& cache = ThreadCacheRegistry::get();
			const ThreadCacheAllocation decision = cache.allocate(
				Context.Layout.SizeClassIndex,
				Context.Layout.BlockSize);

			if (!decision.Block)
			{
				Context.Result.ErrorCode = std::make_error_code(std::errc::not_enough_memory);
				return false;
			}

			Context.Runtime.RawPtr = static_cast<std::byte*>(decision.Block);
			Context.Runtime.UserPtr = Context.Runtime.RawPtr + Context.Layout.UserOffset;
			Context.Runtime.IsFromThreadCache = decision.Hit;
			Context.Runtime.IsFromCentralPool = decision.FromCentral;
			Context.Result.IsSuccess = true;
			return true;
		}

		VirtualRegion Region = acquireDedicatedRegion(
			Context.Layout.TotalSize,
			Context.Descriptor.Lifetime);

		if (!Region.isValid())
		{
			Context.Result.ErrorCode = std::make_error_code(std::errc::not_enough_memory);
			return false;
		}

		if (!VirtualMemoryManager::commit(Region, 0, Region.ReservedSize))
		{
			VirtualMemoryManager::release(Region);
			Context.Result.ErrorCode = std::make_error_code(std::errc::not_enough_memory);
			return false;
		}

		const std::uintptr_t RawAddress = reinterpret_cast<std::uintptr_t>(Region.Base);
		const std::uintptr_t MinUserAddress = RawAddress + Context.Layout.HeaderSize + Context.Layout.FrontGuard;
		const std::uintptr_t UserAddress = alignUp(MinUserAddress, Context.Descriptor.Alignment);
		const std::size_t UserOffset = static_cast<std::size_t>(UserAddress - RawAddress);

		std::size_t RequiredTail = 0;
		if (!checkedAdd(UserOffset, Context.Descriptor.Size, RequiredTail) ||
			!checkedAdd(RequiredTail, Context.Layout.BackGuard, RequiredTail) ||
			RequiredTail > Region.ReservedSize)
		{
			VirtualMemoryManager::release(Region);
			Context.Result.ErrorCode = std::make_error_code(std::errc::value_too_large);
			return false;
		}

		Context.Layout.UserOffset = UserOffset;
		Context.Layout.TotalSize = Region.ReservedSize;
		Context.Runtime.RawPtr = Region.Base;
		Context.Runtime.UserPtr = reinterpret_cast<std::byte*>(UserAddress);
		Context.Runtime.IsFromOS = true;

		DedicatedAllocation Record;
		Record.Region = Region;
		Record.UserOffset = UserOffset;
		Record.RequestedSize = Context.Descriptor.Size;
		Record.RequestedAlignment = Context.Descriptor.Alignment;
		Record.Lifetime = Context.Descriptor.Lifetime;

		{
			std::lock_guard Lock(DedicatedMutex);
			DedicatedAllocations[Context.Runtime.UserPtr] = Record;
		}

		Context.Result.IsSuccess = true;
		return true;
	}

	void initializeLayout(AllocationContext& Context)
	{
		Context.Stage = AllocationStage::Initialize;

		if (Context.Descriptor.IsZeroMemory)
		{
			std::memset(Context.Runtime.UserPtr, 0, Context.Descriptor.Size);
		}
		else
		{
			std::memset(Context.Runtime.UserPtr, AllocatedPattern, Context.Descriptor.Size);
		}

		if (!Config.EnableDebugGuards)
		{
			return;
		}

		auto* Header = reinterpret_cast<DebugHeader*>(Context.Runtime.RawPtr);
		Header->Magic = DebugHeaderMagic;
		Header->Version = 1;
		Header->AllocationId = Context.Metadata.AllocationId;
		Header->RequestedSize = Context.Descriptor.Size;
		Header->RequestedAlignment = static_cast<std::uint32_t>(Context.Descriptor.Alignment);
		Header->SizeClassIndex = Context.Layout.SizeClassIndex;
		Header->FrontGuardBytes = static_cast<std::uint32_t>(Context.Layout.FrontGuard);
		Header->BackGuardBytes = static_cast<std::uint32_t>(Context.Layout.BackGuard);

		if (Context.Layout.FrontGuard > 0)
		{
			std::memset(
				Context.Runtime.RawPtr + Context.Layout.HeaderSize,
				FrontGuardPattern,
				Context.Layout.FrontGuard);
		}

		if (Context.Layout.BackGuard > 0)
		{
			std::memset(
				Context.Runtime.UserPtr + Context.Descriptor.Size,
				BackGuardPattern,
				Context.Layout.BackGuard);
		}
	}

	void destroyLayout(AllocationContext& Context) noexcept
	{
		Context.Stage = AllocationStage::Destroy;
		if (!Config.EnableDebugGuards)
		{
			std::memset(Context.Runtime.UserPtr, FreedPattern, Context.Descriptor.Size);
			return;
		}

		auto* Header = reinterpret_cast<DebugHeader*>(Context.Runtime.RawPtr);
		const bool IsHeaderOk = Header->Magic == DebugHeaderMagic &&
			Header->RequestedSize == Context.Descriptor.Size;

		bool IsGuardsOk = true;
		if (IsHeaderOk)
		{
			const std::byte* front_guard = Context.Runtime.RawPtr + Context.Layout.HeaderSize;
			for (std::size_t i = 0; i < Context.Layout.FrontGuard; ++i)
			{
				if (static_cast<std::uint8_t>(front_guard[i]) != FrontGuardPattern)
				{
					IsGuardsOk = false;
					break;
				}
			}

			if (IsGuardsOk)
			{
				const std::byte* back_guard = Context.Runtime.UserPtr + Context.Descriptor.Size;
				for (std::size_t i = 0; i < Context.Layout.BackGuard; ++i)
				{
					if (static_cast<std::uint8_t>(back_guard[i]) != BackGuardPattern)
					{
						IsGuardsOk = false;
						break;
					}
				}
			}
		}

		if (!IsHeaderOk || !IsGuardsOk)
		{
			emitGuardCorruption(Context);
		}

		Header->Magic = 0;
		std::memset(Context.Runtime.UserPtr, FreedPattern, Context.Descriptor.Size);
	}

	void freeSmallBlock(AllocationContext& Context)
	{
		Context.Stage = AllocationStage::Free;
		if (shouldQuarantineSmallBlock(Context))
		{
			enqueueSmallBlockQuarantine(Context);
			return;
		}

		releaseSmallBlock(
			Context.Layout.SizeClassIndex,
			Context.Layout.BlockSize,
			Context.Runtime.RawPtr,
			Context.Descriptor.Lifetime);
	}

	[[nodiscard]] bool releaseDedicatedBlock(AllocationContext& Context) noexcept
	{
		DedicatedAllocation Record;
		{
			std::lock_guard Lock(DedicatedMutex);
			auto Iter = DedicatedAllocations.find(Context.Runtime.UserPtr);
			if (Iter == DedicatedAllocations.end())
			{
				return false;
			}

			Record = Iter->second;
			DedicatedAllocations.erase(Iter);
		}

		Context.Layout.UserOffset = Record.UserOffset;
		Context.Layout.TotalSize = Record.Region.ReservedSize;
		Context.Descriptor.Size = Record.RequestedSize;
		Context.Descriptor.Alignment = Record.RequestedAlignment;
		Context.Descriptor.Lifetime = Record.Lifetime;
		Context.Runtime.RawPtr = Record.Region.Base;
		Context.Runtime.IsFromOS = true;

		destroyLayout(Context);

		VirtualRegion Region = Record.Region;
		releaseDedicatedRegion(Region, Record.Lifetime);
		return true;
	}

	void releaseSmallBlock(
		const std::uint16_t SizeClassIndex,
		const std::size_t BlockSize,
		std::byte* RawPtr,
		const AllocationLifetime Lifetime)
	{
		if (!RawPtr || SizeClassIndex == InvalidSizeClass)
		{
			return;
		}

		if (Lifetime == AllocationLifetime::Persistent)
		{
			*reinterpret_cast<void**>(RawPtr) = nullptr;
			CentralPool.returnBatch(SizeClassIndex, RawPtr, 1);
			return;
		}

		ThreadCacheRegistry::get().deallocate(SizeClassIndex, BlockSize, RawPtr);
	}

	[[nodiscard]] bool shouldQuarantineSmallBlock(const AllocationContext& Context) const noexcept
	{
		if (!Config.EnableUseAfterFreeDetection)
		{
			return false;
		}

		if (!Config.EnableDebugGuards)
		{
			return false;
		}

		return Context.Layout.IsSmallAllocation && Context.Runtime.RawPtr != nullptr;
	}

	void enqueueSmallBlockQuarantine(const AllocationContext& Context)
	{
		SmallBlockQuarantineEntry Entry;
		Entry.RawPtr = Context.Runtime.RawPtr;
		Entry.UserOffset = Context.Layout.UserOffset;
		Entry.UserSize = Context.Descriptor.Size;
		Entry.BlockSize = Context.Layout.BlockSize;
		Entry.SizeClassIndex = Context.Layout.SizeClassIndex;
		Entry.AllocationId = Context.Metadata.AllocationId;
		Entry.FrameIndex = Context.Metadata.FrameIndex;
		Entry.ResourceId = Context.Metadata.ResourceId;
		Entry.Lifetime = Context.Descriptor.Lifetime;

		std::vector<SmallBlockQuarantineEntry> released;
		{
			std::lock_guard Lock(QuarantineMutex);
			SmallBlockQuarantine.push_back(Entry);
			QuarantineBytes += Entry.BlockSize;
			collectQuarantineReleasesLocked(released, false);
		}

		releaseQuarantineEntries(released);
	}

	void collectQuarantineReleasesLocked(
		std::vector<SmallBlockQuarantineEntry>& Released,
		const bool ForceAll)
	{
		while (!SmallBlockQuarantine.empty())
		{
			if (!ForceAll)
			{
				const bool overBytes = QuarantineBytes > Config.UseAfterFreeQuarantineBytes;
				const bool overEntries =
					SmallBlockQuarantine.size() > Config.UseAfterFreeQuarantineMaxEntries;

				if (!overBytes && !overEntries)
				{
					break;
				}
			}

			SmallBlockQuarantineEntry Entry = SmallBlockQuarantine.front();
			SmallBlockQuarantine.pop_front();
			QuarantineBytes = QuarantineBytes >= Entry.BlockSize
				? QuarantineBytes - Entry.BlockSize
				: 0;
			Released.push_back(Entry);
		}
	}

	void releaseQuarantineEntries(const std::vector<SmallBlockQuarantineEntry>& Entries)
	{
		for (const SmallBlockQuarantineEntry& Entry : Entries)
		{
			if (!isQuarantinePatternIntact(Entry))
			{
				emitUseAfterFree(Entry);
			}

			releaseSmallBlock(
				Entry.SizeClassIndex,
				Entry.BlockSize,
				Entry.RawPtr,
				Entry.Lifetime);
		}
	}

	[[nodiscard]] bool isQuarantinePatternIntact(const SmallBlockQuarantineEntry& Entry) const noexcept
	{
		const std::uint8_t* Bytes = reinterpret_cast<const std::uint8_t*>(Entry.RawPtr + Entry.UserOffset);
		for (std::size_t i = 0; i < Entry.UserSize; ++i)
		{
			if (Bytes[i] != FreedPattern)
			{
				return false;
			}
		}

		return true;
	}

	void drainQuarantineToBudget()
	{
		if (!Config.EnableUseAfterFreeDetection || !Config.EnableDebugGuards)
		{
			return;
		}

		std::vector<SmallBlockQuarantineEntry> released;
		{
			std::lock_guard Lock(QuarantineMutex);
			collectQuarantineReleasesLocked(released, false);
		}

		releaseQuarantineEntries(released);
	}

	void flushQuarantine() noexcept
	{
		std::vector<SmallBlockQuarantineEntry> released;
		{
			std::lock_guard Lock(QuarantineMutex);
			collectQuarantineReleasesLocked(released, true);
		}

		releaseQuarantineEntries(released);
	}

	[[nodiscard]] VirtualRegion acquireDedicatedRegion(
		const std::size_t Bytes,
		const AllocationLifetime Lifetime)
	{
		if (Config.EnableDedicatedRegionCache && Lifetime == AllocationLifetime::Transient)
		{
			std::lock_guard Lock(DedicatedCacheMutex);
			auto Iter = DedicatedRegionCache.find(Bytes);
			if (Iter != DedicatedRegionCache.end() && !Iter->second.empty())
			{
				VirtualRegion Region = Iter->second.back();
				Iter->second.pop_back();
				DedicatedCacheBytes = DedicatedCacheBytes >= Region.ReservedSize
					? DedicatedCacheBytes - Region.ReservedSize
					: 0;
				return Region;
			}
		}

		return VirtualMemoryManager::reserve(
			Bytes,
			Config.EnableHugePage,
			Config.PreferredNumaNode);
	}

	void releaseDedicatedRegion(VirtualRegion& Region, const AllocationLifetime Lifetime) noexcept
	{
		if (!Region.isValid())
		{
			return;
		}

		if (Config.EnableDedicatedRegionCache && Lifetime == AllocationLifetime::Transient)
		{
			std::lock_guard Lock(DedicatedCacheMutex);

			auto& bucket = DedicatedRegionCache[Region.ReservedSize];
			if (bucket.size() < Config.DedicatedRegionCacheMaxEntriesPerSize &&
				DedicatedCacheBytes + Region.ReservedSize <= Config.DedicatedRegionCacheLimitBytes)
			{
				VirtualMemoryManager::decommit(Region, 0, Region.ReservedSize);
				bucket.push_back(Region);
				DedicatedCacheBytes += Region.ReservedSize;
				Region = {};
				return;
			}
		}

		VirtualMemoryManager::release(Region);
	}

	void emitAllocationEvent(AllocationContext& Context)
	{
		Context.Stage = AllocationStage::Notify;
		if (!EventBus.hasObservers())
		{
			return;
		}

		MemoryEvent event;
		event.Type = MemoryEventType::Allocate;
		event.UserPtr = Context.Runtime.UserPtr;
		event.Size = Context.Descriptor.Size;
		event.Alignment = Context.Descriptor.Alignment;
		event.AllocationId = Context.Metadata.AllocationId;
		event.Timestamp = Context.Metadata.Timestamp;
		event.ThreadId = Context.Metadata.ThreadId;
		event.FrameIndex = Context.Metadata.FrameIndex;
		event.ResourceId = Context.Metadata.ResourceId;
		event.FromThreadCache = Context.Runtime.IsFromThreadCache;
		event.FromCentralPool = Context.Runtime.IsFromCentralPool;
		event.FromOS = Context.Runtime.IsFromOS;
		event.Context = &Context;
		EventBus.emit(event);

		if (Context.Runtime.IsFromThreadCache)
		{
			event.Type = MemoryEventType::ThreadCacheHit;
			EventBus.emit(event);
		}
		else
		{
			event.Type = MemoryEventType::ThreadCacheMiss;
			EventBus.emit(event);
		}

		if (Context.Runtime.IsFromCentralPool)
		{
			event.Type = MemoryEventType::CentralFetch;
			EventBus.emit(event);
		}
	}

	void emitDeallocationEvent(AllocationContext& Context)
	{
		Context.Stage = AllocationStage::Notify;
		if (!EventBus.hasObservers())
		{
			return;
		}

		MemoryEvent event;
		event.Type = MemoryEventType::Deallocate;
		event.UserPtr = Context.Runtime.UserPtr;
		event.Size = Context.Descriptor.Size;
		event.Alignment = Context.Descriptor.Alignment;
		event.AllocationId = Context.Metadata.AllocationId;
		event.Timestamp = Context.Metadata.Timestamp;
		event.ThreadId = Context.Metadata.ThreadId;
		event.FrameIndex = Context.Metadata.FrameIndex;
		event.ResourceId = Context.Metadata.ResourceId;
		event.FromThreadCache = !Context.Runtime.IsFromOS;
		event.FromCentralPool = !Context.Runtime.IsFromOS;
		event.FromOS = Context.Runtime.IsFromOS;
		event.Context = &Context;
		EventBus.emit(event);

		if (!Context.Runtime.IsFromOS)
		{
			event.Type = MemoryEventType::CentralReturn;
			EventBus.emit(event);
		}
	}

	void emitValidationFailure(AllocationContext& Context)
	{
		if (!EventBus.hasObservers())
		{
			return;
		}

		MemoryEvent event;
		event.Type = MemoryEventType::ValidationError;
		event.UserPtr = Context.Runtime.UserPtr;
		event.Size = Context.Descriptor.Size;
		event.Alignment = Context.Descriptor.Alignment;
		event.AllocationId = Context.Metadata.AllocationId;
		event.Timestamp = Context.Metadata.Timestamp;
		event.ThreadId = Context.Metadata.ThreadId;
		event.FrameIndex = Context.Metadata.FrameIndex;
		event.ResourceId = Context.Metadata.ResourceId;
		event.Context = &Context;
		EventBus.emit(event);
	}

	void emitGuardCorruption(AllocationContext& Context) noexcept
	{
		if (!EventBus.hasObservers())
		{
			return;
		}

		MemoryEvent event;
		event.Type = MemoryEventType::GuardCorruption;
		event.UserPtr = Context.Runtime.UserPtr;
		event.Size = Context.Descriptor.Size;
		event.Alignment = Context.Descriptor.Alignment;
		event.AllocationId = Context.Metadata.AllocationId;
		event.Timestamp = Context.Metadata.Timestamp;
		event.ThreadId = Context.Metadata.ThreadId;
		event.FrameIndex = Context.Metadata.FrameIndex;
		event.ResourceId = Context.Metadata.ResourceId;
		event.Context = &Context;
		EventBus.emit(event);
	}

	void emitUseAfterFree(const SmallBlockQuarantineEntry& Entry) noexcept
	{
		if (!EventBus.hasObservers())
		{
			return;
		}

		MemoryEvent event;
		event.Type = MemoryEventType::UseAfterFree;
		event.UserPtr = Entry.RawPtr + Entry.UserOffset;
		event.Size = Entry.UserSize;
		event.Alignment = 0;
		event.AllocationId = Entry.AllocationId;
		event.Timestamp = timestampNs();
		event.ThreadId = std::this_thread::get_id();
		event.FrameIndex = Entry.FrameIndex;
		event.ResourceId = Entry.ResourceId;
		event.Context = nullptr;
		EventBus.emit(event);
	}

	[[nodiscard]] std::uint32_t captureStackId() const noexcept
	{
		if (!Config.CaptureStack)
		{
			return 0;
		}

#if defined(_WIN32)
		void* frames[16]{};
		const USHORT count = ::RtlCaptureStackBackTrace(2, 16, frames, nullptr);
		std::uint64_t hash = 1469598103934665603ull;
		for (USHORT i = 0; i < count; ++i)
		{
			hash ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(frames[i]));
			hash *= 1099511628211ull;
		}
		return static_cast<std::uint32_t>(hash ^ (hash >> 32));
#else
		return 0;
#endif
	}

	[[nodiscard]] static bool checkedAdd(
		const std::size_t left,
		const std::size_t right,
		std::size_t& out) noexcept
	{
		if (left > std::numeric_limits<std::size_t>::max() - right)
		{
			out = 0;
			return false;
		}

		out = left + right;
		return true;
	}

	AllocatorConfig Config;
	PageAllocator PageAllocator;
	CentralPool CentralPool;
	MemoryEventBus EventBus;

	std::atomic<std::uint64_t> NextAllocationId = 1;
	std::atomic<std::uint32_t> CurrentFrame = 0;

	std::mutex DedicatedMutex;
	std::unordered_map<void*, DedicatedAllocation> DedicatedAllocations;

	std::mutex DedicatedCacheMutex;
	std::unordered_map<std::size_t, std::vector<VirtualRegion>> DedicatedRegionCache;
	std::size_t DedicatedCacheBytes = 0;

	std::mutex QuarantineMutex;
	std::deque<SmallBlockQuarantineEntry> SmallBlockQuarantine;
	std::size_t QuarantineBytes = 0;
};

class AllocatorFacade
{
public:
	/**
	 * @brief Configure allocator before first runtime use.
	 * @param
	 *     - Config  const AllocatorConfig&  Runtime allocator options.
	 * @usage
	 *     - Invoke at engine bootstrap before any memory allocation call.
	 * @return
	 *     - void
	 */
	static void configure(const AllocatorConfig& Config)
	{
		MemoryAllocatorEngine::configureBeforeFirstUse(Config);
	}

	/**
	 * @brief Allocate memory block from the unified allocator pipeline.
	 * @param
	 *     - Size        std::size_t   Requested bytes.
	 *     - Alignment   std::size_t   Required Alignment.
	 *     - ResourceId  std::uint32_t Optional resource grouping id.
	 *     - ZeroMemory  bool          Whether user payload should be zero-filled.
	 * @usage
	 *     - Use as default entry for engine systems that need tracked allocations.
	 * @return
	 *     - void*  User payload pointer. Throws std::bad_alloc on failure.
	 */
	[[nodiscard]] static void* allocate(
		const std::size_t Size,
		const std::size_t Alignment = alignof(std::max_align_t),
		const std::uint32_t ResourceId = 0,
		const bool ZeroMemory = false,
		const AllocationLifetime Lifetime = AllocationLifetime::Default)
	{
		AllocationDescriptor Descriptor;
		Descriptor.Size = Size;
		Descriptor.Alignment = Alignment;
		Descriptor.IsZeroMemory = ZeroMemory;
		Descriptor.ResourceId = ResourceId;
		Descriptor.Lifetime = Lifetime;

		void* Ptr = MemoryAllocatorEngine::Self().allocate(Descriptor);
		if (!Ptr)
		{
			throw std::bad_alloc();
		}

		return Ptr;
	}

	/**
	 * @brief Free memory block allocated by AllocatorFacade::allocate.
	 * @param
	 *     - Ptr         void*         Pointer returned by allocate.
	 *     - Size        std::size_t   Original requested Size.
	 *     - Alignment   std::size_t   Original Alignment.
	 *     - ResourceId  std::uint32_t Optional resource grouping id.
	 * @usage
	 *     - Always pass the same Size/Alignment metadata used on allocation.
	 * @return
	 *     - void
	 */
	static void deallocate(
		void* Ptr,
		const std::size_t Size,
		const std::size_t Alignment = alignof(std::max_align_t),
		const std::uint32_t ResourceId = 0,
		const AllocationLifetime Lifetime = AllocationLifetime::Default) noexcept
	{
		if (!Ptr)
		{
			return;
		}

		AllocationDescriptor Descriptor;
		Descriptor.Size = Size;
		Descriptor.Alignment = Alignment;
		Descriptor.ResourceId = ResourceId;
		Descriptor.Lifetime = Lifetime;
		MemoryAllocatorEngine::Self().deallocate(Ptr, Descriptor);
	}

	/**
	 * @brief Allocate memory marked as transient/hot-lifetime.
	 * @param
	 *     - Size        std::size_t   Requested bytes.
	 *     - Alignment   std::size_t   Required Alignment.
	 *     - ResourceId  std::uint32_t Optional resource grouping id.
	 *     - ZeroMemory  bool          Whether user payload should be zero-filled.
	 * @usage
	 *     - Use for frame-local or short-lived objects to favor fast reuse paths.
	 * @return
	 *     - void*  User payload pointer.
	 */
	[[nodiscard]] static void* allocateTransient(
		const std::size_t Size,
		const std::size_t Alignment = alignof(std::max_align_t),
		const std::uint32_t ResourceId = 0,
		const bool ZeroMemory = false)
	{
		return allocate(Size, Alignment, ResourceId, ZeroMemory, AllocationLifetime::Transient);
	}

	/**
	 * @brief Allocate memory marked as persistent/long-lived.
	 * @param
	 *     - Size        std::size_t   Requested bytes.
	 *     - Alignment   std::size_t   Required Alignment.
	 *     - ResourceId  std::uint32_t Optional resource grouping id.
	 *     - ZeroMemory  bool          Whether user payload should be zero-filled.
	 * @usage
	 *     - Use for resources with long lifetime to reduce TLS cache pollution.
	 * @return
	 *     - void*  User payload pointer.
	 */
	[[nodiscard]] static void* allocatePersistent(
		const std::size_t Size,
		const std::size_t Alignment = alignof(std::max_align_t),
		const std::uint32_t ResourceId = 0,
		const bool ZeroMemory = false)
	{
		return allocate(Size, Alignment, ResourceId, ZeroMemory, AllocationLifetime::Persistent);
	}

	/**
	 * @brief Free memory allocated by allocateTransient().
	 * @param
	 *     - Ptr         void*         Pointer returned by allocateTransient().
	 *     - Size        std::size_t   Original requested Size.
	 *     - Alignment   std::size_t   Original Alignment.
	 *     - ResourceId  std::uint32_t Optional resource grouping id.
	 * @usage
	 *     - Pair with allocateTransient() to keep lifetime hints consistent.
	 * @return
	 *     - void
	 */
	static void deallocateTransient(
		void* Ptr,
		const std::size_t Size,
		const std::size_t Alignment = alignof(std::max_align_t),
		const std::uint32_t ResourceId = 0) noexcept
	{
		deallocate(Ptr, Size, Alignment, ResourceId, AllocationLifetime::Transient);
	}

	/**
	 * @brief Free memory allocated by allocatePersistent().
	 * @param
	 *     - Ptr         void*         Pointer returned by allocatePersistent().
	 *     - Size        std::size_t   Original requested Size.
	 *     - Alignment   std::size_t   Original Alignment.
	 *     - ResourceId  std::uint32_t Optional resource grouping id.
	 * @usage
	 *     - Pair with allocatePersistent() to keep lifetime hints consistent.
	 * @return
	 *     - void
	 */
	static void deallocatePersistent(
		void* Ptr,
		const std::size_t Size,
		const std::size_t Alignment = alignof(std::max_align_t),
		const std::uint32_t ResourceId = 0) noexcept
	{
		deallocate(Ptr, Size, Alignment, ResourceId, AllocationLifetime::Persistent);
	}

	template <typename ObserverType>
	/**
	 * @brief Bind Observer to global memory event bus.
	 * @param
	 *     - Observer  ObserverType&  Observer instance implementing onMemoryEvent().
	 * @usage
	 *     - Bind statistics/leak/profiler modules without changing allocator core.
	 * @return
	 *     - void
	 */
	static void bindObserver(ObserverType& Observer)
	{
		MemoryAllocatorEngine::Self().getEventBus().bind(Observer);
	}

	template <typename ObserverType>
	/**
	 * @brief Unbind Observer from global memory event bus.
	 * @param
	 *     - Observer  ObserverType&  Observer instance previously bound.
	 * @usage
	 *     - Call during subsystem shutdown to avoid dangling callbacks.
	 * @return
	 *     - void
	 */
	static void unbindObserver(ObserverType& Observer)
	{
		MemoryAllocatorEngine::Self().getEventBus().unbind(Observer);
	}

	/**
	 * @brief Access global memory event bus.
	 * @param
	 *     - none
	 * @usage
	 *     - Bind statistics, leak, and profiling observers through this API.
	 * @return
	 *     - MemoryEventBus&  Shared event bus instance.
	 */
	[[nodiscard]] static MemoryEventBus& getEventBus()
	{
		return MemoryAllocatorEngine::Self().getEventBus();
	}

	/**
	 * @brief Flush and release current thread cache explicitly.
	 * @param
	 *     - none
	 * @usage
	 *     - Call on worker-thread shutdown in long-lived processes.
	 * @return
	 *     - void
	 */
	static void onThreadExit() noexcept
	{
		MemoryAllocatorEngine::Self().onThreadExit();
	}

	/**
	 * @brief Explicitly flush deferred frees (UAF quarantine) to allocator backends.
	 * @param
	 *     - none
	 * @usage
	 *     - Useful at frame boundaries or test checkpoints requiring deterministic release.
	 * @return
	 *     - void
	 */
	static void flushDeferredFrees() noexcept
	{
		MemoryAllocatorEngine::Self().flushDeferredFrees();
	}

	/**
	 * @brief Set current Frame id injected into allocation metadata.
	 * @param
	 *     - Frame  std::uint32_t  Frame number.
	 * @usage
	 *     - Update once per Frame before gameplay allocations.
	 * @return
	 *     - void
	 */
	static void setCurrentFrame(const std::uint32_t Frame) noexcept
	{
		MemoryAllocatorEngine::Self().setCurrentFrame(Frame);
	}

	/**
	 * @brief Batch-prewarm allocator classes for startup or level-transition spikes.
	 * @param
	 *     - Requests  const std::vector<MemoryPrewarmRequest>&  Batch of prewarm requests.
	 * @usage
	 *     - Execute in loading phases to pre-touch spans and reduce first-use hitches.
	 * @return
	 *     - std::size_t  Number of warmed allocations successfully executed.
	 */
	[[nodiscard]] static std::size_t prewarm(const std::vector<MemoryPrewarmRequest>& Requests)
	{
		return MemoryAllocatorEngine::Self().prewarm(Requests);
	}

};

template <typename Ty>
class EngineAllocator
{
public:
	using value_type = Ty;

	/**
	 * @brief Default construct STL adapter allocator.
	 * @param
	 *     - none
	 * @usage
	 *     - Use as std container allocator argument.
	 * @return
	 *     - void
	 */
	constexpr EngineAllocator() noexcept = default;

	template <typename Other>
	constexpr EngineAllocator(const EngineAllocator<Other>&) noexcept
	{
	}

	/**
	 * @brief Allocate contiguous typed storage for STL container.
	 * @param
	 *     - count  std::size_t  Number of elements.
	 * @usage
	 *     - Called by STL container internals during growth.
	 * @return
	 *     - Ty*  Typed storage pointer.
	 */
	[[nodiscard]] Ty* allocate(const std::size_t count)
	{
		const std::size_t bytes = count * sizeof(Ty);
		void* Ptr = AllocatorFacade::allocate(bytes, alignof(Ty));
		return static_cast<Ty*>(Ptr);
	}

	/**
	 * @brief Free typed storage previously allocated by this allocator.
	 * @param
	 *     - Ptr    Ty*          Storage pointer.
	 *     - count  std::size_t  Number of elements.
	 * @usage
	 *     - Called by STL container internals during shrink/destruction.
	 * @return
	 *     - void
	 */
	void deallocate(Ty* Ptr, const std::size_t count) noexcept
	{
		AllocatorFacade::deallocate(Ptr, count * sizeof(Ty), alignof(Ty));
	}

	template <typename Other>
	[[nodiscard]] constexpr bool operator==(const EngineAllocator<Other>&) const noexcept
	{
		return true;
	}

	template <typename Other>
	[[nodiscard]] constexpr bool operator!=(const EngineAllocator<Other>&) const noexcept
	{
		return false;
	}
};

// PMR 兼容层必须继承 std::pmr::memory_resource，此处虚函数仅存在于标准库边界。
class EngineMemoryResource final : public std::pmr::memory_resource
{
protected:
	[[nodiscard]] void* do_allocate(const std::size_t bytes, const std::size_t Alignment) override
	{
		return AllocatorFacade::allocate(bytes, Alignment);
	}

	void do_deallocate(void* Ptr, const std::size_t bytes, const std::size_t Alignment) override
	{
		AllocatorFacade::deallocate(Ptr, bytes, Alignment);
	}

	[[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
	{
		return this == &other;
	}
};

} // namespace core::mem