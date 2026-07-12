
#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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

#ifndef CORE_MEM_CFG_ENABLE_INTERNAL_BENCH_TIMING
#define CORE_MEM_CFG_ENABLE_INTERNAL_BENCH_TIMING 0
#endif

inline constexpr bool MemoryCompileEnableDebugGuards = CORE_MEM_CFG_ENABLE_DEBUG_GUARDS != 0;
inline constexpr bool MemoryCompileEnableUseAfterFreeDetection = CORE_MEM_CFG_ENABLE_UAF_DETECTION != 0;
inline constexpr bool MemoryCompileCaptureStack = CORE_MEM_CFG_CAPTURE_STACK != 0;
inline constexpr bool MemoryCompileQuarantineReleaseOnlyOnFlush =
	CORE_MEM_CFG_QUARANTINE_RELEASE_ONLY_ON_FLUSH != 0;
inline constexpr bool MemoryCompileLazyCommit = CORE_MEM_CFG_LAZY_COMMIT != 0;
inline constexpr bool MemoryCompileEnableInternalBenchTiming =
	CORE_MEM_CFG_ENABLE_INTERNAL_BENCH_TIMING != 0;

inline constexpr std::uint8_t AllocatedPattern = 0xCD;
inline constexpr std::uint8_t FreedPattern = 0xDD;
inline constexpr std::uint8_t FrontGuardPattern = 0xCD;
inline constexpr std::uint8_t BackGuardPattern = 0xFD;
inline constexpr std::uint32_t DebugHeaderMagic = 0x504D454D; // "PMEM"
inline constexpr std::uint16_t InvalidSizeClass = std::numeric_limits<std::uint16_t>::max();

namespace tuning
{
	// -------------------- 自旋锁参数 --------------------
	// 初始 pause 次数，控制轻度竞争时的忙等成本。
	inline constexpr std::uint32_t SpinLockInitialPauseCount = 4;
	// pause 上限，超过后让出时间片避免高竞争下空转烧 CPU。
	inline constexpr std::uint32_t SpinLockMaxPauseCount = 128;

	// -------------------- CentralPool 参数 --------------------
	// 中央池每个 size-class 的最大分片数（按硬件并发自适应，不超过该上限）。
	inline constexpr std::size_t CentralPoolBucketMaxShardCount = 64;
	// 中央池 page->span 映射分片数，降低注册/查询的锁竞争。
	inline constexpr std::size_t CentralPoolPageMapShardCount = 64;
	// returnBatch 批处理分组上限，避免热路径动态分配。
	inline constexpr std::size_t CentralPoolMaxReturnBatchGroups = 8;
	// 每个分片最多保留空 span 数，超过后归还给页分配器。
	inline constexpr std::size_t CentralPoolMaxRetainedEmptySpansPerClass = 2;
	// span 元数据对象池每次扩容块大小。
	inline constexpr std::size_t CentralPoolSpanPoolChunkSize = 256;
	// 中央池分片数的最小值。
	inline constexpr std::size_t CentralPoolMinShardCount = 8;
	// 中央池分片数相对硬件线程数的缩放倍数。
	inline constexpr std::size_t CentralPoolShardScale = 2;
	// page-base 分片哈希右移位数（4KB 页对齐）。
	inline constexpr std::size_t CentralPoolPageShift = 12;

