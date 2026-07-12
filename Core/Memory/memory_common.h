
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#if (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__))
#include <immintrin.h>
#endif

namespace core::mem
{

inline constexpr bool MemoryDebugEnabled =
#if defined(NDEBUG) && !defined(CORE_MEM_FORCE_DEBUG)
	false;
#else
	true;
#endif

#if defined(NDEBUG) && !defined(CORE_MEM_FORCE_DEBUG)
#define CORE_MEM_DEFAULT_DEBUG_FEATURES 0
#else
#define CORE_MEM_DEFAULT_DEBUG_FEATURES 1
#endif

#ifndef CORE_MEM_CFG_ENABLE_DEBUG_GUARDS
#define CORE_MEM_CFG_ENABLE_DEBUG_GUARDS CORE_MEM_DEFAULT_DEBUG_FEATURES
#endif

#ifndef CORE_MEM_CFG_ENABLE_UAF_DETECTION
#define CORE_MEM_CFG_ENABLE_UAF_DETECTION CORE_MEM_DEFAULT_DEBUG_FEATURES
#endif

#ifndef CORE_MEM_CFG_CAPTURE_STACK
#define CORE_MEM_CFG_CAPTURE_STACK 0
#endif

#ifndef CORE_MEM_CFG_QUARANTINE_RELEASE_ONLY_ON_FLUSH
#define CORE_MEM_CFG_QUARANTINE_RELEASE_ONLY_ON_FLUSH 1
#endif

#ifndef CORE_MEM_CFG_LAZY_COMMIT
#define CORE_MEM_CFG_LAZY_COMMIT 1
#endif

inline constexpr bool MemoryCompileEnableDebugGuards = CORE_MEM_CFG_ENABLE_DEBUG_GUARDS != 0;
inline constexpr bool MemoryCompileEnableUseAfterFreeDetection = CORE_MEM_CFG_ENABLE_UAF_DETECTION != 0;
inline constexpr bool MemoryCompileCaptureStack = CORE_MEM_CFG_CAPTURE_STACK != 0;
inline constexpr bool MemoryCompileQuarantineReleaseOnlyOnFlush =
	CORE_MEM_CFG_QUARANTINE_RELEASE_ONLY_ON_FLUSH != 0;
inline constexpr bool MemoryCompileLazyCommit = CORE_MEM_CFG_LAZY_COMMIT != 0;

inline constexpr std::uint8_t AllocatedPattern = 0xCD;
inline constexpr std::uint8_t FreedPattern = 0xDD;
inline constexpr std::uint8_t FrontGuardPattern = 0xCD;
inline constexpr std::uint8_t BackGuardPattern = 0xFD;
inline constexpr std::uint32_t DebugHeaderMagic = 0x504D454D; // "PMEM"
inline constexpr std::uint16_t InvalidSizeClass = std::numeric_limits<std::uint16_t>::max();

[[nodiscard]] constexpr bool isPowerOfTwo(const std::size_t value) noexcept
{
	return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] constexpr std::size_t alignUp(const std::size_t value, const std::size_t alignment) noexcept
{
	return (value + alignment - 1) & ~(alignment - 1);
}

[[nodiscard]] inline std::uint64_t timestampNs() noexcept
{
	using clock = std::chrono::steady_clock;
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch()).count());
}

inline void spinPause() noexcept
{
#if (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__))
	_mm_pause();
#else
	std::this_thread::yield();
#endif
}

struct SpinLock
{
	void lock() noexcept
	{
		while (Locked.test_and_set(std::memory_order_acquire))
		{
			spinPause();
		}
	}

	void unlock() noexcept
	{
		Locked.clear(std::memory_order_release);
	}

private:
	std::atomic_flag Locked = ATOMIC_FLAG_INIT;
};

enum class AllocationStage : std::uint16_t
{
	Prepare = 0,
	Validate = 1,
	Layout = 2,
	Allocate = 3,
	Initialize = 4,
	Notify = 5,
	Return = 6,
	Free = 7,
	Destroy = 8,
};

enum class MemoryEventType : std::uint8_t
{
	Allocate,
	Deallocate,
	Reallocate,
	Reserve,
	Commit,
	Decommit,
	Release,
	ThreadCacheHit,
	ThreadCacheMiss,
	CentralFetch,
	CentralReturn,
	PageAllocate,
	PageFree,
	GuardCorruption,
	UseAfterFree,
	ValidationError,
};

enum class AllocationLifetime : std::uint8_t
{
	Default,
	Transient,
	Persistent,
};

struct AllocationDescriptor
{
	std::size_t Size = 0;
	std::size_t Alignment = alignof(std::max_align_t);
	bool IsZeroMemory = false;
	bool IsNoThrow = false;
	AllocationLifetime Lifetime = AllocationLifetime::Default;
	std::uint32_t FrameIndex = 0;
	std::uint32_t ResourceId = 0;
};

struct MemoryPrewarmRequest
{
	std::size_t Size = 0;
	std::size_t Alignment = alignof(std::max_align_t);
	std::uint32_t Count = 0;
	std::uint32_t ResourceId = 0;
	bool ZeroInitialize = false;
	AllocationLifetime Lifetime = AllocationLifetime::Transient;
};

struct AllocationLayout
{
	std::size_t RequestedSize = 0;
	std::size_t RequestedAlignment = alignof(std::max_align_t);

	std::size_t TotalSize = 0;
	std::size_t HeaderSize = 0;
	std::size_t FrontGuard = 0;
	std::size_t BackGuard = 0;
	std::size_t UserOffset = 0;
	std::size_t BlockSize = 0;
	std::uint16_t SizeClassIndex = InvalidSizeClass;
	bool IsSmallAllocation = false;
};

