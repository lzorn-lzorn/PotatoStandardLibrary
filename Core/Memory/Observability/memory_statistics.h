#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../memory_common.h"

namespace core::mem
{

struct MemoryStatisticsSnapshot
{
	std::uint64_t AllocateCount = 0;
	std::uint64_t DeallocateCount = 0;
	std::uint64_t ThreadCacheHitCount = 0;
	std::uint64_t ThreadCacheMissCount = 0;
	std::uint64_t CentralFetchCount = 0;
	std::uint64_t CentralReturnCount = 0;
	std::uint64_t OsAllocateCount = 0;
	std::uint64_t OsReleaseCount = 0;
	std::uint64_t GuardCorruptionCount = 0;
	std::uint64_t UseAfterFreeCount = 0;
	std::size_t CurrentBytes = 0;
	std::size_t PeakBytes = 0;
};

struct AllocatorRuntimeStatsSnapshot
{
	std::size_t DedicatedAllocationCount = 0;
	std::size_t DedicatedCacheBucketCount = 0;
	std::size_t DedicatedCacheBytes = 0;
	std::size_t QuarantineEntryCount = 0;
	std::size_t QuarantineBytes = 0;

	std::size_t PageToSpanEntryCount = 0;
	std::size_t PageToSpanLevel1NodesUsed = 0;
	std::size_t PageToSpanLevel1NodesCapacity = 0;
	double PageToSpanLevel1UsageRatio = 0.0;
	std::size_t PageToSpanLevel2NodesUsed = 0;
	std::size_t PageToSpanLevel2NodesCapacity = 0;
	double PageToSpanLevel2UsageRatio = 0.0;
	std::size_t PageToSpanPoolExhaustionWarningCount = 0;

	std::size_t SpanObjectPoolAllocated = 0;
	std::size_t SpanObjectPoolInUse = 0;
	double SpanObjectPoolUsageRatio = 0.0;

	std::size_t RegionIndexRegionCount = 0;
	std::size_t RegionIndexEntryCount = 0;
	std::size_t RegionIndexLevel1NodesUsed = 0;
	std::size_t RegionIndexLevel1NodesCapacity = 0;
	double RegionIndexLevel1UsageRatio = 0.0;
	std::size_t RegionIndexLevel2NodesUsed = 0;
	std::size_t RegionIndexLevel2NodesCapacity = 0;
	double RegionIndexLevel2UsageRatio = 0.0;
	std::size_t RegionIndexPoolExhaustionWarningCount = 0;
};

struct AllocatorRuntimeStatsInputs
{
	std::size_t DedicatedAllocationCount = 0;
	std::size_t DedicatedCacheBucketCount = 0;
	std::size_t DedicatedCacheBytes = 0;
	std::size_t QuarantineEntryCount = 0;
	std::size_t QuarantineBytes = 0;

	std::size_t PageToSpanEntryCount = 0;
	std::size_t PageToSpanLevel1NodesUsed = 0;
	std::size_t PageToSpanLevel1NodesCapacity = 0;
	double PageToSpanLevel1UsageRatio = 0.0;
	std::size_t PageToSpanLevel2NodesUsed = 0;
	std::size_t PageToSpanLevel2NodesCapacity = 0;
	double PageToSpanLevel2UsageRatio = 0.0;
	std::size_t PageToSpanPoolExhaustionWarningCount = 0;

	std::size_t SpanObjectPoolAllocated = 0;
	std::size_t SpanObjectPoolInUse = 0;
	double SpanObjectPoolUsageRatio = 0.0;