	// -------------------- ThreadCache 参数 --------------------
	// 每个 size-class 的最小预算字节数，避免小类被饿死。
	inline constexpr std::size_t ThreadCacheMinBytesPerClass = 256;
	// 每个 size-class 的最小对象数。
	inline constexpr std::size_t ThreadCacheMinObjectsPerClass = 8;
	// spill 到 deferred/central 的最小批量。
	inline constexpr std::uint16_t ThreadCacheSpillMinCount = 2;
	// trim 时每次 drain 的最小块数。
	inline constexpr std::uint16_t ThreadCacheTrimMinDrainChunk = 8;
	// trim 时每次 drain 的最大块数。
	inline constexpr std::uint16_t ThreadCacheTrimMaxDrainChunk = 32;
	// 触发 trim 的阈值倍率：MaxBytes * 5 / 4（即超 25%）。
	inline constexpr std::size_t ThreadCacheTrimTriggerNumerator = 5;
	inline constexpr std::size_t ThreadCacheTrimTriggerDenominator = 4;
	// 非强制 trim 的回落目标：MaxBytes * 9 / 8（即超 12.5% 即继续回收）。
	inline constexpr std::size_t ThreadCacheTrimTargetNumerator = 9;
	inline constexpr std::size_t ThreadCacheTrimTargetDenominator = 8;
	// 单次 trim 最多执行的批次数因子：NumSizeClasses * Factor。
	inline constexpr std::uint32_t ThreadCacheTrimMaxBatchesFactor = 2;
	// DeferredList 单次向 central 回流的最小批量。
	inline constexpr std::uint16_t ThreadCacheDeferredDrainMinBatch = 16;
	// DeferredList 单次向 central 回流的最大批量。
	inline constexpr std::uint16_t ThreadCacheDeferredDrainMaxBatch = 128;
	// Deferred 总预算占 ThreadCache 总预算的分母（1/2）。
	inline constexpr std::size_t ThreadCacheDeferredBudgetDivisor = 2;

	// -------------------- MemoryFacade 参数 --------------------
	// dedicated 元数据分片上限。
	inline constexpr std::size_t MemoryFacadeMaxDedicatedShardCount = 64;
	// quarantine 分片上限。
	inline constexpr std::size_t MemoryFacadeMaxQuarantineShardCount = 64;
	// facade 分片最小值。
	inline constexpr std::size_t MemoryFacadeMinShardCount = 8;
	// facade 分片按硬件线程缩放倍数。
	inline constexpr std::size_t MemoryFacadeShardScale = 2;
	// 每个 quarantine 分片的无锁环容量（需为 2 的幂）。
	inline constexpr std::size_t MemoryFacadeQuarantineRingCapacity = 1024;
	// 大对象缓存桶数量。
	inline constexpr std::size_t MemoryFacadeDedicatedCacheBucketCount = 64;
	// 每个大对象缓存桶最大缓存条目数。
	inline constexpr std::size_t MemoryFacadeDedicatedCacheBucketCapacity = 64;
	// 大对象缓存查找线性探测次数（冲突时最多探测 N 个桶）。
	inline constexpr std::size_t MemoryFacadeDedicatedCacheMaxProbe = 4;
	// dedicated 元数据节点池每次扩容的节点数。
	inline constexpr std::size_t MemoryFacadeDedicatedNodeChunkSize = 256;

	// -------------------- Internal Benchmark 参数 --------------------
	// 内部计时统计每累积多少次采样打印一次摘要。
	inline constexpr std::size_t MemoryBenchPrintEverySamples = 200000;
	// 线程分组统计桶数（按线程哈希分桶）。
	inline constexpr std::size_t MemoryBenchThreadGroupBucketCount = 64;
	// size-class 分组统计桶数（覆盖当前 56 个 class 并预留扩展）。
	inline constexpr std::size_t MemoryBenchSizeClassGroupBucketCount = 64;
	// 每个点位打印的线程分组 TopN（按调用次数）。
	inline constexpr std::size_t MemoryBenchPrintTopThreadGroups = 4;

	// -------------------- PageAllocator 参数 --------------------
	// 页分配器分片上限（与 CentralPool 对齐到 64）。
	inline constexpr std::size_t PageAllocatorMaxShardCount = 64;
	// 页分配器分片最小值。
	inline constexpr std::size_t PageAllocatorMinShardCount = 8;
	// 页分配器分片相对硬件线程数的缩放倍数。
	inline constexpr std::size_t PageAllocatorShardScale = 2;
	// decommit 采样掩码（0x7 表示约 1/8 采样）。
	inline constexpr std::uint32_t PageAllocatorDecommitSampleMask = 0x7;
	// decommit 的最小阈值字节。
	inline constexpr std::size_t PageAllocatorDecommitMinBytes = 256u * 1024u;
	// decommit 动态阈值分母：SmallRegionReserveBytes / 128。
	inline constexpr std::size_t PageAllocatorDecommitRegionDivisor = 128;
	// decommit 的最小页数阈值。
	inline constexpr std::size_t PageAllocatorDecommitMinPages = 64;
	// FreeSpanCache 桶数量（压缩映射）。
	inline constexpr std::size_t PageAllocatorCacheBucketCount = 64;
	// 进入压缩桶缓存的最大页数，超过该值进入 oversized 回退列表。
	inline constexpr std::size_t PageAllocatorCachedMaxPageCount = 1024;
}

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

