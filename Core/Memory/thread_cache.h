#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "central_pool.h"
// #include "FreeList.h"

namespace core::mem
{

struct FreeList
{
	void* Head = nullptr;
	std::uint16_t Count = 0;
	std::uint16_t MaxCount = 0;
	Span* SpanHint = nullptr;
};

struct DeferredFreeList
{
	void* Head = nullptr;
	std::uint16_t Count = 0;
	Span* SpanHint = nullptr;
};

struct ThreadCacheAllocation
{
	void* Block = nullptr;
	bool Hit = false;
	bool FromCentral = false;
};

class ThreadCache
{
public:
	explicit ThreadCache(CentralPool& CentralPool, const AllocatorConfig& Config)
		: CentralPool(&CentralPool)
		, MaxBytes(Config.ThreadCacheMaxBytes)
		, DeferredBudgetBytes(std::max<std::size_t>(
			1,
			Config.ThreadCacheMaxBytes / tuning::ThreadCacheDeferredBudgetDivisor))
		, RefillBatch(Config.RefillBatchSize)
	{
		const std::size_t BytesPerClass = std::max<std::size_t>(
			tuning::ThreadCacheMinBytesPerClass,
			Config.ThreadCacheMaxBytes / NumSizeClasses);

		for (std::size_t I = 0; I < NumSizeClasses; ++I)
		{
			const std::size_t BlockSize = classIndexToSize(static_cast<std::uint16_t>(I));
			const std::size_t CountFromBytes = std::max<std::size_t>(
				tuning::ThreadCacheMinObjectsPerClass,
				BytesPerClass / BlockSize);
			const std::size_t Capped = std::min<std::size_t>(CountFromBytes, Config.ThreadCacheMaxPerClass);
			FreeLists[I].MaxCount = static_cast<std::uint16_t>(Capped);
		}
	}

	ThreadCache(const ThreadCache&) = delete;
	ThreadCache& operator=(const ThreadCache&) = delete;

	[[nodiscard]] ThreadCacheAllocation allocate(
		const std::uint16_t SizeClassIndex,
		const std::size_t BlockSize)
	{
		CORE_MEM_BENCH_SCOPE_SC(MemoryBenchPoint::ThreadCacheAllocate, SizeClassIndex);

		if (SizeClassIndex >= NumSizeClasses) [[unlikely]]
		{
			return {};
		}

		FreeList& FreeList = FreeLists[SizeClassIndex];
		if (!FreeList.Head)
		{
			refillFromDeferred(SizeClassIndex, BlockSize);
		}

		if (FreeList.Head) [[likely]]
		{
			void* Block = popBlock(FreeList.Head);
			--FreeList.Count;
			CurrentBytes -= BlockSize;

			ThreadCacheAllocation Result;
			Result.Block = Block;
			Result.Hit = true;
			return Result;
		}

		constexpr std::uint32_t MaxBatch = 128;
		void* Batch[MaxBatch]{};
		Span* FetchedSpanHint = nullptr;
		const std::uint32_t Target = std::min<std::uint32_t>(RefillBatch, MaxBatch);
		const std::uint32_t Fetched = CentralPool->fetchBatch(SizeClassIndex, Batch, Target, &FetchedSpanHint);
		if (Fetched == 0)
		{
			return {};
		}

		if (FetchedSpanHint)
		{
			FreeList.SpanHint = FetchedSpanHint;
		}

		for (std::uint32_t I = 1; I < Fetched; ++I)
		{
			pushBlock(FreeList.Head, Batch[I]);
			++FreeList.Count;
			CurrentBytes += BlockSize;
		}

		ThreadCacheAllocation Result;
		Result.Block = Batch[0];
		Result.FromCentral = true;
		return Result;
	}

