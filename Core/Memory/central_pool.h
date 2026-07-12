#pragma once

#include <atomic>
#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../Containers/radix_tree.h"

#include "memory_common.h"
#include "virtual_memory_backend.h"

namespace core::mem
{

inline constexpr std::array<std::uint32_t, 56> SizeClasses = {
	8, 16, 24, 32, 40, 48, 56, 64,
	80, 96, 112, 128, 160, 192, 224, 256,
	320, 384, 448, 512, 640, 768, 896, 1024,
	1280, 1536, 1792, 2048, 2560, 3072, 3584, 4096,
	5120, 6144, 7168, 8192, 10240, 12288, 14336, 16384,
	20480, 24576, 28672, 32768, 40960, 49152, 57344, 65536,
	81920, 98304, 114688, 131072, 163840, 196608, 229376, 262144
};

inline constexpr std::size_t NumSizeClasses = SizeClasses.size();
inline constexpr std::size_t MaxSmallAllocation = SizeClasses.back();
inline constexpr std::size_t ClassQuantum = 8;
inline constexpr std::size_t ClassLookupCount = (MaxSmallAllocation / ClassQuantum) + 1;
inline constexpr std::size_t MaxRetainedEmptySpansPerClass = 2;
inline constexpr std::size_t CentralPoolBucketShardCount = 16;
inline constexpr std::size_t MaxReturnBatchGroups = 8;

inline std::array<std::uint16_t, ClassLookupCount> buildSizeClassLookup()
{
	std::array<std::uint16_t, ClassLookupCount> lookup{};

	for (std::size_t Bucket = 0; Bucket < lookup.size(); ++Bucket)
	{
		const std::size_t size = Bucket * ClassQuantum;
		std::uint16_t class_index = 0;

		while (class_index + 1 < NumSizeClasses && SizeClasses[class_index] < size)
		{
			++class_index;
		}

		lookup[Bucket] = class_index;
	}

	return lookup;
}

inline const std::array<std::uint16_t, ClassLookupCount> SizeClassLookup = buildSizeClassLookup();

[[nodiscard]] inline std::uint16_t sizeToClassIndex(const std::size_t size) noexcept
{
	if (size == 0 || size > MaxSmallAllocation)
	{
		return InvalidSizeClass;
	}

	const std::size_t Bucket = (size + ClassQuantum - 1) / ClassQuantum;
	return SizeClassLookup[Bucket];
}

[[nodiscard]] inline std::size_t classIndexToSize(const std::uint16_t ClassIndex) noexcept
{
	if (ClassIndex >= NumSizeClasses)
	{
		return 0;
	}

	return alignUp(static_cast<std::size_t>(SizeClasses[ClassIndex]), alignof(std::max_align_t));
}

struct SizeClass
{
	std::uint32_t BlockSize = 0;
	std::uint16_t BlocksPerSpan = 0;
	std::uint16_t PagesPerSpan = 0;
};

struct Span
{
	Span* Next = nullptr;
	Span* Prev = nullptr;
	Span* PoolNext = nullptr;

	std::uint32_t BlockSize = 0;
	std::uint16_t TotalBlocks = 0;
	std::uint16_t FreeBlocks = 0;
	std::uint16_t SizeClassIndex = InvalidSizeClass;
	std::uint16_t BucketShardIndex = 0;

	void* FreeList = nullptr;
	std::byte* SpanBase = nullptr;
	PageSpan BackingSpan{};

	bool IsInPartialList = false;
	bool IsInEmptyList = false;
	bool IsReleased = false;
};

class CentralPool
{
public:
	struct PageToSpanPoolStats
	{
		std::size_t EntryCount = 0;
		std::size_t Level1NodesUsed = 0;
		std::size_t Level1NodesCapacity = 0;
		std::size_t Level2NodesUsed = 0;
		std::size_t Level2NodesCapacity = 0;
		double Level1UsageRatio = 0.0;
		double Level2UsageRatio = 0.0;
		std::size_t PoolExhaustionWarningCount = 0;

