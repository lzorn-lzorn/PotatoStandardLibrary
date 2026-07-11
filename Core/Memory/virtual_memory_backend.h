
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

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
        const std::size_t pageSize = HugePage && getHugePageSize() > 0 ? getHugePageSize() : getSystemPageSize();
        const std::size_t AlignedBytes = alignUp(Bytes, pageSize);
        if (AlignedBytes == 0)
        {
            return {};
        }

#if defined(_WIN32)
        DWORD AllocType = MEM_RESERVE;
        if (HugePage && getHugePageSize() > 0)
        {
            AllocType |= MEM_LARGE_PAGES;
        }

        std::byte* base = static_cast<std::byte*>(allocateWindows(nullptr, AlignedBytes, AllocType, PAGE_READWRITE, NumaNode));

        if (!base && (AllocType & MEM_LARGE_PAGES) != 0)
        {
            base = static_cast<std::byte*>(allocateWindows(nullptr, AlignedBytes, MEM_RESERVE, PAGE_READWRITE, NumaNode));
        }

        if (!base)
        {
            return {};
        }

        VirtualRegion Region;
        Region.Base = base;
        Region.ReservedSize = AlignedBytes;
        Region.PageSize = pageSize;
        Region.NumaNode = NumaNode;
        Region.HugePage = HugePage && (AllocType & MEM_LARGE_PAGES) != 0;
        return Region;
#else
        int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(MAP_HUGETLB)
        if (HugePage)
        {
            flags |= MAP_HUGETLB;
        }
#endif
        void* base = ::mmap(nullptr, AlignedBytes, PROT_NONE, flags, -1, 0);
        if (base == MAP_FAILED)
        {
            return {};
        }

        VirtualRegion Region;
        Region.Base = static_cast<std::byte*>(base);
        Region.ReservedSize = AlignedBytes;
        Region.PageSize = pageSize;
        Region.NumaNode = NumaNode;
        Region.HugePage = HugePage;
        return Region;
#endif
    }

    /**
     * @brief Commit a sub-range inside a reserved Region.
     * @param
     *     - Region  VirtualRegion&  Region metadata to update.
     *     - Offset  std::size_t     Start offset from Region base.
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
        DWORD AllocType = MEM_COMMIT;
        if (Region.HugePage)
        {
            AllocType |= MEM_LARGE_PAGES;
        }

        void* committed = ::VirtualAlloc(Region.Base + AlignedOffset, AlignedBytes, AllocType, PAGE_READWRITE);
        if (!committed)
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
     * @brief Decommit a committed sub-range while keeping virtual address stable.
     * @param
     *     - Region  VirtualRegion&  Region metadata to update.
     *     - Offset  std::size_t     Start offset from Region base.
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
    explicit PageAllocator(const AllocatorConfig& InConfig)
        : Config(InConfig)
        , PageSize(VirtualMemoryManager::getSystemPageSize())
    {
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
        if (PageCount == 0)
        {
            return {};
        }

        std::lock_guard lock(Mutex);

        // 优先命中 free cache，保证 Span 的地址复用和延迟稳定性。
        auto CacheIt = FreeSpanCache.find(PageCount);
        if (CacheIt != FreeSpanCache.end() && !CacheIt->second.empty())
        {
            PageSpan Span = CacheIt->second.back();
            CacheIt->second.pop_back();

            auto* owner = static_cast<RegionCursor*>(Span.OwnerRegion);
            if (!owner || !owner->Region.isValid())
            {
                return {};
            }

            const std::size_t offset = static_cast<std::size_t>(Span.Base - owner->Region.Base);
            if (!VirtualMemoryManager::commit(owner->Region, offset, Span.getBytes()))
            {
                return {};
            }

            return Span;
        }

        for (RegionCursor& Cursor : Regions)
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
        Cursor.ReservedPages = reserveBytes / PageSize;

        Regions.push_back(std::move(Cursor));
        RegionCursor& back = Regions.back();

        PageSpan Span;
        Span.Base = back.Region.Base;
        Span.PageCount = PageCount;
        Span.PageSize = PageSize;
        Span.OwnerRegion = &back;
        back.NextPage = PageCount;

        if (!VirtualMemoryManager::commit(back.Region, 0, Span.getBytes()))
        {
            VirtualMemoryManager::release(back.Region);
            Regions.pop_back();
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
        if (!Span.isValid())
        {
            return;
        }

        std::lock_guard lock(Mutex);
        auto* owner = static_cast<RegionCursor*>(Span.OwnerRegion);
        if (owner && owner->Region.isValid() && Config.LazyCommit)
        {
            const std::size_t offset = static_cast<std::size_t>(Span.Base - owner->Region.Base);
            VirtualMemoryManager::decommit(owner->Region, offset, Span.getBytes());
        }

        FreeSpanCache[Span.PageCount].push_back(Span);
    }

private:
    struct RegionCursor
    {
        VirtualRegion Region;
        std::size_t ReservedPages = 0;
        std::size_t NextPage = 0;
    };

    void releaseAllRegions()
    {
        std::lock_guard lock(Mutex);
        for (RegionCursor& cursor : Regions)
        {
            VirtualMemoryManager::release(cursor.Region);
        }
        Regions.clear();
        FreeSpanCache.clear();
    }

    AllocatorConfig Config;
    std::size_t PageSize;
    std::mutex Mutex;
    std::deque<RegionCursor> Regions;
    std::unordered_map<std::size_t, std::vector<PageSpan>> FreeSpanCache;
};

} // namespace core::mem