	void deallocate(
		const std::uint16_t SizeClassIndex,
		const std::size_t BlockSize,
		void* Ptr)
	{
		CORE_MEM_BENCH_SCOPE_SC(MemoryBenchPoint::ThreadCacheDeallocate, SizeClassIndex);

		if (SizeClassIndex >= NumSizeClasses || !Ptr)
		{
			return;
		}

		FreeList& FreeList = FreeLists[SizeClassIndex];
		const bool RouteDeferred = shouldRouteToDeferred(SizeClassIndex, Ptr);
		if (RouteDeferred)
		{
			DeferredFreeList& Deferred = DeferredLists[SizeClassIndex];
			pushBlock(Deferred.Head, Ptr);
			++Deferred.Count;
			DeferredBytes += BlockSize;
		}
		else
		{
			pushBlock(FreeList.Head, Ptr);
			++FreeList.Count;
			CurrentBytes += BlockSize;
		}

		if (FreeList.Count > FreeList.MaxCount || CurrentBytes > MaxBytes)
		{
			const std::uint16_t SpillCount = FreeList.MaxCount > 0
				? static_cast<std::uint16_t>(
					std::max<std::uint32_t>(
						tuning::ThreadCacheSpillMinCount,
						std::min<std::uint32_t>(
							RefillBatch,
							static_cast<std::uint32_t>(FreeList.MaxCount / 4 + 1))))
				: 1;
			spillToDeferred(SizeClassIndex, BlockSize, SpillCount);
		}

		if (DeferredBytes > DeferredBudgetBytes)
		{
			drainDeferredToCentral(false);
		}

		const std::size_t TriggerBytes =
			(MaxBytes * tuning::ThreadCacheTrimTriggerNumerator) /
			tuning::ThreadCacheTrimTriggerDenominator;
		if (getTotalRetainedBytes() > TriggerBytes)
		{
			trimToBudget(false);
		}
	}

	void onIdle()
	{
		drainDeferredToCentral(true);
		trimToBudget(true);
	}

	void flushAll()
	{
		drainDeferredToCentral(true);

		for (std::size_t I = 0; I < NumSizeClasses; ++I)
		{
			const std::size_t BlockSize = classIndexToSize(static_cast<std::uint16_t>(I));
			drainToCentral(static_cast<std::uint16_t>(I), BlockSize, FreeLists[I].Count, FreeLists[I].SpanHint);
		}
	}

	[[nodiscard]] std::size_t getCurrentBytes() const noexcept
	{
		return CurrentBytes;
	}

	[[nodiscard]] std::size_t getDeferredBytes() const noexcept
	{
		return DeferredBytes;
	}

private:
	[[nodiscard]] std::size_t getTotalRetainedBytes() const noexcept
	{
		return CurrentBytes + DeferredBytes;
	}

	[[nodiscard]] static bool isBlockInSpanHint(
		const Span* Hint,
		const void* Ptr,
		const std::uint16_t SizeClassIndex) noexcept
	{
		if (!Hint || Hint->IsReleased || Hint->SizeClassIndex != SizeClassIndex)
		{
			return false;
		}

		const std::uintptr_t Address = reinterpret_cast<std::uintptr_t>(Ptr);
		const std::uintptr_t Begin = reinterpret_cast<std::uintptr_t>(Hint->SpanBase);
		const std::uintptr_t End = Begin +
			static_cast<std::uintptr_t>(Hint->BlockSize) * static_cast<std::uintptr_t>(Hint->TotalBlocks);
		return Address >= Begin && Address < End;
	}

	[[nodiscard]] bool shouldRouteToDeferred(const std::uint16_t SizeClassIndex, void* Ptr)
	{
		if (SizeClassIndex >= NumSizeClasses || !Ptr)
		{
			return false;
		}

		FreeList& FreeList = FreeLists[SizeClassIndex];
		if (isBlockInSpanHint(FreeList.SpanHint, Ptr, SizeClassIndex))
		{
			return false;
		}

		Span* ResolvedHint = nullptr;
		const bool IsCurrentShard = CentralPool->isCurrentThreadShardForPointer(
			Ptr,
			SizeClassIndex,
			FreeList.SpanHint,
			&ResolvedHint);
		if (ResolvedHint)
		{
			FreeList.SpanHint = ResolvedHint;
			DeferredLists[SizeClassIndex].SpanHint = ResolvedHint;
		}

		return !IsCurrentShard;
	}