		std::size_t SpanObjectsAllocated = 0;
		std::size_t SpanObjectsInUse = 0;
		double SpanObjectPoolUsageRatio = 0.0;
	};

	explicit CentralPool(PageAllocator& pageAllocator)
		: PageAllocator(&pageAllocator)
		, PageSize(pageAllocator.getPageSize())
	{
		for (std::size_t i = 0; i < NumSizeClasses; ++i)
		{
			Classes[i] = buildSizeClass(pageAllocator.getPageSize(), SizeClasses[i]);
		}

		SpanLookupSnapshotState.store(
			std::make_shared<const SpanLookupSnapshot>(),
			std::memory_order_release);
	}

	CentralPool(const CentralPool&) = delete;
	CentralPool& operator=(const CentralPool&) = delete;

	/**
	 * @brief Fetch a Block batch from one size class into thread-local cache.
	 * @param
	 *     - SizeClassIndex  std::uint16_t  Target size-class index.
	 *     - OutBlocks       void**         Caller-provided output Block array.
	 *     - MaxBlocks       std::uint32_t  Maximum Blocks to fetch.
	 * @usage
	 *     - Called on ThreadCache miss to amortize synchronization cost.
	 * @return
	 *     - std::uint32_t  Number of Blocks returned.
	 */
	[[nodiscard]] std::uint32_t fetchBatch(
		const std::uint16_t SizeClassIndex,
		void** OutBlocks,
		const std::uint32_t MaxBlocks)
	{
		if (SizeClassIndex >= NumSizeClasses || !OutBlocks || MaxBlocks == 0)
		{
			return 0;
		}

		SizeClassBucket& Bucket = Buckets[SizeClassIndex];
		const std::size_t shardIndex = getThreadShardIndex();
		SizeClassBucketShard& Shard = Bucket.Shards[shardIndex];
		std::lock_guard<SpinLock> Lock(Shard.Lock);

		Span* SpanObj = Shard.PartialSpans;
		if (!SpanObj)
		{
			SpanObj = Shard.EmptySpans;
		}

		if (!SpanObj)
		{
			SpanObj = allocateSpanForClass(
				SizeClassIndex,
				static_cast<std::uint16_t>(shardIndex),
				Shard);
			if (!SpanObj)
			{
				return 0;
			}
		}

		if (SpanObj->IsInEmptyList)
		{
			removeSpan(Shard.EmptySpans, SpanObj);
			SpanObj->IsInEmptyList = false;
			if (Shard.EmptySpanCount > 0)
			{
				--Shard.EmptySpanCount;
			}
		}

		if (!SpanObj->IsInPartialList)
		{
			insertSpan(Shard.PartialSpans, SpanObj);
			SpanObj->IsInPartialList = true;
		}

		std::uint32_t allocated = 0;
		while (allocated < MaxBlocks && SpanObj->FreeList)
		{
			OutBlocks[allocated] = popBlock(SpanObj->FreeList);
			++allocated;
			--SpanObj->FreeBlocks;
		}

		if (SpanObj->FreeBlocks == 0 && SpanObj->IsInPartialList)
		{
			removeSpan(Shard.PartialSpans, SpanObj);
			SpanObj->IsInPartialList = false;
		}

		return allocated;
	}