enum class MemoryBenchPoint : std::uint8_t
{
	EngineAllocate = 0,
	EngineDeallocate,
	QuarantineEnqueue,
	QuarantineDrain,
	QuarantineReleaseEntry,
	DedicatedCacheAcquireHit,
	DedicatedCacheAcquireMiss,
	DedicatedCacheRecycle,
	ThreadCacheAllocate,
	ThreadCacheDeallocate,
	ThreadCacheRefillDeferred,
	ThreadCacheSpillDeferred,
	ThreadCacheDrainDeferred,
	ThreadCacheTrimToBudget,
	CentralPoolFetchBatch,
	CentralPoolReturnBatch,
	CentralSpanRegisterPages,
	CentralSpanUnregisterPages,
	PageAllocatorAcquireSpan,
	PageAllocatorReleaseSpan,
	Count
};

#if CORE_MEM_CFG_ENABLE_INTERNAL_BENCH_TIMING
struct MemoryBenchCounter
{
	std::atomic<std::uint64_t> Calls = 0;
	std::atomic<std::uint64_t> TotalNs = 0;
	std::atomic<std::uint64_t> MaxNs = 0;
};

[[nodiscard]] inline std::array<MemoryBenchCounter, static_cast<std::size_t>(MemoryBenchPoint::Count)>&
memoryBenchCounters() noexcept
{
	static std::array<MemoryBenchCounter, static_cast<std::size_t>(MemoryBenchPoint::Count)> Counters{};
	return Counters;
}

[[nodiscard]] inline std::array<
	std::array<MemoryBenchCounter, tuning::MemoryBenchThreadGroupBucketCount>,
	static_cast<std::size_t>(MemoryBenchPoint::Count)>&
memoryBenchThreadGroupCounters() noexcept
{
	static std::array<
		std::array<MemoryBenchCounter, tuning::MemoryBenchThreadGroupBucketCount>,
		static_cast<std::size_t>(MemoryBenchPoint::Count)> Counters{};
	return Counters;
}

[[nodiscard]] inline std::array<
	std::array<MemoryBenchCounter, tuning::MemoryBenchSizeClassGroupBucketCount>,
	static_cast<std::size_t>(MemoryBenchPoint::Count)>&
memoryBenchSizeClassGroupCounters() noexcept
{
	static std::array<
		std::array<MemoryBenchCounter, tuning::MemoryBenchSizeClassGroupBucketCount>,
		static_cast<std::size_t>(MemoryBenchPoint::Count)> Counters{};
	return Counters;
}

[[nodiscard]] inline std::atomic<std::uint64_t>& memoryBenchSampleCounter() noexcept
{
	static std::atomic<std::uint64_t> Counter = 0;
	return Counter;
}

[[nodiscard]] inline std::size_t memoryBenchPointIndex(const MemoryBenchPoint Point) noexcept
{
	return static_cast<std::size_t>(Point);
}

[[nodiscard]] inline std::size_t memoryBenchThreadBucketForCurrentThread() noexcept
{
	thread_local const std::size_t ThreadHash =
		static_cast<std::size_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
	return ThreadHash % tuning::MemoryBenchThreadGroupBucketCount;
}

inline void memoryBenchUpdateCounter(MemoryBenchCounter& Counter, const std::uint64_t ElapsedNs) noexcept
{
	Counter.Calls.fetch_add(1, std::memory_order_relaxed);
	Counter.TotalNs.fetch_add(ElapsedNs, std::memory_order_relaxed);

	std::uint64_t Max = Counter.MaxNs.load(std::memory_order_relaxed);
	while (Max < ElapsedNs &&
		!Counter.MaxNs.compare_exchange_weak(Max, ElapsedNs, std::memory_order_relaxed, std::memory_order_relaxed))
	{
	}
}