	void refillFromDeferred(
		const std::uint16_t SizeClassIndex,
		const std::size_t BlockSize)
	{
		CORE_MEM_BENCH_SCOPE_SC(MemoryBenchPoint::ThreadCacheRefillDeferred, SizeClassIndex);

		if (SizeClassIndex >= NumSizeClasses)
		{
			return;
		}

		DeferredFreeList& Deferred = DeferredLists[SizeClassIndex];
		if (!Deferred.Head)
		{
			return;
		}

		FreeList& FreeList = FreeLists[SizeClassIndex];
		const std::uint16_t Target = std::max<std::uint16_t>(1, RefillBatch);
		std::uint16_t Moved = 0;
		while (Moved < Target && Deferred.Head)
		{
			void* Block = popBlock(Deferred.Head);
			--Deferred.Count;
			DeferredBytes -= BlockSize;
			pushBlock(FreeList.Head, Block);
			++FreeList.Count;
			CurrentBytes += BlockSize;
			++Moved;
		}

		if (!Deferred.Head)
		{
			Deferred.SpanHint = nullptr;
		}
	}

	void spillToDeferred(
		const std::uint16_t SizeClassIndex,
		const std::size_t BlockSize,
		const std::uint16_t RequestedCount)
	{
		CORE_MEM_BENCH_SCOPE_SC(MemoryBenchPoint::ThreadCacheSpillDeferred, SizeClassIndex);

		if (SizeClassIndex >= NumSizeClasses || RequestedCount == 0)
		{
			return;
		}

		FreeList& FreeList = FreeLists[SizeClassIndex];
		DeferredFreeList& Deferred = DeferredLists[SizeClassIndex];
		std::uint16_t Spilled = 0;
		while (Spilled < RequestedCount && FreeList.Head)
		{
			void* Block = popBlock(FreeList.Head);
			--FreeList.Count;
			CurrentBytes -= BlockSize;
			pushBlock(Deferred.Head, Block);
			++Deferred.Count;
			DeferredBytes += BlockSize;
			++Spilled;
		}

		if (Deferred.SpanHint == nullptr)
		{
			Deferred.SpanHint = FreeList.SpanHint;
		}

		if (!FreeList.Head)
		{
			FreeList.SpanHint = nullptr;
		}
	}

	void drainDeferredClassToCentral(
		const std::uint16_t SizeClassIndex,
		const std::size_t BlockSize,
		const std::uint16_t RequestedCount)
	{
		if (SizeClassIndex >= NumSizeClasses || RequestedCount == 0)
		{
			return;
		}

		DeferredFreeList& Deferred = DeferredLists[SizeClassIndex];
		if (!Deferred.Head)
		{
			return;
		}

		void* Chain = nullptr;
		std::uint32_t Drained = 0;
		while (Drained < RequestedCount && Deferred.Head)
		{
			void* Block = popBlock(Deferred.Head);
			*reinterpret_cast<void**>(Block) = Chain;
			Chain = Block;
			--Deferred.Count;
			DeferredBytes -= BlockSize;
			++Drained;
		}

		if (Chain && Drained > 0)
		{
			CentralPool->returnBatch(SizeClassIndex, Chain, Drained, Deferred.SpanHint);
		}

		if (!Deferred.Head)
		{
			Deferred.SpanHint = nullptr;
		}
	}

	void drainDeferredToCentral(const bool ForceAll)
	{
		CORE_MEM_BENCH_SCOPE(MemoryBenchPoint::ThreadCacheDrainDeferred);

		if (!ForceAll && DeferredBytes <= DeferredBudgetBytes)
		{
			return;
		}

		if (ForceAll)
		{
			for (std::size_t I = 0; I < NumSizeClasses; ++I)
			{
				const std::size_t BlockSize = classIndexToSize(static_cast<std::uint16_t>(I));
				drainDeferredClassToCentral(
					static_cast<std::uint16_t>(I),
					BlockSize,
					DeferredLists[I].Count);
			}
			return;
		}

		const std::uint16_t DrainBatch = static_cast<std::uint16_t>(
			std::max<std::uint32_t>(
				tuning::ThreadCacheDeferredDrainMinBatch,
				std::min<std::uint32_t>(
					tuning::ThreadCacheDeferredDrainMaxBatch,
					RefillBatch)));
		const std::uint32_t MaxBatches = static_cast<std::uint32_t>(NumSizeClasses) *
			tuning::ThreadCacheTrimMaxBatchesFactor;

		for (std::uint32_t Batch = 0; Batch < MaxBatches && DeferredBytes > DeferredBudgetBytes; ++Batch)
		{
			bool Drained = false;
			for (std::size_t Offset = 0; Offset < NumSizeClasses; ++Offset)
			{
				const std::size_t Index = (LastDeferredDrainClass + Offset) % NumSizeClasses;
				if (!DeferredLists[Index].Head)
				{
					continue;
				}

				const std::size_t BlockSize = classIndexToSize(static_cast<std::uint16_t>(Index));
				const std::uint16_t Requested = std::min<std::uint16_t>(
					DeferredLists[Index].Count,
					DrainBatch);
				drainDeferredClassToCentral(
					static_cast<std::uint16_t>(Index),
					BlockSize,
					Requested);
				LastDeferredDrainClass = (Index + 1) % NumSizeClasses;
				Drained = true;
				break;
			}

			if (!Drained)
			{
				break;
			}
		}
	}