	/**
	 * @brief Return a linked Block batch back to one central size class.
	 * @param
	 *     - SizeClassIndex  std::uint16_t  Target size-class index.
	 *     - Head            void*          Intrusive single-link Head pointer.
	 *     - count           std::uint32_t  Number of Blocks in chain.
	 * @usage
	 *     - Called by ThreadCache when local freelist is over target capacity.
	 * @return
	 *     - void
	 */
	void returnBatch(const std::uint16_t SizeClassIndex, void* Head, const std::uint32_t count)
	{
		if (SizeClassIndex >= NumSizeClasses || !Head || count == 0)
		{
			return;
		}

		SizeClassBucket& Bucket = Buckets[SizeClassIndex];

		struct SpanBatch
		{
			Span* SpanObj = nullptr;
			void* Head = nullptr;
			void* Tail = nullptr;
			std::uint32_t Count = 0;
			std::uint16_t ShardIndex = 0;
		};

		std::array<SpanBatch, MaxReturnBatchGroups> batches{};
		std::size_t batchCount = 0;

		auto findBatch = [&](Span* SpanObj) -> SpanBatch*
		{
			for (std::size_t i = 0; i < batchCount; ++i)
			{
				if (batches[i].SpanObj == SpanObj)
				{
					return &batches[i];
				}
			}

			return nullptr;
		};

		auto flushSingleBlock = [&](Span* SpanObj, void* Block) {
			if (!SpanObj || !Block)
			{
				return;
			}

			SizeClassBucketShard& Shard = Bucket.Shards[normalizeShardIndex(SpanObj->BucketShardIndex)];
			std::lock_guard<SpinLock> Lock(Shard.Lock);
			applyReturnedBatchLocked(Shard, *SpanObj, Block, Block, 1);
			trimEmptySpansLocked(Shard);
		};

		void* current = Head;
		Span* spanHint = nullptr;
		for (std::uint32_t i = 0; i < count && current; ++i)
		{
			void* next = *reinterpret_cast<void**>(current);
			Span* SpanObj = findSpanHinted(current, spanHint, SizeClassIndex);
			if (!SpanObj || SpanObj->IsReleased)
			{
				current = next;
				continue;
			}

			SpanBatch* batch = findBatch(SpanObj);
			if (!batch)
			{
				if (batchCount < batches.size())
				{
					batch = &batches[batchCount++];
					batch->SpanObj = SpanObj;
					batch->ShardIndex = SpanObj->BucketShardIndex;
				}
				else
				{
					flushSingleBlock(SpanObj, current);
					spanHint = SpanObj;
					current = next;
					continue;
				}
			}

			*reinterpret_cast<void**>(current) = batch->Head;
			batch->Head = current;
			if (!batch->Tail)
			{
				batch->Tail = current;
			}
			++batch->Count;
			spanHint = SpanObj;

			current = next;
		}

		for (std::size_t i = 0; i < batchCount; ++i)
		{
			SpanBatch& batch = batches[i];
			if (!batch.SpanObj || batch.Count == 0)
			{
				continue;
			}

			SizeClassBucketShard& Shard = Bucket.Shards[normalizeShardIndex(batch.ShardIndex)];
			std::lock_guard<SpinLock> Lock(Shard.Lock);
			applyReturnedBatchLocked(Shard, *batch.SpanObj, batch.Head, batch.Tail, batch.Count);
			trimEmptySpansLocked(Shard);
		}
	}

	/**
	 * @brief Read immutable size-class metadata.
	 * @param
	 *     - ClassIndex  std::uint16_t  Size-class index.
	 * @usage
	 *     - Used by ThreadCache and allocator layout stage.
	 * @return
	 *     - SizeClass  Metadata for this class or zero-initialized fallback.
	 */
	[[nodiscard]] SizeClass getSizeClassInfo(const std::uint16_t ClassIndex) const noexcept
	{
		return ClassIndex < NumSizeClasses ? Classes[ClassIndex] : SizeClass{};
	}

	[[nodiscard]] PageToSpanPoolStats getPageToSpanPoolStats()
	{
		PageToSpanPoolStats stats;

		{
			std::lock_guard lock(SpanMapMutex);
			stats.EntryCount = PageToSpan.size();
			stats.Level1NodesUsed = PageToSpan.level1NodesUsed();
			stats.Level1NodesCapacity = PageToSpan.level1NodesCapacity();
			stats.Level2NodesUsed = PageToSpan.level2NodesUsed();
			stats.Level2NodesCapacity = PageToSpan.level2NodesCapacity();
			stats.PoolExhaustionWarningCount = PageToSpan.poolExhaustionFailureCount();
		}

		{
			std::lock_guard storageLock(SpanStorageMutex);
			stats.SpanObjectsAllocated = SpanPoolAllocatedCount;
			stats.SpanObjectsInUse = SpanPoolAllocatedCount >= SpanPoolFreeCount
				? SpanPoolAllocatedCount - SpanPoolFreeCount
				: 0;
		}

		stats.Level1UsageRatio = toUsageRatio(stats.Level1NodesUsed, stats.Level1NodesCapacity);
		stats.Level2UsageRatio = toUsageRatio(stats.Level2NodesUsed, stats.Level2NodesCapacity);
		stats.SpanObjectPoolUsageRatio = toUsageRatio(stats.SpanObjectsInUse, stats.SpanObjectsAllocated);
		return stats;
	}

private:
	static constexpr std::size_t SpanPoolChunkSize = 256;