inline void memoryBenchPrintCounterLine(
	const char* Prefix,
	const char* Name,
	const std::uint64_t Calls,
	const std::uint64_t TotalNs,
	const std::uint64_t MaxNs) noexcept
{
	if (Calls == 0)
	{
		return;
	}

	const double AvgNs = static_cast<double>(TotalNs) / static_cast<double>(Calls);
	const double TotalMs = static_cast<double>(TotalNs) / 1000000.0;
	std::printf(
		"[MEM-BENCH] %s %-30s calls=%llu total_ms=%.3f avg_ns=%.1f max_ns=%llu\n",
		Prefix,
		Name,
		static_cast<unsigned long long>(Calls),
		TotalMs,
		AvgNs,
		static_cast<unsigned long long>(MaxNs));
}

[[nodiscard]] inline const char* memoryBenchPointName(const MemoryBenchPoint Point) noexcept
{
	switch (Point)
	{
	case MemoryBenchPoint::EngineAllocate:
		return "engine.allocate";
	case MemoryBenchPoint::EngineDeallocate:
		return "engine.deallocate";
	case MemoryBenchPoint::QuarantineEnqueue:
		return "quarantine.enqueue";
	case MemoryBenchPoint::QuarantineDrain:
		return "quarantine.drain";
	case MemoryBenchPoint::QuarantineReleaseEntry:
		return "quarantine.release_entry";
	case MemoryBenchPoint::DedicatedCacheAcquireHit:
		return "dedicated_cache.acquire_hit";
	case MemoryBenchPoint::DedicatedCacheAcquireMiss:
		return "dedicated_cache.acquire_miss";
	case MemoryBenchPoint::DedicatedCacheRecycle:
		return "dedicated_cache.recycle";
	case MemoryBenchPoint::ThreadCacheAllocate:
		return "thread_cache.allocate";
	case MemoryBenchPoint::ThreadCacheDeallocate:
		return "thread_cache.deallocate";
	case MemoryBenchPoint::ThreadCacheRefillDeferred:
		return "thread_cache.refill_deferred";
	case MemoryBenchPoint::ThreadCacheSpillDeferred:
		return "thread_cache.spill_deferred";
	case MemoryBenchPoint::ThreadCacheDrainDeferred:
		return "thread_cache.drain_deferred";
	case MemoryBenchPoint::ThreadCacheTrimToBudget:
		return "thread_cache.trim_to_budget";
	case MemoryBenchPoint::CentralPoolFetchBatch:
		return "central_pool.fetch_batch";
	case MemoryBenchPoint::CentralPoolReturnBatch:
		return "central_pool.return_batch";
	case MemoryBenchPoint::CentralSpanRegisterPages:
		return "central_pool.register_span_pages";
	case MemoryBenchPoint::CentralSpanUnregisterPages:
		return "central_pool.unregister_span_pages";
	case MemoryBenchPoint::PageAllocatorAcquireSpan:
		return "page_allocator.acquire_span";
	case MemoryBenchPoint::PageAllocatorReleaseSpan:
		return "page_allocator.release_span";
	default:
		return "unknown";
	}
}