	void drainToCentral(
		const std::uint16_t SizeClassIndex,
		const std::size_t BlockSize,
		const std::uint16_t RequestedCount,
		Span* SpanHint)
	{
		if (SizeClassIndex >= NumSizeClasses || RequestedCount == 0)
		{
			return;
		}

		FreeList& FreeList = FreeLists[SizeClassIndex];
		void* Chain = nullptr;
		std::uint32_t Drained = 0;

		while (Drained < RequestedCount && FreeList.Head)
		{
			void* Block = popBlock(FreeList.Head);
			*reinterpret_cast<void**>(Block) = Chain;
			Chain = Block;
			--FreeList.Count;
			CurrentBytes -= BlockSize;
			++Drained;
		}

		if (Chain && Drained > 0)
		{
			CentralPool->returnBatch(SizeClassIndex, Chain, Drained, SpanHint);
		}

		if (FreeList.Count == 0)
		{
			FreeList.SpanHint = nullptr;
		}
	}

	void trimToBudget(const bool ForceAll)
	{
		CORE_MEM_BENCH_SCOPE(MemoryBenchPoint::ThreadCacheTrimToBudget);

		if (!ForceAll && getTotalRetainedBytes() <= MaxBytes)
		{
			return;
		}

		drainDeferredToCentral(ForceAll);

		constexpr std::uint16_t MinDrainChunk = tuning::ThreadCacheTrimMinDrainChunk;
		constexpr std::uint16_t MaxDrainChunk = tuning::ThreadCacheTrimMaxDrainChunk;
		const std::uint16_t DrainChunk = static_cast<std::uint16_t>(
			std::max<std::uint32_t>(
				1,
				std::min<std::uint32_t>(
					MaxDrainChunk,
					std::max<std::uint32_t>(MinDrainChunk, RefillBatch / 2u))));

		if (ForceAll)
		{
			for (std::size_t I = 0; I < NumSizeClasses; ++I)
			{
				FreeList& FreeList = FreeLists[I];
				if (!FreeList.Head || FreeList.Count == 0)
				{
					continue;
				}

				const std::size_t BlockSize = classIndexToSize(static_cast<std::uint16_t>(I));
				drainToCentral(
					static_cast<std::uint16_t>(I),
					BlockSize,
					FreeList.Count,
					FreeList.SpanHint);
			}
			return;
		}

		const std::size_t TargetBytes =
			(MaxBytes * tuning::ThreadCacheTrimTargetNumerator) /
			tuning::ThreadCacheTrimTargetDenominator;
		constexpr std::uint32_t MaxBatchesPerPass =
			static_cast<std::uint32_t>(NumSizeClasses) * tuning::ThreadCacheTrimMaxBatchesFactor;
		for (std::uint32_t batch = 0; batch < MaxBatchesPerPass && CurrentBytes > TargetBytes; ++batch)
		{
			bool Drained = false;
			for (std::size_t offset = 0; offset < NumSizeClasses; ++offset)
			{
				const std::size_t index = (LastDrainClass + offset) % NumSizeClasses;
				FreeList& FreeList = FreeLists[index];
				if (!FreeList.Head || FreeList.Count == 0)
				{
					continue;
				}

				const std::size_t BlockSize = classIndexToSize(static_cast<std::uint16_t>(index));
				const std::uint16_t Requested = std::min<std::uint16_t>(FreeList.Count, DrainChunk);
				drainToCentral(
					static_cast<std::uint16_t>(index),
					BlockSize,
					Requested,
					FreeList.SpanHint);
				LastDrainClass = (index + 1) % NumSizeClasses;
				Drained = true;
				break;
			}

			if (!Drained)
			{
				break;
			}
		}
	}