struct AllocationMetadata
{
	std::uint64_t AllocationId = 0;
	std::uint32_t StackId = 0;
	std::thread::id ThreadId = std::this_thread::get_id();
	std::uint32_t ResourceId = 0;
	std::uint32_t ArenaId = 0;
	std::uint64_t Timestamp = 0;
	std::uint32_t FrameIndex = 0;
};

struct AllocationRuntime
{
	std::byte* RawPtr = nullptr;
	std::byte* UserPtr = nullptr;
	void* Owner = nullptr;
	void* InternalHandle = nullptr;
	bool IsFromThreadCache = false;
	bool IsFromCentralPool = false;
	bool IsFromOS = false;
};

struct AllocationResult
{
	bool IsSuccess = false;
	std::error_code ErrorCode;
};

struct AllocationContext
{
	AllocationDescriptor Descriptor;
	AllocationLayout Layout;
	AllocationMetadata Metadata;
	AllocationRuntime Runtime;
	AllocationResult Result;
	AllocationStage Stage = AllocationStage::Prepare;

	constexpr void reset() noexcept
	{
		Descriptor = {};
		Layout = {};
		Metadata = {};
		Runtime = {};
		Result = {};
		Stage = AllocationStage::Prepare;
	}
};

struct DebugHeader
{
	std::uint32_t Magic = 0;
	std::uint32_t Version = 1;
	std::uint64_t AllocationId = 0;
	std::uint64_t RequestedSize = 0;
	std::uint32_t RequestedAlignment = 0;
	std::uint16_t SizeClassIndex = InvalidSizeClass;
	std::uint16_t Reserved = 0;
	std::uint32_t FrontGuardBytes = 0;
	std::uint32_t BackGuardBytes = 0;
};

template <typename Stage>
concept CAllocationStage = requires(AllocationContext& Context)
{
	{ Stage::allocate(Context) } -> std::same_as<void>;
	{ Stage::deallocate(Context) } -> std::same_as<void>;
};

struct MemoryEvent
{
	MemoryEventType Type = MemoryEventType::Allocate;
	void* UserPtr = nullptr;
	std::size_t Size = 0;
	std::size_t Alignment = 0;
	std::uint64_t AllocationId = 0;
	std::uint64_t Timestamp = 0;
	std::thread::id ThreadId = std::this_thread::get_id();
	std::uint32_t FrameIndex = 0;
	std::uint32_t ResourceId = 0;
	bool FromThreadCache = false;
	bool FromCentralPool = false;
	bool FromOS = false;
	const AllocationContext* Context = nullptr;
};

template <typename Ty>
concept CMemoryObserver = requires(Ty& Observer, const MemoryEvent& Event)
{
	{ Observer.onMemoryEvent(Event) } -> std::same_as<void>;
};

struct ObserverEntry
{
	void* Object = nullptr;
	void (*Callback)(void*, const MemoryEvent&) = nullptr;
};

class MemoryEventBus
{
public:
	MemoryEventBus()
		: Snapshot(std::make_shared<const std::vector<ObserverEntry>>())
	{
	}

	template <CMemoryObserver ObserverType>
	void bind(ObserverType& Observer)
	{
		ObserverEntry Entry;
		Entry.Object = &Observer;
		Entry.Callback = [](void* object, const MemoryEvent& event) {
			static_cast<ObserverType*>(object)->onMemoryEvent(event);
		};

		std::lock_guard Lock(MutationMutex);
		auto Next = std::make_shared<std::vector<ObserverEntry>>(*Snapshot.load(std::memory_order_acquire));
		Next->push_back(Entry);
		Snapshot.store(Next, std::memory_order_release);
	}

	template <CMemoryObserver ObserverType>
	void unbind(ObserverType& Observer)
	{
		std::lock_guard Lock(MutationMutex);
		auto Next = std::make_shared<std::vector<ObserverEntry>>(*Snapshot.load(std::memory_order_acquire));
		Next->erase(
			std::remove_if(Next->begin(), Next->end(), [&](const ObserverEntry& Entry) {
				return Entry.Object == &Observer;
			}),
			Next->end());
		Snapshot.store(Next, std::memory_order_release);
	}

	[[nodiscard]] bool hasObservers() const noexcept
	{
		const auto RetSnapshot = Snapshot.load(std::memory_order_acquire);
		return RetSnapshot && !RetSnapshot->empty();
	}

	void emit(const MemoryEvent& Event) const
	{
		const auto RetSnapshot = Snapshot.load(std::memory_order_acquire);
		if (!RetSnapshot)
		{
			return;
		}

		for (const ObserverEntry& Entry : *RetSnapshot)
		{
			Entry.Callback(Entry.Object, Event);
		}
	}

private:
	std::mutex MutationMutex;
	std::atomic<std::shared_ptr<const std::vector<ObserverEntry>>> Snapshot;
};

struct AllocatorConfig
{
	std::size_t ThreadCacheMaxBytes = 16u * 1024u * 1024u;
	std::uint16_t ThreadCacheMaxPerClass = 1024;
	std::uint16_t RefillBatchSize = 64;
	std::size_t SmallRegionReserveBytes = 64u * 1024u * 1024u;
	std::size_t DedicatedRegionReserveBytes = 2u * 1024u * 1024u;
	bool EnableHugePage = false;
	bool EnableDedicatedRegionCache = true;
	std::size_t DedicatedRegionCacheLimitBytes = 128u * 1024u * 1024u;
	std::size_t DedicatedRegionCacheMaxEntriesPerSize = 8;
	std::size_t UseAfterFreeQuarantineBytes = 512u * 1024u;
	std::size_t UseAfterFreeQuarantineMaxEntries = 8192;
	std::int32_t PreferredNumaNode = -1;
};

} // namespace core::mem