inline void memoryBenchPrintSummary() noexcept
{
	static std::mutex PrintMutex;
	std::unique_lock<std::mutex> Lock(PrintMutex, std::try_to_lock);
	if (!Lock.owns_lock())
	{
		return;
	}

	auto& Counters = memoryBenchCounters();
	auto& ThreadCounters = memoryBenchThreadGroupCounters();
	auto& SizeClassCounters = memoryBenchSizeClassGroupCounters();
	std::printf("[MEM-BENCH] -------- allocator timing summary --------\n");
	for (std::size_t I = 0; I < Counters.size(); ++I)
	{
		const std::uint64_t Calls = Counters[I].Calls.load(std::memory_order_relaxed);
		if (Calls == 0)
		{
			continue;
		}

		const std::uint64_t TotalNs = Counters[I].TotalNs.load(std::memory_order_relaxed);
		const std::uint64_t MaxNs = Counters[I].MaxNs.load(std::memory_order_relaxed);
		memoryBenchPrintCounterLine(
			"[all]",
			memoryBenchPointName(static_cast<MemoryBenchPoint>(I)),
			Calls,
			TotalNs,
			MaxNs);

		std::array<std::size_t, tuning::MemoryBenchPrintTopThreadGroups> TopThreadBuckets{};
		TopThreadBuckets.fill(std::numeric_limits<std::size_t>::max());
		for (std::size_t Rank = 0; Rank < TopThreadBuckets.size(); ++Rank)
		{
			std::size_t BestBucket = std::numeric_limits<std::size_t>::max();
			std::uint64_t BestCalls = 0;
			for (std::size_t Bucket = 0; Bucket < tuning::MemoryBenchThreadGroupBucketCount; ++Bucket)
			{
				bool AlreadySelected = false;
				for (std::size_t Prev = 0; Prev < Rank; ++Prev)
				{
					if (TopThreadBuckets[Prev] == Bucket)
					{
						AlreadySelected = true;
						break;
					}
				}

				if (AlreadySelected)
				{
					continue;
				}

				const std::uint64_t BucketCalls =
					ThreadCounters[I][Bucket].Calls.load(std::memory_order_relaxed);
				if (BucketCalls > BestCalls)
				{
					BestCalls = BucketCalls;
					BestBucket = Bucket;
				}
			}

			if (BestBucket == std::numeric_limits<std::size_t>::max() || BestCalls == 0)
			{
				break;
			}

			TopThreadBuckets[Rank] = BestBucket;
			const auto& Counter = ThreadCounters[I][BestBucket];
			const std::uint64_t GroupCalls = Counter.Calls.load(std::memory_order_relaxed);
			const std::uint64_t GroupTotalNs = Counter.TotalNs.load(std::memory_order_relaxed);
			const std::uint64_t GroupMaxNs = Counter.MaxNs.load(std::memory_order_relaxed);
			const double GroupAvgNs = static_cast<double>(GroupTotalNs) / static_cast<double>(GroupCalls);
			std::printf(
				"[MEM-BENCH] [thread-bucket] %-30s bucket=%llu calls=%llu total_ms=%.3f avg_ns=%.1f max_ns=%llu\n",
				memoryBenchPointName(static_cast<MemoryBenchPoint>(I)),
				static_cast<unsigned long long>(BestBucket),
				static_cast<unsigned long long>(GroupCalls),
				static_cast<double>(GroupTotalNs) / 1000000.0,
				GroupAvgNs,
				static_cast<unsigned long long>(GroupMaxNs));
		}

		for (std::size_t SizeClass = 0; SizeClass < tuning::MemoryBenchSizeClassGroupBucketCount; ++SizeClass)
		{
			const auto& Counter = SizeClassCounters[I][SizeClass];
			const std::uint64_t GroupCalls = Counter.Calls.load(std::memory_order_relaxed);
			if (GroupCalls == 0)
			{
				continue;
			}

			const std::uint64_t GroupTotalNs = Counter.TotalNs.load(std::memory_order_relaxed);
			const std::uint64_t GroupMaxNs = Counter.MaxNs.load(std::memory_order_relaxed);
			const double GroupAvgNs = static_cast<double>(GroupTotalNs) / static_cast<double>(GroupCalls);
			std::printf(
				"[MEM-BENCH] [size-class] %-30s class=%llu calls=%llu total_ms=%.3f avg_ns=%.1f max_ns=%llu\n",
				memoryBenchPointName(static_cast<MemoryBenchPoint>(I)),
				static_cast<unsigned long long>(SizeClass),
				static_cast<unsigned long long>(GroupCalls),
				static_cast<double>(GroupTotalNs) / 1000000.0,
				GroupAvgNs,
				static_cast<unsigned long long>(GroupMaxNs));
		}
	}
	std::fflush(stdout);
}