	struct SizeClassBucketShard
	{
		SpinLock Lock;
		Span* PartialSpans = nullptr;
		Span* EmptySpans = nullptr;
		std::size_t EmptySpanCount = 0;
	};

	struct SizeClassBucket
	{
		std::array<SizeClassBucketShard, CentralPoolBucketShardCount> Shards{};
	};

	[[nodiscard]] static std::size_t getThreadShardIndex() noexcept
	{
		const std::size_t hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
		return hash % CentralPoolBucketShardCount;
	}

	[[nodiscard]] static std::size_t normalizeShardIndex(const std::uint16_t ShardIndex) noexcept
	{
		return static_cast<std::size_t>(ShardIndex) % CentralPoolBucketShardCount;
	}

	[[nodiscard]] static double toUsageRatio(const std::size_t used, const std::size_t capacity) noexcept
	{
		if (capacity == 0)
		{
			return 0.0;
		}

		return static_cast<double>(used) / static_cast<double>(capacity);
	}

	[[nodiscard]] static SizeClass buildSizeClass(const std::size_t PageSize, const std::uint32_t BlockSize) noexcept
	{
		const std::uint32_t AlignedBlockSize =
			static_cast<std::uint32_t>(alignUp(static_cast<std::size_t>(BlockSize), alignof(std::max_align_t)));

		const std::size_t TargetBytes = AlignedBlockSize <= 4096 ? 128u * 1024u : 256u * 1024u;
		std::size_t Blocks = std::max<std::size_t>(32, TargetBytes / AlignedBlockSize);

		std::size_t SpanBytes = alignUp(Blocks * static_cast<std::size_t>(AlignedBlockSize), PageSize);
		std::size_t Pages = std::max<std::size_t>(1, SpanBytes / PageSize);
		SpanBytes = Pages * PageSize;
		Blocks = SpanBytes / AlignedBlockSize;

		if (Blocks > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()))
		{
			Blocks = std::numeric_limits<std::uint16_t>::max();
		}
		if (Pages > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()))
		{
			Pages = std::numeric_limits<std::uint16_t>::max();
		}

