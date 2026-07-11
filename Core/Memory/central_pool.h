#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

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

inline std::array<std::uint16_t, ClassLookupCount> buildSizeClassLookup()
{
	std::array<std::uint16_t, ClassLookupCount> lookup{};

	for (std::size_t bucket = 0; bucket < lookup.size(); ++bucket)
	{
		const std::size_t size = bucket * ClassQuantum;
		std::uint16_t class_index = 0;

		while (class_index + 1 < NumSizeClasses && SizeClasses[class_index] < size)
		{
			++class_index;
		}

		lookup[bucket] = class_index;
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

	const std::size_t bucket = (size + ClassQuantum - 1) / ClassQuantum;
	return SizeClassLookup[bucket];
}

[[nodiscard]] inline std::size_t classIndexToSize(const std::uint16_t classIndex) noexcept
{
	if (classIndex >= NumSizeClasses)
	{
		return 0;
	}

	return alignUp(static_cast<std::size_t>(SizeClasses[classIndex]), alignof(std::max_align_t));
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

	std::uint32_t BlockSize = 0;
	std::uint16_t TotalBlocks = 0;
	std::uint16_t FreeBlocks = 0;
	std::uint16_t SizeClassIndex = InvalidSizeClass;
	std::uint16_t Reserved = 0;

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
	explicit CentralPool(PageAllocator& pageAllocator)
		: PageAllocator(&pageAllocator)
		, PageSize(pageAllocator.getPageSize())
	{
		for (std::size_t i = 0; i < NumSizeClasses; ++i)
		{
			Classes[i] = buildSizeClass(pageAllocator.getPageSize(), SizeClasses[i]);
		}
	}

	CentralPool(const CentralPool&) = delete;
	CentralPool& operator=(const CentralPool&) = delete;

	/**
	 * @brief Fetch a Block batch from one size class into thread-local cache.
	 * @param
	 *     - SizeClassIndex  std::uint16_t  Target size-class index.
	 *     - OutBlocks       void**         Caller-provided output Block array.
	 *     - MaxBlocks       std::uint32_t  Maximum blocks to fetch.
	 * @usage
	 *     - Called on ThreadCache miss to amortize synchronization cost.
	 * @return
	 *     - std::uint32_t  Number of blocks returned.
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

		SizeClassBucket& bucket = Buckets[SizeClassIndex];
		std::lock_guard lock(bucket.Mutex);

		Span* SpanObj = bucket.PartialSpans;
		if (!SpanObj)
		{
			SpanObj = bucket.EmptySpans;
		}

		if (!SpanObj)
		{
			SpanObj = allocateSpanForClass(SizeClassIndex, bucket);
			if (!SpanObj)
			{
				return 0;
			}
		}

		if (SpanObj->IsInEmptyList)
		{
			removeSpan(bucket.EmptySpans, SpanObj);
			SpanObj->IsInEmptyList = false;
			if (bucket.EmptySpanCount > 0)
			{
				--bucket.EmptySpanCount;
			}
		}

		if (!SpanObj->IsInPartialList)
		{
			insertSpan(bucket.PartialSpans, SpanObj);
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
			removeSpan(bucket.PartialSpans, SpanObj);
			SpanObj->IsInPartialList = false;
		}

		return allocated;
	}

	/**
	 * @brief Return a linked Block batch back to one central size class.
	 * @param
	 *     - SizeClassIndex  std::uint16_t  Target size-class index.
	 *     - Head            void*          Intrusive single-link Head pointer.
	 *     - count           std::uint32_t  Number of blocks in chain.
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

		SizeClassBucket& bucket = Buckets[SizeClassIndex];
		std::lock_guard lock(bucket.Mutex);
		std::shared_lock spanMapLock(SpanMapMutex);

		void* current = Head;
		for (std::uint32_t i = 0; i < count && current; ++i)
		{
			void* next = *reinterpret_cast<void**>(current);
			Span* SpanObj = findSpanLocked(current);
			if (!SpanObj || SpanObj->IsReleased)
			{
				current = next;
				continue;
			}

			const bool was_full = SpanObj->FreeBlocks == 0;
			pushBlock(SpanObj->FreeList, current);
			++SpanObj->FreeBlocks;

			if (was_full)
			{
				insertSpan(bucket.PartialSpans, SpanObj);
				SpanObj->IsInPartialList = true;
			}

			if (SpanObj->FreeBlocks == SpanObj->TotalBlocks)
			{
				if (SpanObj->IsInPartialList)
				{
					removeSpan(bucket.PartialSpans, SpanObj);
					SpanObj->IsInPartialList = false;
				}

				if (!SpanObj->IsInEmptyList)
				{
					insertSpan(bucket.EmptySpans, SpanObj);
					SpanObj->IsInEmptyList = true;
					++bucket.EmptySpanCount;
				}
			}

			current = next;
		}

		spanMapLock.unlock();

		while (bucket.EmptySpanCount > MaxRetainedEmptySpansPerClass)
		{
			Span* SpanObj = bucket.EmptySpans;
			if (!SpanObj)
			{
				break;
			}
			releaseEmptySpan(bucket, SpanObj);
		}
	}

	/**
	 * @brief Read immutable size-class metadata.
	 * @param
	 *     - classIndex  std::uint16_t  Size-class index.
	 * @usage
	 *     - Used by ThreadCache and allocator layout stage.
	 * @return
	 *     - SizeClass  Metadata for this class or zero-initialized fallback.
	 */
	[[nodiscard]] SizeClass getSizeClassInfo(const std::uint16_t classIndex) const noexcept
	{
		return classIndex < NumSizeClasses ? Classes[classIndex] : SizeClass{};
	}

private:
	struct SizeClassBucket
	{
		std::mutex Mutex;
		Span* PartialSpans = nullptr;
		Span* EmptySpans = nullptr;
		std::size_t EmptySpanCount = 0;
	};

	[[nodiscard]] static SizeClass buildSizeClass(const std::size_t pageSize, const std::uint32_t blockSize) noexcept
	{
		const std::uint32_t alignedBlockSize =
			static_cast<std::uint32_t>(alignUp(static_cast<std::size_t>(blockSize), alignof(std::max_align_t)));

		const std::size_t targetBytes = alignedBlockSize <= 4096 ? 128u * 1024u : 256u * 1024u;
		std::size_t blocks = std::max<std::size_t>(32, targetBytes / alignedBlockSize);

		std::size_t spanBytes = alignUp(blocks * static_cast<std::size_t>(alignedBlockSize), pageSize);
		std::size_t pages = std::max<std::size_t>(1, spanBytes / pageSize);
		spanBytes = pages * pageSize;
		blocks = spanBytes / alignedBlockSize;

		if (blocks > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()))
		{
			blocks = std::numeric_limits<std::uint16_t>::max();
		}
		if (pages > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()))
		{
			pages = std::numeric_limits<std::uint16_t>::max();
		}

		SizeClass info;
		info.BlockSize = alignedBlockSize;
		info.BlocksPerSpan = static_cast<std::uint16_t>(blocks);
		info.PagesPerSpan = static_cast<std::uint16_t>(pages);
		return info;
	}

	[[nodiscard]] Span* allocateSpanForClass(
		const std::uint16_t classIndex,
		SizeClassBucket& bucket)
	{
		const SizeClass info = Classes[classIndex];
		PageSpan pageSpan = PageAllocator->acquireSpan(info.PagesPerSpan);
		if (!pageSpan.isValid())
		{
			return nullptr;
		}

		auto SpenUniquePtr = std::make_unique<Span>();
		SpenUniquePtr->BlockSize = info.BlockSize;
		SpenUniquePtr->TotalBlocks = info.BlocksPerSpan;
		SpenUniquePtr->FreeBlocks = info.BlocksPerSpan;
		SpenUniquePtr->SizeClassIndex = classIndex;
		SpenUniquePtr->SpanBase = pageSpan.Base;
		SpenUniquePtr->BackingSpan = pageSpan;

		std::byte* Block = SpenUniquePtr->SpanBase;
		for (std::uint16_t i = 0; i < SpenUniquePtr->TotalBlocks; ++i)
		{
			pushBlock(SpenUniquePtr->FreeList, Block);
			Block += SpenUniquePtr->BlockSize;
		}

		Span* SpanRawPtr = SpenUniquePtr.get();
		{
			std::lock_guard storageLock(SpanStorageMutex);
			SpanStorage.push_back(std::move(SpenUniquePtr));
		}

		registerSpanPages(*SpanRawPtr);
		insertSpan(bucket.EmptySpans, SpanRawPtr);
		SpanRawPtr->IsInEmptyList = true;
		++bucket.EmptySpanCount;

		return SpanRawPtr;
	}

	void releaseEmptySpan(SizeClassBucket& bucket, Span* InSpan)
	{
		if (!InSpan || InSpan->IsReleased)
		{
			return;
		}

		if (InSpan->IsInPartialList)
		{
			removeSpan(bucket.PartialSpans, InSpan);
			InSpan->IsInPartialList = false;
		}

		if (InSpan->IsInEmptyList)
		{
			removeSpan(bucket.EmptySpans, InSpan);
			InSpan->IsInEmptyList = false;
			if (bucket.EmptySpanCount > 0)
			{
				--bucket.EmptySpanCount;
			}
		}

		unregisterSpanPages(*InSpan);
		PageAllocator->releaseSpan(InSpan->BackingSpan);

		InSpan->BackingSpan = {};
		InSpan->SpanBase = nullptr;
		InSpan->FreeList = nullptr;
		InSpan->TotalBlocks = 0;
		InSpan->FreeBlocks = 0;
		InSpan->IsReleased = true;
	}

	void registerSpanPages(Span& InSpan)
	{
		if (!InSpan.BackingSpan.isValid())
		{
			return;
		}

		std::unique_lock lock(SpanMapMutex);
		const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(InSpan.SpanBase);
		for (std::size_t i = 0; i < InSpan.BackingSpan.PageCount; ++i)
		{
			PageToSpan[base + i * PageSize] = &InSpan;
		}
	}

	void unregisterSpanPages(const Span& InSpan)
	{
		if (!InSpan.BackingSpan.isValid())
		{
			return;
		}

		std::unique_lock lock(SpanMapMutex);
		const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(InSpan.SpanBase);
		for (std::size_t i = 0; i < InSpan.BackingSpan.PageCount; ++i)
		{
			PageToSpan.erase(base + i * PageSize);
		}
	}

	[[nodiscard]] Span* findSpanLocked(void* Ptr) const
	{
		const std::uintptr_t Address = reinterpret_cast<std::uintptr_t>(Ptr);
		const std::uintptr_t PageBase = Address & ~(PageSize - 1);
		auto Iter = PageToSpan.find(PageBase);
		return Iter == PageToSpan.end() ? nullptr : Iter->second;
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

	mutable std::shared_mutex SpanMapMutex;
	std::unordered_map<std::uintptr_t, Span*> PageToSpan;

	std::mutex SpanStorageMutex;
	std::vector<std::unique_ptr<Span>> SpanStorage;
};

} // namespace core::mem