inline void memoryBenchRecord(
	const MemoryBenchPoint Point,
	const std::uint64_t ElapsedNs,
	const std::uint16_t SizeClass = InvalidSizeClass) noexcept
{
	auto& Counters = memoryBenchCounters();
	auto& ThreadCounters = memoryBenchThreadGroupCounters();
	auto& SizeClassCounters = memoryBenchSizeClassGroupCounters();
	const std::size_t PointIndex = memoryBenchPointIndex(Point);

	memoryBenchUpdateCounter(Counters[PointIndex], ElapsedNs);
	memoryBenchUpdateCounter(ThreadCounters[PointIndex][memoryBenchThreadBucketForCurrentThread()], ElapsedNs);
	if (SizeClass < tuning::MemoryBenchSizeClassGroupBucketCount)
	{
		memoryBenchUpdateCounter(SizeClassCounters[PointIndex][SizeClass], ElapsedNs);
	}

	const std::uint64_t Sample = memoryBenchSampleCounter().fetch_add(1, std::memory_order_relaxed) + 1;
	if (Sample % tuning::MemoryBenchPrintEverySamples == 0)
	{
		memoryBenchPrintSummary();
	}
}

class MemoryBenchScope
{
public:
	explicit MemoryBenchScope(
		const MemoryBenchPoint Point,
		const std::uint16_t SizeClass = InvalidSizeClass) noexcept
		: Point(Point)
		, SizeClass(SizeClass)
		, StartNs(timestampNs())
	{
	}

	~MemoryBenchScope() noexcept
	{
		memoryBenchRecord(Point, timestampNs() - StartNs, SizeClass);
	}

private:
	MemoryBenchPoint Point;
	std::uint16_t SizeClass = InvalidSizeClass;
	std::uint64_t StartNs = 0;
};
#else
class MemoryBenchScope
{
public:
	constexpr explicit MemoryBenchScope(
		const MemoryBenchPoint,
		const std::uint16_t = InvalidSizeClass) noexcept
	{
	}
};

inline void memoryBenchPrintSummary() noexcept
{
}
#endif

#if CORE_MEM_CFG_ENABLE_INTERNAL_BENCH_TIMING
#define CORE_MEM_BENCH_SCOPE(PointValue) ::core::mem::MemoryBenchScope CoreMemBenchScope_##__LINE__(PointValue)
#define CORE_MEM_BENCH_SCOPE_SC(PointValue, SizeClassValue) \
	::core::mem::MemoryBenchScope CoreMemBenchScope_##__LINE__(PointValue, static_cast<std::uint16_t>(SizeClassValue))
#else
#define CORE_MEM_BENCH_SCOPE(PointValue) ((void)0)
#define CORE_MEM_BENCH_SCOPE_SC(PointValue, SizeClassValue) ((void)0)
#endif

inline void spinPause() noexcept
{
#if (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__))
	_mm_pause();
#else
	std::this_thread::yield();
#endif
}

inline void prefetchRead(const void* Ptr) noexcept
{
	if (!Ptr)
	{
		return;
	}

#if (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__))
	_mm_prefetch(reinterpret_cast<const char*>(Ptr), _MM_HINT_T0);
#elif defined(__GNUC__) || defined(__clang__)
	__builtin_prefetch(Ptr, 0, 3);
#endif
}

inline void prefetchWrite(const void* Ptr) noexcept
{
	if (!Ptr)
	{
		return;
	}

#if defined(__GNUC__) || defined(__clang__)
	__builtin_prefetch(Ptr, 1, 3);
#else
	prefetchRead(Ptr);
#endif
}

struct SpinLock
{
	void lock() noexcept
	{
		if (!Locked.test_and_set(std::memory_order_acquire))
		{
			return;
		}

		std::uint32_t pauseBudget = tuning::SpinLockInitialPauseCount;
		while (Locked.test_and_set(std::memory_order_acquire))
		{
			for (std::uint32_t i = 0; i < pauseBudget; ++i)
			{
				spinPause();
			}

			if (pauseBudget < tuning::SpinLockMaxPauseCount)
			{
				pauseBudget <<= 1;
			}
			else
			{
				std::this_thread::yield();
			}
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
	std::thread::id ThreadId{};
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
	std::thread::id ThreadId{};
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