		SizeClass Info;
		Info.BlockSize = AlignedBlockSize;
		Info.BlocksPerSpan = static_cast<std::uint16_t>(Blocks);
		Info.PagesPerSpan = static_cast<std::uint16_t>(Pages);
		return Info;
	}

	[[nodiscard]] Span* allocateSpanForClass(
		const std::uint16_t ClassIndex,
		const std::uint16_t ShardIndex,
		SizeClassBucketShard& Shard)
	{
		const SizeClass Info = Classes[ClassIndex];
		PageSpan PageSpan = PageAllocator->acquireSpan(Info.PagesPerSpan);
		if (!PageSpan.isValid())
		{
			return nullptr;
		}

		Span* SpanRawPtr = acquireSpanObject(ClassIndex);
		if (!SpanRawPtr)
		{
			PageAllocator->releaseSpan(PageSpan);
			return nullptr;
		}

		SpanRawPtr->BlockSize = Info.BlockSize;
		SpanRawPtr->TotalBlocks = Info.BlocksPerSpan;
		SpanRawPtr->FreeBlocks = Info.BlocksPerSpan;
		SpanRawPtr->SizeClassIndex = ClassIndex;
		SpanRawPtr->BucketShardIndex = ShardIndex;
		SpanRawPtr->SpanBase = PageSpan.Base;
		SpanRawPtr->BackingSpan = PageSpan;
		SpanRawPtr->IsReleased = false;

		std::byte* Block = SpanRawPtr->SpanBase;
		for (std::uint16_t i = 0; i < SpanRawPtr->TotalBlocks; ++i)
		{
			pushBlock(SpanRawPtr->FreeList, Block);
			Block += SpanRawPtr->BlockSize;
		}

		if (!registerSpanPages(*SpanRawPtr))
		{
			PageAllocator->releaseSpan(SpanRawPtr->BackingSpan);
			releaseSpanObject(SpanRawPtr);
			return nullptr;
		}
		insertSpan(Shard.EmptySpans, SpanRawPtr);
		SpanRawPtr->IsInEmptyList = true;
		++Shard.EmptySpanCount;

		return SpanRawPtr;
	}

	void releaseEmptySpan(SizeClassBucketShard& Shard, Span* InSpan)
	{
		if (!InSpan || InSpan->IsReleased)
		{
			return;
		}

		if (InSpan->IsInPartialList)
		{
			removeSpan(Shard.PartialSpans, InSpan);
			InSpan->IsInPartialList = false;
		}

		if (InSpan->IsInEmptyList)
		{
			removeSpan(Shard.EmptySpans, InSpan);
			InSpan->IsInEmptyList = false;
			if (Shard.EmptySpanCount > 0)
			{
				--Shard.EmptySpanCount;
			}
		}

		unregisterSpanPages(*InSpan);
		PageAllocator->releaseSpan(InSpan->BackingSpan);

		InSpan->IsReleased = true;
		releaseSpanObject(InSpan);
	}

	void trimEmptySpansLocked(SizeClassBucketShard& Shard)
	{
		while (Shard.EmptySpanCount > MaxRetainedEmptySpansPerClass)
		{
			Span* SpanObj = Shard.EmptySpans;
			if (!SpanObj)
			{
				break;
			}

			releaseEmptySpan(Shard, SpanObj);
		}
	}

	[[nodiscard]] Span* acquireSpanObject(const std::uint16_t ClassIndex)
	{
		std::lock_guard storageLock(SpanStorageMutex);

		Span*& ClassFreeList = SpanClassFreeLists[ClassIndex];
		Span* span = nullptr;
		if (ClassFreeList)
		{
			span = ClassFreeList;
			ClassFreeList = span->PoolNext;
		}
		else
		{
			if (!SpanFreeList && !growSpanPoolLocked())
			{
				return nullptr;
			}

			span = SpanFreeList;
			SpanFreeList = span->PoolNext;
		}

		if (!span)
		{
			return nullptr;
		}

		span->PoolNext = nullptr;
		if (SpanPoolFreeCount > 0)
		{
			--SpanPoolFreeCount;
		}

		*span = {};
		return span;
	}

	void releaseSpanObject(Span* InSpan)
	{
		if (!InSpan)
		{
			return;
		}

		std::lock_guard storageLock(SpanStorageMutex);
		const std::uint16_t ClassIndex = InSpan->SizeClassIndex;
		*InSpan = {};

		if (ClassIndex < NumSizeClasses)
		{
			InSpan->PoolNext = SpanClassFreeLists[ClassIndex];
			SpanClassFreeLists[ClassIndex] = InSpan;
		}
		else
		{
			InSpan->PoolNext = SpanFreeList;
			SpanFreeList = InSpan;
		}

		++SpanPoolFreeCount;
	}

	[[nodiscard]] bool growSpanPoolLocked()
	{
		auto NewChunk = std::unique_ptr<Span[]>(new (std::nothrow) Span[SpanPoolChunkSize]);
		if (!NewChunk)
		{
			return false;
		}

		Span* chunkBase = NewChunk.get();
		for (std::size_t i = 0; i < SpanPoolChunkSize; ++i)
		{
			chunkBase[i] = {};
			chunkBase[i].PoolNext = SpanFreeList;
			SpanFreeList = &chunkBase[i];
		}

		SpanPoolChunks.push_back(std::move(NewChunk));
		SpanPoolAllocatedCount += SpanPoolChunkSize;
		SpanPoolFreeCount += SpanPoolChunkSize;
		return true;
	}

	[[nodiscard]] bool registerSpanPages(Span& InSpan)
	{
		if (!InSpan.BackingSpan.isValid())
		{
			return false;
		}

		std::lock_guard Lock(SpanMapMutex);
		auto CurrentSnapshot = SpanLookupSnapshotState.load(std::memory_order_acquire);
		auto NextSnapshot = std::make_shared<SpanLookupSnapshot>(
			CurrentSnapshot ? *CurrentSnapshot : SpanLookupSnapshot{});

		const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(InSpan.SpanBase);
		for (std::size_t i = 0; i < InSpan.BackingSpan.PageCount; ++i)
		{
			const std::uint64_t pageBase = static_cast<std::uint64_t>(base + i * PageSize);
			if (!PageToSpan.insertOrAssign(pageBase, &InSpan))
			{
				for (std::size_t rollback = 0; rollback < i; ++rollback)
				{
					PageToSpan.erase(static_cast<std::uint64_t>(base + rollback * PageSize));
				}
				return false;
			}

			(*NextSnapshot)[pageBase] = &InSpan;
		}

		SpanLookupSnapshotState.store(NextSnapshot, std::memory_order_release);

		return true;
	}

	void unregisterSpanPages(const Span& InSpan)
	{
		if (!InSpan.BackingSpan.isValid())
		{
			return;
		}

		std::lock_guard Lock(SpanMapMutex);
		auto CurrentSnapshot = SpanLookupSnapshotState.load(std::memory_order_acquire);
		auto NextSnapshot = std::make_shared<SpanLookupSnapshot>(
			CurrentSnapshot ? *CurrentSnapshot : SpanLookupSnapshot{});

		const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(InSpan.SpanBase);
		for (std::size_t i = 0; i < InSpan.BackingSpan.PageCount; ++i)
		{
			const std::uint64_t pageBase = static_cast<std::uint64_t>(base + i * PageSize);
			PageToSpan.erase(pageBase);
			NextSnapshot->erase(pageBase);
		}

		SpanLookupSnapshotState.store(NextSnapshot, std::memory_order_release);
	}

	[[nodiscard]] Span* findSpanSnapshot(void* Ptr) const
	{
		const std::uintptr_t Address = reinterpret_cast<std::uintptr_t>(Ptr);
		const std::uint64_t PageBase = static_cast<std::uint64_t>(Address & ~(PageSize - 1));

		auto Snapshot = SpanLookupSnapshotState.load(std::memory_order_acquire);
		if (!Snapshot)
		{
			return nullptr;
		}

		auto Iter = Snapshot->find(PageBase);
		if (Iter == Snapshot->end())
		{
			return nullptr;
		}

		return Iter->second;
	}

	[[nodiscard]] static bool isBlockInSpan(const Span& InSpan, const void* Ptr) noexcept
	{
		if (!InSpan.SpanBase || InSpan.BlockSize == 0 || InSpan.TotalBlocks == 0)
		{
			return false;
		}

		const std::uintptr_t Address = reinterpret_cast<std::uintptr_t>(Ptr);
		const std::uintptr_t Begin = reinterpret_cast<std::uintptr_t>(InSpan.SpanBase);
		const std::uintptr_t End = Begin +
			static_cast<std::uintptr_t>(InSpan.BlockSize) * static_cast<std::uintptr_t>(InSpan.TotalBlocks);
		return Address >= Begin && Address < End;
	}

	[[nodiscard]] Span* findSpanHinted(
		void* Ptr,
		Span* Hint,
		const std::uint16_t ExpectedSizeClass) const
	{
		if (Hint &&
			!Hint->IsReleased &&
			Hint->SizeClassIndex == ExpectedSizeClass &&
			isBlockInSpan(*Hint, Ptr))
		{
			return Hint;
		}

		Span* SpanObj = findSpanSnapshot(Ptr);
		if (!SpanObj)
		{
			return nullptr;
		}

		if (SpanObj->IsReleased || SpanObj->SizeClassIndex != ExpectedSizeClass)
		{
			return nullptr;
		}

		return isBlockInSpan(*SpanObj, Ptr) ? SpanObj : nullptr;
	}

	void applyReturnedBatchLocked(
		SizeClassBucketShard& Shard,
		Span& InSpan,
		void* BatchHead,
		void* BatchTail,
		const std::uint32_t BatchCount)
	{
		if (BatchCount == 0 || !BatchHead || !BatchTail || InSpan.IsReleased)
		{
			return;
		}

		const bool WasFull = InSpan.FreeBlocks == 0;
		*reinterpret_cast<void**>(BatchTail) = InSpan.FreeList;
		InSpan.FreeList = BatchHead;

		const std::uint32_t nextFreeBlocks = std::min<std::uint32_t>(
			static_cast<std::uint32_t>(InSpan.TotalBlocks),
			static_cast<std::uint32_t>(InSpan.FreeBlocks) + BatchCount);
		InSpan.FreeBlocks = static_cast<std::uint16_t>(nextFreeBlocks);

		if (WasFull)
		{
			insertSpan(Shard.PartialSpans, &InSpan);
			InSpan.IsInPartialList = true;
		}

		if (InSpan.FreeBlocks == InSpan.TotalBlocks)
		{
			if (InSpan.IsInPartialList)
			{
				removeSpan(Shard.PartialSpans, &InSpan);
				InSpan.IsInPartialList = false;
			}

			if (!InSpan.IsInEmptyList)
			{
				insertSpan(Shard.EmptySpans, &InSpan);
				InSpan.IsInEmptyList = true;
				++Shard.EmptySpanCount;
			}
		}
	}

	static void insertSpan(Span*& Head, Span* InSpan)
	{
		InSpan->Prev = nullptr;
		InSpan->Next = Head;
		if (Head)
		{
			Head->Prev = InSpan;
		}
		Head = InSpan;
	}

	static void removeSpan(Span*& Head, Span* InSpan)
	{
		if (InSpan->Prev)
		{
			InSpan->Prev->Next = InSpan->Next;
		}
		else
		{
			Head = InSpan->Next;
		}

		if (InSpan->Next)
		{
			InSpan->Next->Prev = InSpan->Prev;
		}

		InSpan->Next = nullptr;
		InSpan->Prev = nullptr;
	}

	[[nodiscard]] static void* popBlock(void*& Head) noexcept
	{
		if (!Head)
		{
			return nullptr;
		}

		void* Block = Head;
		Head = *reinterpret_cast<void**>(Block);
		return Block;
	}

	static void pushBlock(void*& Head, void* Block) noexcept
	{
		*reinterpret_cast<void**>(Block) = Head;
		Head = Block;
	}

	PageAllocator* PageAllocator = nullptr;
	std::size_t PageSize = 0;

	std::array<SizeClass, NumSizeClasses> Classes{};
	std::array<SizeClassBucket, NumSizeClasses> Buckets{};

	using SpanLookupSnapshot = std::unordered_map<std::uint64_t, Span*>;

	mutable std::mutex SpanMapMutex;
	core::containers::RadixTreeMap<Span*, 12, 16, 10, 10, 256, 4096> PageToSpan;
	std::atomic<std::shared_ptr<const SpanLookupSnapshot>> SpanLookupSnapshotState;

	std::mutex SpanStorageMutex;
	Span* SpanFreeList = nullptr;
	std::array<Span*, NumSizeClasses> SpanClassFreeLists{};
	std::vector<std::unique_ptr<Span[]>> SpanPoolChunks;
	std::size_t SpanPoolAllocatedCount = 0;
	std::size_t SpanPoolFreeCount = 0;
};

} // namespace core::mem