	std::size_t RegionIndexRegionCount = 0;
	std::size_t RegionIndexEntryCount = 0;
	std::size_t RegionIndexLevel1NodesUsed = 0;
	std::size_t RegionIndexLevel1NodesCapacity = 0;
	double RegionIndexLevel1UsageRatio = 0.0;
	std::size_t RegionIndexLevel2NodesUsed = 0;
	std::size_t RegionIndexLevel2NodesCapacity = 0;
	double RegionIndexLevel2UsageRatio = 0.0;
	std::size_t RegionIndexPoolExhaustionWarningCount = 0;
};

class AllocatorRuntimeStatsCollector
{
public:
	[[nodiscard]] static AllocatorRuntimeStatsSnapshot collect(const AllocatorRuntimeStatsInputs& Inputs) noexcept
	{
		AllocatorRuntimeStatsSnapshot Snapshot;
		Snapshot.DedicatedAllocationCount = Inputs.DedicatedAllocationCount;
		Snapshot.DedicatedCacheBucketCount = Inputs.DedicatedCacheBucketCount;
		Snapshot.DedicatedCacheBytes = Inputs.DedicatedCacheBytes;
		Snapshot.QuarantineEntryCount = Inputs.QuarantineEntryCount;
		Snapshot.QuarantineBytes = Inputs.QuarantineBytes;

		Snapshot.PageToSpanEntryCount = Inputs.PageToSpanEntryCount;
		Snapshot.PageToSpanLevel1NodesUsed = Inputs.PageToSpanLevel1NodesUsed;
		Snapshot.PageToSpanLevel1NodesCapacity = Inputs.PageToSpanLevel1NodesCapacity;
		Snapshot.PageToSpanLevel1UsageRatio = Inputs.PageToSpanLevel1UsageRatio;
		Snapshot.PageToSpanLevel2NodesUsed = Inputs.PageToSpanLevel2NodesUsed;
		Snapshot.PageToSpanLevel2NodesCapacity = Inputs.PageToSpanLevel2NodesCapacity;
		Snapshot.PageToSpanLevel2UsageRatio = Inputs.PageToSpanLevel2UsageRatio;
		Snapshot.PageToSpanPoolExhaustionWarningCount = Inputs.PageToSpanPoolExhaustionWarningCount;

		Snapshot.SpanObjectPoolAllocated = Inputs.SpanObjectPoolAllocated;
		Snapshot.SpanObjectPoolInUse = Inputs.SpanObjectPoolInUse;
		Snapshot.SpanObjectPoolUsageRatio = Inputs.SpanObjectPoolUsageRatio;

		Snapshot.RegionIndexRegionCount = Inputs.RegionIndexRegionCount;
		Snapshot.RegionIndexEntryCount = Inputs.RegionIndexEntryCount;
		Snapshot.RegionIndexLevel1NodesUsed = Inputs.RegionIndexLevel1NodesUsed;
		Snapshot.RegionIndexLevel1NodesCapacity = Inputs.RegionIndexLevel1NodesCapacity;
		Snapshot.RegionIndexLevel1UsageRatio = Inputs.RegionIndexLevel1UsageRatio;
		Snapshot.RegionIndexLevel2NodesUsed = Inputs.RegionIndexLevel2NodesUsed;
		Snapshot.RegionIndexLevel2NodesCapacity = Inputs.RegionIndexLevel2NodesCapacity;
		Snapshot.RegionIndexLevel2UsageRatio = Inputs.RegionIndexLevel2UsageRatio;
		Snapshot.RegionIndexPoolExhaustionWarningCount = Inputs.RegionIndexPoolExhaustionWarningCount;
		return Snapshot;
	}
};

class MemoryStatisticsObserver
{
public:
	/**
	 * @brief Consume one memory event and update aggregated counters.
	 * @param
	 *     - event  const MemoryEvent&  Event emitted by allocator pipeline.
	 * @usage
	 *     - Bind this observer to AllocatorFacade event bus for runtime metrics.
	 * @return
	 *     - void
	 */
	void onMemoryEvent(const MemoryEvent& event) noexcept
	{
		switch (event.Type)
		{
		case MemoryEventType::Allocate:
			AllocateCount.fetch_add(1, std::memory_order_relaxed);
			CurrentBytes.fetch_add(event.Size, std::memory_order_relaxed);
			updatePeak();
			if (event.FromOS)
			{
				OsAllocateCount.fetch_add(1, std::memory_order_relaxed);
			}
			break;

		case MemoryEventType::Deallocate:
			DeallocateCount.fetch_add(1, std::memory_order_relaxed);
			CurrentBytes.fetch_sub(event.Size, std::memory_order_relaxed);
			if (event.FromOS)
			{
				OsReleaseCount.fetch_add(1, std::memory_order_relaxed);
			}
			break;

		case MemoryEventType::ThreadCacheHit:
			ThreadCacheHitCount.fetch_add(1, std::memory_order_relaxed);
			break;

		case MemoryEventType::ThreadCacheMiss:
			ThreadCacheMissCount.fetch_add(1, std::memory_order_relaxed);
			break;

		case MemoryEventType::CentralFetch:
			CentralFetchCount.fetch_add(1, std::memory_order_relaxed);
			break;

		case MemoryEventType::CentralReturn:
			CentralReturnCount.fetch_add(1, std::memory_order_relaxed);
			break;

		case MemoryEventType::GuardCorruption:
			GuardCorruptionCount.fetch_add(1, std::memory_order_relaxed);
			break;

		case MemoryEventType::UseAfterFree:
			UseAfterFreeCount.fetch_add(1, std::memory_order_relaxed);
			break;

		default:
			break;
		}
	}

