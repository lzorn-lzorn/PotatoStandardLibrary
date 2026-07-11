#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "central_pool.h"

namespace core::mem
{

struct FreeList
{
	void* Head = nullptr;
	std::uint16_t Count = 0;
	std::uint16_t MaxCount = 0;
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
		, RefillBatch(Config.RefillBatchSize)
	{
		const std::size_t BytesPerClass = std::max<std::size_t>(
			256,
			Config.ThreadCacheMaxBytes / NumSizeClasses);

		for (std::size_t i = 0; i < NumSizeClasses; ++i)
		{
			const std::size_t BlockSize = classIndexToSize(static_cast<std::uint16_t>(i));
			const std::size_t countFromBytes = std::max<std::size_t>(8, BytesPerClass / BlockSize);
			const std::size_t capped = std::min<std::size_t>(countFromBytes, Config.ThreadCacheMaxPerClass);
			FreeLists[i].MaxCount = static_cast<std::uint16_t>(capped);
		}
	}

	ThreadCache(const ThreadCache&) = delete;
	ThreadCache& operator=(const ThreadCache&) = delete;

	[[nodiscard]] ThreadCacheAllocation allocate(
		const std::uint16_t SizeClassIndex,
		const std::size_t BlockSize)
	{
		if (SizeClassIndex >= NumSizeClasses)
		{
			return {};
		}

		FreeList& FreeList = FreeLists[SizeClassIndex];
		if (FreeList.Head)
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
		const std::uint32_t target = std::min<std::uint32_t>(RefillBatch, MaxBatch);
		const std::uint32_t fetched = CentralPool->fetchBatch(SizeClassIndex, Batch, target);
		if (fetched == 0)
		{
			return {};
		}

		for (std::uint32_t i = 1; i < fetched; ++i)
		{
			pushBlock(FreeList.Head, Batch[i]);
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
		if (SizeClassIndex >= NumSizeClasses || !Ptr)
		{
			return;
		}

		FreeList& FreeList = FreeLists[SizeClassIndex];
		pushBlock(FreeList.Head, Ptr);
		++FreeList.Count;
		CurrentBytes += BlockSize;

		if (FreeList.Count > FreeList.MaxCount || CurrentBytes > MaxBytes)
		{
			drainToCentral(SizeClassIndex, BlockSize, FreeList.MaxCount / 2 + 1);
		}
	}

	void flushAll()
	{
		for (std::size_t i = 0; i < NumSizeClasses; ++i)
		{
			const std::size_t BlockSize = classIndexToSize(static_cast<std::uint16_t>(i));
			drainToCentral(static_cast<std::uint16_t>(i), BlockSize, FreeLists[i].Count);
		}
	}

	[[nodiscard]] std::size_t getCurrentBytes() const noexcept
	{
		return CurrentBytes;
	}

private:
	void drainToCentral(
		const std::uint16_t SizeClassIndex,
		const std::size_t BlockSize,
		const std::uint16_t RequestedCount)
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
			CentralPool->returnBatch(SizeClassIndex, Chain, Drained);
		}
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

	CentralPool* CentralPool = nullptr;
	std::array<FreeList, NumSizeClasses> FreeLists{};
	std::size_t CurrentBytes = 0;
	std::size_t MaxBytes = 0;
	std::uint16_t RefillBatch = 0;
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
