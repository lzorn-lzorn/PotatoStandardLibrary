
#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

#include "../Containers/radix_tree.h"

#include "memory_common.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace core::mem
{

struct VirtualRegion
{
    std::byte* Base = nullptr;
    std::size_t ReservedSize = 0;
    std::size_t CommittedSize = 0;
    std::size_t PageSize = 0;
    std::int32_t NumaNode = -1;
    bool HugePage = false;

    [[nodiscard]] bool isValid() const noexcept
    {
        return Base != nullptr;
    }
};

struct PageSpan
{
    std::byte* Base = nullptr;
    std::size_t PageCount = 0;
    std::size_t PageSize = 0;
    void* OwnerRegion = nullptr;
    std::uint64_t OwnerRegionId = 0;

    [[nodiscard]] bool isValid() const noexcept
    {
        return Base != nullptr && PageCount > 0 && PageSize > 0;
    }

    [[nodiscard]] std::size_t getBytes() const noexcept
    {
        return PageCount * PageSize;
    }
};

class VirtualMemoryManager
{
public:
    /**
     * @brief Query system default page Size.
     * @param
     *     - none
     * @usage
     *     - Use this Value as baseline granularity for regular virtual regions.
     * @return
     *     - std::size_t  OS page Size in Bytes.
     */
    [[nodiscard]] static std::size_t getSystemPageSize() noexcept
    {
#if defined(_WIN32)
        SYSTEM_INFO Info{};
        ::GetSystemInfo(&Info);
        return static_cast<std::size_t>(Info.dwPageSize);
#else
        const long Value = ::sysconf(_SC_PAGESIZE);
        return Value > 0 ? static_cast<std::size_t>(Value) : 4096u;
#endif
    }

    /**
     * @brief Query huge-page Size supported by the current platform.
     * @param
     *     - none
     * @usage
     *     - Check result before enabling huge-page reserve paths.
     * @return
     *     - std::size_t  Huge-page Size in Bytes, 0 if unavailable.
     */
    [[nodiscard]] static std::size_t getHugePageSize() noexcept
    {
#if defined(_WIN32)
        const SIZE_T Value = ::GetLargePageMinimum();
        return Value == 0 ? 0u : static_cast<std::size_t>(Value);
#else
        return 2u * 1024u * 1024u;
#endif
    }

    /**
     * @brief Reserve virtual address space without forcing physical commit.
     * @param
     *     - Bytes      std::size_t   Requested virtual Size.
     *     - HugePage   bool          Prefer huge-page mapping when possible.
     *     - NumaNode   std::int32_t  Preferred NUMA node, negative means default policy.
     * @usage
     *     - This is the entry of the virtual-memory pipeline before page commit.
     * @return
     *     - VirtualRegion  Valid Region on success, empty Region on failure.
     */
    [[nodiscard]] static VirtualRegion reserve(
        const std::size_t Bytes,
        const bool HugePage,
        const std::int32_t NumaNode) noexcept
    {
        const std::size_t systemPageSize = getSystemPageSize();
        const std::size_t hugePageSize = getHugePageSize();
        const bool requestHugePage = HugePage && hugePageSize > 0;
        const std::size_t requestPageSize = requestHugePage ? hugePageSize : systemPageSize;
        const std::size_t AlignedBytes = alignUp(Bytes, requestPageSize);
        if (AlignedBytes == 0)
        {
            return {};
        }

#if defined(_WIN32)
        std::byte* Base = nullptr;
        bool HugePageMapped = false;

        if (requestHugePage)
        {
            const DWORD hugeAllocType = MEM_RESERVE | MEM_LARGE_PAGES;
            Base = static_cast<std::byte*>(
                allocateWindows(nullptr, AlignedBytes, hugeAllocType, PAGE_READWRITE, NumaNode));
            HugePageMapped = Base != nullptr;
        }

        if (!Base)
        {
            Base = static_cast<std::byte*>(allocateWindows(nullptr, AlignedBytes, MEM_RESERVE, PAGE_READWRITE, NumaNode));
            HugePageMapped = false;
        }

        if (!Base)
        {
            return {};
        }

        VirtualRegion Region;
        Region.Base = Base;
        Region.ReservedSize = AlignedBytes;
        Region.PageSize = HugePageMapped ? hugePageSize : systemPageSize;
        Region.NumaNode = NumaNode;
        Region.HugePage = HugePageMapped;
        return Region;
#else
        int Flags = MAP_PRIVATE | MAP_ANONYMOUS;
        bool HugePageMapped = false;
#if defined(MAP_HUGETLB)
        if (requestHugePage)
        {
            Flags |= MAP_HUGETLB;
            HugePageMapped = true;
        }
#endif
        void* Base = ::mmap(nullptr, AlignedBytes, PROT_NONE, Flags, -1, 0);

    #if defined(MAP_HUGETLB)
        if (Base == MAP_FAILED && HugePageMapped)
        {
            Flags &= ~MAP_HUGETLB;
            HugePageMapped = false;
            Base = ::mmap(nullptr, AlignedBytes, PROT_NONE, Flags, -1, 0);
        }
    #endif

        if (Base == MAP_FAILED)
        {
            return {};
        }

        VirtualRegion Region;
        Region.Base = static_cast<std::byte*>(Base);
        Region.ReservedSize = AlignedBytes;
        Region.PageSize = HugePageMapped ? hugePageSize : systemPageSize;
        Region.NumaNode = NumaNode;
        Region.HugePage = HugePageMapped;
        return Region;
#endif
    }

    /**
     * @brief Commit a sub-range inside a reserved Region.
     * @param
     *     - Region  VirtualRegion&  Region metadata to update.
     *     - Offset  std::size_t     Start offset from Region Base.
     *     - Bytes   std::size_t     Commit length.
     * @usage
     *     - Call before handing a page span to upper allocator layers.
     * @return
     *     - bool  true on success, false on bounds or OS failure.
     */
    static bool commit(VirtualRegion& Region, const std::size_t Offset, const std::size_t Bytes) noexcept
    {
        if (!Region.isValid())
        {
            return false;
        }

        const std::size_t AlignedOffset = alignUp(Offset, Region.PageSize);
        const std::size_t AlignedBytes = alignUp(Bytes, Region.PageSize);
        if (AlignedOffset + AlignedBytes > Region.ReservedSize)
        {
            return false;
        }

#if defined(_WIN32)
        void* Committed = ::VirtualAlloc(Region.Base + AlignedOffset, AlignedBytes, MEM_COMMIT, PAGE_READWRITE);
        if (!Committed)
        {
            return false;
        }
#else
        if (::mprotect(Region.Base + AlignedOffset, AlignedBytes, PROT_READ | PROT_WRITE) != 0)
        {
            return false;
        }
#endif

        Region.CommittedSize = std::max(Region.CommittedSize, AlignedOffset + AlignedBytes);
        return true;
    }

    /**
     * @brief Decommit a Committed sub-range while keeping virtual address stable.
     * @param
     *     - Region  VirtualRegion&  Region metadata to update.
     *     - Offset  std::size_t     Start offset from Region Base.
     *     - Bytes   std::size_t     Decommit length.
     * @usage
     *     - Use for lazy reclamation without invalidating reserved address range.
     * @return
     *     - bool  true on success, false on bounds or OS failure.
     */
    static bool decommit(VirtualRegion& Region, const std::size_t Offset, const std::size_t Bytes) noexcept
    {
        if (!Region.isValid())
        {
            return false;
        }

        const std::size_t AlignedOffset = alignUp(Offset, Region.PageSize);
        const std::size_t AlignedBytes = alignUp(Bytes, Region.PageSize);
        if (AlignedOffset + AlignedBytes > Region.ReservedSize)
        {
            return false;
        }

#if defined(_WIN32)
        if (::VirtualFree(Region.Base + AlignedOffset, AlignedBytes, MEM_DECOMMIT) == 0)
        {
            return false;
        }
#else
        if (::mprotect(Region.Base + AlignedOffset, AlignedBytes, PROT_NONE) != 0)
        {
            return false;
        }
#endif

        if (AlignedOffset + AlignedBytes >= Region.CommittedSize)
        {
            Region.CommittedSize = AlignedOffset;
        }

        return true;
    }

    static bool protectNoAccess(const VirtualRegion& Region, const std::size_t Offset, const std::size_t Bytes) noexcept
    {
        if (!Region.isValid())
        {
            return false;
        }

        const std::size_t AlignedOffset = alignUp(Offset, Region.PageSize);
        const std::size_t AlignedBytes = alignUp(Bytes, Region.PageSize);
        if (AlignedOffset + AlignedBytes > Region.ReservedSize)
        {
            return false;
        }

#if defined(_WIN32)
        DWORD OldProtect = 0;
        return ::VirtualProtect(Region.Base + AlignedOffset, AlignedBytes, PAGE_NOACCESS, &OldProtect) != 0;
#else
        return ::mprotect(Region.Base + AlignedOffset, AlignedBytes, PROT_NONE) == 0;
#endif
    }

    static bool protectReadWrite(const VirtualRegion& Region, const std::size_t Offset, const std::size_t Bytes) noexcept
    {
        if (!Region.isValid())
        {
            return false;
        }

        const std::size_t AlignedOffset = alignUp(Offset, Region.PageSize);
        const std::size_t AlignedBytes = alignUp(Bytes, Region.PageSize);
        if (AlignedOffset + AlignedBytes > Region.ReservedSize)
        {
            return false;
        }

#if defined(_WIN32)
        DWORD OldProtect = 0;
        return ::VirtualProtect(Region.Base + AlignedOffset, AlignedBytes, PAGE_READWRITE, &OldProtect) != 0;
#else
        return ::mprotect(Region.Base + AlignedOffset, AlignedBytes, PROT_READ | PROT_WRITE) == 0;
#endif
    }

    /**
     * @brief Release an entire virtual Region and reset its metadata.
     * @param
     *     - Region  VirtualRegion&  Region to release.
     * @usage
     *     - Call only when the allocator no longer keeps any span in this Region.
     * @return
     *     - void
     */
    static void release(VirtualRegion& Region) noexcept
    {
        if (!Region.isValid())
        {
            return;
        }

#if defined(_WIN32)
        ::VirtualFree(Region.Base, 0, MEM_RELEASE);
#else
        ::munmap(Region.Base, Region.ReservedSize);
#endif

        Region = {};
    }

private:
#if defined(_WIN32)
    [[nodiscard]] static void* allocateWindows(
        void* Hint,
        const std::size_t Size,
        const DWORD AllocType,
        const DWORD Protect,
        const std::int32_t NumaNode) noexcept
    {
        if (NumaNode < 0)
        {
            return ::VirtualAlloc(Hint, Size, AllocType, Protect);
        }

        using VirtualAllocExNumaFn = PVOID(WINAPI*)(HANDLE, PVOID, SIZE_T, DWORD, DWORD, DWORD);
        static VirtualAllocExNumaFn VirtualAllocExNuma =
            reinterpret_cast<VirtualAllocExNumaFn>(::GetProcAddress(
                ::GetModuleHandleW(L"kernel32.dll"),
                "VirtualAllocExNuma"));

        if (!VirtualAllocExNuma)
        {
            return ::VirtualAlloc(Hint, Size, AllocType, Protect);
        }

        return VirtualAllocExNuma(
            ::GetCurrentProcess(),
            Hint,
            Size,
            AllocType,
            Protect,
            static_cast<DWORD>(NumaNode));
    }
#endif
};

class PageAllocator
{
public:
    struct RegionIndexPoolStats
    {
        std::size_t RegionCount = 0;
        std::size_t EntryCount = 0;
        std::size_t Level1NodesUsed = 0;
        std::size_t Level1NodesCapacity = 0;
        std::size_t Level2NodesUsed = 0;
        std::size_t Level2NodesCapacity = 0;
        double Level1UsageRatio = 0.0;
        double Level2UsageRatio = 0.0;
        std::size_t PoolExhaustionWarningCount = 0;
    };

    explicit PageAllocator(const AllocatorConfig& InConfig)
        : Config(InConfig)
        , PageSize(VirtualMemoryManager::getSystemPageSize())
        , ShardCount(determineShardCount())
        , Shards(std::make_unique<Shard[]>(ShardCount))
    {
        for (std::size_t i = 0; i < ShardCount; ++i)
        {
            Shards[i].NextRegionId = i == 0 ? ShardCount : i;
        }
    }

    ~PageAllocator()
    {
        releaseAllRegions();
    }

    PageAllocator(const PageAllocator&) = delete;
    PageAllocator& operator=(const PageAllocator&) = delete;

    /**
     * @brief Return allocator page granularity.
     * @param
     *     - none
     * @usage
     *     - Used by upper layers to derive pages-per-span and alignment constraints.
     * @return
     *     - std::size_t  Page Size in Bytes.
     */
    [[nodiscard]] std::size_t getPageSize() const noexcept
    {
        return PageSize;
    }

    [[nodiscard]] RegionIndexPoolStats getRegionIndexPoolStats()
    {
        RegionIndexPoolStats stats;

        for (std::size_t i = 0; i < ShardCount; ++i)
        {
            Shard& shard = Shards[i];
            std::lock_guard Lock(shard.Mutex);

            stats.RegionCount += shard.Regions.size();
            stats.EntryCount += shard.RegionIndex.size();
            stats.Level1NodesUsed += shard.RegionIndex.level1NodesUsed();
            stats.Level1NodesCapacity += shard.RegionIndex.level1NodesCapacity();
            stats.Level2NodesUsed += shard.RegionIndex.level2NodesUsed();
            stats.Level2NodesCapacity += shard.RegionIndex.level2NodesCapacity();
            stats.PoolExhaustionWarningCount += shard.RegionIndex.poolExhaustionFailureCount();
        }

        stats.Level1UsageRatio = toUsageRatio(stats.Level1NodesUsed, stats.Level1NodesCapacity);
        stats.Level2UsageRatio = toUsageRatio(stats.Level2NodesUsed, stats.Level2NodesCapacity);
        return stats;
    }

    /**
     * @brief Acquire a contiguous span with page granularity.
     * @param
     *     - PageCount  std::size_t  Requested page count.
     * @usage
     *     - CentralPool requests small/medium spans through this function.
     * @return
     *     - PageSpan  Valid span on success, empty span on failure.
     */
    [[nodiscard]] PageSpan acquireSpan(const std::size_t PageCount)
    {
        CORE_MEM_BENCH_SCOPE(MemoryBenchPoint::PageAllocatorAcquireSpan);

        if (PageCount == 0)
        {
            return {};
        }

        Shard& shard = getShardForCurrentThread();
        std::lock_guard Lock(shard.Mutex);

        // 优先命中 free cache，保证 Span 的地址复用和延迟稳定性。
        PageSpan CachedSpan;
        if (tryPopCachedSpanLocked(shard, PageCount, CachedSpan))
        {
            RegionCursor* owner = findRegionByIdLocked(shard, CachedSpan.OwnerRegionId);
            if (!owner || !owner->Region.isValid())
            {
                return {};
            }

            const std::size_t offset = static_cast<std::size_t>(CachedSpan.Base - owner->Region.Base);
            const std::size_t spanBytes = CachedSpan.getBytes();
            if (offset > owner->Region.ReservedSize || spanBytes > owner->Region.ReservedSize - offset)
            {
                return {};
            }

            if (!VirtualMemoryManager::commit(owner->Region, offset, CachedSpan.getBytes()))
            {
                return {};
            }

            CachedSpan.OwnerRegion = owner;

            return CachedSpan;
        }

        for (RegionCursor& Cursor : shard.Regions)
        {
            const std::size_t availablePages = Cursor.ReservedPages - Cursor.NextPage;
            if (availablePages < PageCount)
            {
                continue;
            }

            PageSpan Span;
            Span.Base = Cursor.Region.Base + Cursor.NextPage * PageSize;
            Span.PageCount = PageCount;
            Span.PageSize = PageSize;
            Span.OwnerRegion = &Cursor;
            Span.OwnerRegionId = Cursor.RegionId;

            const std::size_t offset = Cursor.NextPage * PageSize;
            Cursor.NextPage += PageCount;

            if (!VirtualMemoryManager::commit(Cursor.Region, offset, Span.getBytes()))
            {
                Cursor.NextPage -= PageCount;
                return {};
            }

            return Span;
        }

        const std::size_t requestedBytes = PageCount * PageSize;
        const std::size_t reserveBytes = alignUp(std::max(Config.SmallRegionReserveBytes, requestedBytes), PageSize);

        VirtualRegion Region = VirtualMemoryManager::reserve(
            reserveBytes,
            Config.EnableHugePage,
            Config.PreferredNumaNode);

        if (!Region.isValid())
        {
            return {};
        }

        RegionCursor Cursor;
        Cursor.Region = Region;
        Cursor.RegionId = shard.NextRegionId;
        shard.NextRegionId += ShardCount;
        Cursor.ReservedPages = reserveBytes / PageSize;

        shard.Regions.push_back(std::move(Cursor));
        RegionCursor& back = shard.Regions.back();
        if (!shard.RegionIndex.insertOrAssign(back.RegionId, &back))
        {
            VirtualMemoryManager::release(back.Region);
            shard.Regions.pop_back();
            return {};
        }

        PageSpan Span;
        Span.Base = back.Region.Base;
        Span.PageCount = PageCount;
        Span.PageSize = PageSize;
        Span.OwnerRegion = &back;
        Span.OwnerRegionId = back.RegionId;
        back.NextPage = PageCount;

        if (!VirtualMemoryManager::commit(back.Region, 0, Span.getBytes()))
        {
            VirtualMemoryManager::release(back.Region);
            shard.RegionIndex.erase(back.RegionId);
            shard.Regions.pop_back();
            return {};
        }

        return Span;
    }

    /**
     * @brief Return a span back to the page allocator.
     * @param
     *     - span  const PageSpan&  Span to release.
     * @usage
     *     - Called by CentralPool after an empty span is detached from Size-class lists.
     * @return
     *     - void
     */
    void releaseSpan(const PageSpan& Span)
    {
        CORE_MEM_BENCH_SCOPE(MemoryBenchPoint::PageAllocatorReleaseSpan);

        if (!Span.isValid())
        {
            return;
        }

        Shard& shard = getShard(Span.OwnerRegionId);
        std::lock_guard Lock(shard.Mutex);
        RegionCursor* owner = findRegionByIdLocked(shard, Span.OwnerRegionId);
        if (owner && owner->Region.isValid())
        {
            if constexpr (MemoryCompileLazyCommit)
            {
                const std::size_t offset = static_cast<std::size_t>(Span.Base - owner->Region.Base);
                const std::size_t spanBytes = Span.getBytes();
                if (offset <= owner->Region.ReservedSize && spanBytes <= owner->Region.ReservedSize - offset)
                {
                    const bool largeSpan = spanBytes >= decommitThresholdBytes();
                    const bool sampled = (shard.DecommitCounter++ & DecommitSampleMask) == 0;
                    if (largeSpan || sampled)
                    {
                        VirtualMemoryManager::decommit(owner->Region, offset, spanBytes);
                    }
                }
            }
        }

        pushCachedSpanLocked(shard, Span);
    }

private:
    static constexpr std::size_t MaxShardCount = tuning::PageAllocatorMaxShardCount;
    static constexpr std::uint32_t DecommitSampleMask = tuning::PageAllocatorDecommitSampleMask;

    [[nodiscard]] static double toUsageRatio(const std::size_t used, const std::size_t capacity) noexcept
    {
        if (capacity == 0)
        {
            return 0.0;
        }

        return static_cast<double>(used) / static_cast<double>(capacity);
    }

    struct RegionCursor
    {
        VirtualRegion Region;
        std::uint64_t RegionId = 0;
        std::size_t ReservedPages = 0;
        std::size_t NextPage = 0;
    };

    struct Shard
    {
        SpinLock Mutex;
        std::deque<RegionCursor> Regions;
        core::containers::RadixTreeMap<RegionCursor*, 0, 16, 10, 10, 32, 512> RegionIndex;
        std::array<std::vector<PageSpan>, tuning::PageAllocatorCacheBucketCount> FreeSpanCache;
        std::vector<PageSpan> OversizedFreeSpanCache;
        std::uint64_t NextRegionId = 1;
        std::uint32_t DecommitCounter = 0;
    };

    [[nodiscard]] static std::size_t determineShardCount() noexcept
    {
        const std::size_t hardware = std::thread::hardware_concurrency();
        const std::size_t desired = hardware == 0
            ? tuning::PageAllocatorMinShardCount
            : hardware * tuning::PageAllocatorShardScale;
        return std::max<std::size_t>(1, std::min<std::size_t>(desired, MaxShardCount));
    }

    [[nodiscard]] static std::size_t getCacheBucket(const std::size_t PageCount) noexcept
    {
        if (PageCount == 0)
        {
            return 0;
        }

        if (PageCount <= 4)
        {
            return PageCount - 1;
        }

        if (PageCount <= 16)
        {
            return 4 + (PageCount / 2);
        }

        const std::size_t FloorLog2 = std::bit_width(PageCount) - 1;
        return std::min<std::size_t>(
            tuning::PageAllocatorCacheBucketCount - 1,
            12 + FloorLog2);
    }

    [[nodiscard]] std::size_t decommitThresholdBytes() const noexcept
    {
        const std::size_t dynamicThreshold = std::max<std::size_t>(
            tuning::PageAllocatorDecommitMinBytes,
            Config.SmallRegionReserveBytes / tuning::PageAllocatorDecommitRegionDivisor);
        return std::max<std::size_t>(dynamicThreshold, PageSize * tuning::PageAllocatorDecommitMinPages);
    }

    [[nodiscard]] std::size_t selectShardIndex() const noexcept
    {
        if (ShardCount <= 1)
        {
            return 0;
        }

        const std::size_t hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
        return hash % ShardCount;
    }

    [[nodiscard]] Shard& getShardForCurrentThread() noexcept
    {
        return Shards[selectShardIndex()];
    }

    [[nodiscard]] Shard& getShard(const std::uint64_t RegionId) noexcept
    {
        if (ShardCount <= 1)
        {
            return Shards[0];
        }

        return Shards[static_cast<std::size_t>(RegionId % ShardCount)];
    }

    [[nodiscard]] bool tryPopCachedSpanLocked(Shard& ShardState, const std::size_t PageCount, PageSpan& OutSpan)
    {
        std::vector<PageSpan>& bucket = ShardState.FreeSpanCache[getCacheBucket(PageCount)];
        for (std::size_t i = bucket.size(); i > 0; --i)
        {
            PageSpan& candidate = bucket[i - 1];
            if (candidate.PageCount != PageCount)
            {
                continue;
            }

            OutSpan = candidate;
            bucket[i - 1] = bucket.back();
            bucket.pop_back();
            return true;
        }

        for (std::size_t i = 0; i < ShardState.OversizedFreeSpanCache.size(); ++i)
        {
            if (ShardState.OversizedFreeSpanCache[i].PageCount != PageCount)
            {
                continue;
            }

            OutSpan = ShardState.OversizedFreeSpanCache[i];
            ShardState.OversizedFreeSpanCache[i] = ShardState.OversizedFreeSpanCache.back();
            ShardState.OversizedFreeSpanCache.pop_back();
            return true;
        }

        return false;
    }

    void pushCachedSpanLocked(Shard& ShardState, const PageSpan& Span)
    {
        if (Span.PageCount <= tuning::PageAllocatorCachedMaxPageCount)
        {
            ShardState.FreeSpanCache[getCacheBucket(Span.PageCount)].push_back(Span);
            return;
        }

        ShardState.OversizedFreeSpanCache.push_back(Span);
    }

    [[nodiscard]] RegionCursor* findRegionByIdLocked(Shard& ShardState, const std::uint64_t RegionId) noexcept
    {
        if (RegionId == 0)
        {
            return nullptr;
        }

        return ShardState.RegionIndex.find(RegionId);
    }

    void releaseAllRegions()
    {
        for (std::size_t i = 0; i < ShardCount; ++i)
        {
            Shard& shard = Shards[i];
            std::lock_guard Lock(shard.Mutex);
            for (RegionCursor& cursor : shard.Regions)
            {
                VirtualMemoryManager::release(cursor.Region);
            }
            shard.Regions.clear();
            shard.RegionIndex.clear();
            for (std::vector<PageSpan>& bucket : shard.FreeSpanCache)
            {
                bucket.clear();
            }
            shard.OversizedFreeSpanCache.clear();
        }
    }

    AllocatorConfig Config;
    std::size_t PageSize;
    std::size_t ShardCount = 1;
    std::unique_ptr<Shard[]> Shards;
};

} // namespace core::mem