	/**
	 * @brief Build an immutable statistics snapshot.
	 * @param
	 *     - none
	 * @usage
	 *     - Poll this periodically for HUD/telemetry output.
	 * @return
	 *     - MemoryStatisticsSnapshot  Current accumulated counters.
	 */
	[[nodiscard]] MemoryStatisticsSnapshot getSnapshot() const noexcept
	{
		MemoryStatisticsSnapshot Value;
		Value.AllocateCount = AllocateCount.load(std::memory_order_relaxed);
		Value.DeallocateCount = DeallocateCount.load(std::memory_order_relaxed);
		Value.ThreadCacheHitCount = ThreadCacheHitCount.load(std::memory_order_relaxed);
		Value.ThreadCacheMissCount = ThreadCacheMissCount.load(std::memory_order_relaxed);
		Value.CentralFetchCount = CentralFetchCount.load(std::memory_order_relaxed);
		Value.CentralReturnCount = CentralReturnCount.load(std::memory_order_relaxed);
		Value.OsAllocateCount = OsAllocateCount.load(std::memory_order_relaxed);
		Value.OsReleaseCount = OsReleaseCount.load(std::memory_order_relaxed);
		Value.GuardCorruptionCount = GuardCorruptionCount.load(std::memory_order_relaxed);
		Value.UseAfterFreeCount = UseAfterFreeCount.load(std::memory_order_relaxed);
		Value.CurrentBytes = CurrentBytes.load(std::memory_order_relaxed);
		Value.PeakBytes = PeakBytes.load(std::memory_order_relaxed);
		return Value;
	}

private:
	void updatePeak() noexcept
	{
		std::size_t Current = CurrentBytes.load(std::memory_order_relaxed);
		std::size_t peak = PeakBytes.load(std::memory_order_relaxed);
		while (Current > peak && !PeakBytes.compare_exchange_weak(peak, Current, std::memory_order_relaxed))
		{
		}
	}

	std::atomic<std::uint64_t> AllocateCount = 0;
	std::atomic<std::uint64_t> DeallocateCount = 0;
	std::atomic<std::uint64_t> ThreadCacheHitCount = 0;
	std::atomic<std::uint64_t> ThreadCacheMissCount = 0;
	std::atomic<std::uint64_t> CentralFetchCount = 0;
	std::atomic<std::uint64_t> CentralReturnCount = 0;
	std::atomic<std::uint64_t> OsAllocateCount = 0;
	std::atomic<std::uint64_t> OsReleaseCount = 0;
	std::atomic<std::uint64_t> GuardCorruptionCount = 0;
	std::atomic<std::uint64_t> UseAfterFreeCount = 0;
	std::atomic<std::size_t> CurrentBytes = 0;
	std::atomic<std::size_t> PeakBytes = 0;
};

} // namespace core::mem