	[[nodiscard]] static void* popBlock(void*& Head) noexcept
	{
		if (!Head) [[unlikely]]
		{
			return nullptr;
		}

		prefetchRead(Head);
		void* Block = Head;
		Head = *reinterpret_cast<void**>(Block);
		prefetchRead(Head);
		return Block;
	}

	static void pushBlock(void*& Head, void* Block) noexcept
	{
		prefetchWrite(Block);
		prefetchRead(Head);
		*reinterpret_cast<void**>(Block) = Head;
		Head = Block;
	}

	CentralPool* CentralPool = nullptr;
	std::array<FreeList, NumSizeClasses> FreeLists{};
	std::array<DeferredFreeList, NumSizeClasses> DeferredLists{};
	std::size_t CurrentBytes = 0;
	std::size_t DeferredBytes = 0;
	std::size_t MaxBytes = 0;
	std::size_t DeferredBudgetBytes = 0;
	std::uint16_t RefillBatch = 0;
	std::size_t LastDrainClass = 0;
	std::size_t LastDeferredDrainClass = 0;
};

class ThreadCacheRegistry
{
public:
	/**
	 * @brief Bind process-level central components for TLS cache creation.
	 * @param
	 *     - CentralPool  CentralPool&          Shared central pool instance.
	 *     - Config       const AllocatorConfig&  Runtime allocator configuration.
	 * @usage
	 *     - Must be called before the first get() on a thread.
	 * @return
	 *     - void
	 */
	static void bind(CentralPool& CentralPool, const AllocatorConfig& Config)
	{
		CentralPoolRef.store(&CentralPool, std::memory_order_release);
		ConfigRef.store(&Config, std::memory_order_release);
	}

	/**
	 * @brief Get thread-local cache instance for current thread.
	 * @param
	 *     - none
	 * @usage
	 *     - Hot allocation path should call this once then keep local reference.
	 * @return
	 *     - ThreadCache&  Current thread cache reference.
	 */
	[[nodiscard]] static ThreadCache& get()
	{
		CentralPool* CentralPool = CentralPoolRef.load(std::memory_order_acquire);
		const AllocatorConfig* Config = ConfigRef.load(std::memory_order_acquire);
		assert(CentralPool && "ThreadCacheRegistry is not bound to a CentralPool");
		assert(Config && "ThreadCacheRegistry is not bound to an AllocatorConfig");

		if (!Slot.Cache)
		{
			Slot.Cache = new ThreadCache(*CentralPool, *Config);
		}

		return *Slot.Cache;
	}

	/**
	 * @brief Flush and destroy the current thread cache explicitly.
	 * @param
	 *     - none
	 * @usage
	 *     - Call on known worker-thread shutdown boundaries.
	 * @return
	 *     - void
	 */
	static void onThreadExit()
	{
		if (Slot.Cache)
		{
			Slot.Cache->flushAll();
			delete Slot.Cache;
			Slot.Cache = nullptr;
		}
	}

	static void onThreadIdle()
	{
		if (Slot.Cache)
		{
			Slot.Cache->onIdle();
		}
	}

private:
	struct ThreadLocalSlot
	{
		ThreadCache* Cache = nullptr;

		~ThreadLocalSlot()
		{
			if (Cache)
			{
				Cache->flushAll();
				delete Cache;
				Cache = nullptr;
			}
		}
	};

	static std::atomic<CentralPool*> CentralPoolRef;
	static std::atomic<const AllocatorConfig*> ConfigRef;
	static thread_local ThreadLocalSlot Slot;
};

inline std::atomic<CentralPool*> ThreadCacheRegistry::CentralPoolRef = nullptr;
inline std::atomic<const AllocatorConfig*> ThreadCacheRegistry::ConfigRef = nullptr;
inline thread_local ThreadCacheRegistry::ThreadLocalSlot ThreadCacheRegistry::Slot{};

} // namespace core::mem
