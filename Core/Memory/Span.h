
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "virtual_memory_backend.h"
namespace core::mem
{

class Span
{

public:

	void reset() noexcept
	{
		Next = nullptr;
		Prev = nullptr;
		PoolNext = nullptr;

		BlockSize = 0;
		TotalBlocks = 0;
		FreeBlocks = 0;
		SizeClassIndex = InvalidSizeClass;
		BucketShardIndex = 0;

		FreeList = nullptr;
		SpanBase = nullptr;
		BackingSpan = {};

		IsInPartialList = false;
		IsInEmptyList = false;
		IsReleased = false;
	}

	void setup(
		const std::uint32_t InBlockSize,
		const std::uint16_t InTotalBlocks,
		const std::uint16_t InSizeClassIndex,
		const std::uint16_t InBucketShardIndex,
		std::byte* InSpanBase,
		const PageSpan& InBackingSpan) noexcept
	{
		BlockSize = InBlockSize;
		TotalBlocks = InTotalBlocks;
		FreeBlocks = InTotalBlocks;
		SizeClassIndex = InSizeClassIndex;
		BucketShardIndex = InBucketShardIndex;

		FreeList = nullptr;
		SpanBase = InSpanBase;
		BackingSpan = InBackingSpan;

		IsInPartialList = false;
		IsInEmptyList = false;
		IsReleased = false;

		std::byte* Block = SpanBase;
		for (std::uint16_t i = 0; i < TotalBlocks; ++i)
		{
			pushBlockToList(Block);
			Block += BlockSize;
		}
	}

	[[nodiscard]] void* popBlock() noexcept
	{
		if (!FreeList) [[unlikely]]
		{
			return nullptr;
		}

		void* Block = FreeList;
		FreeList = *reinterpret_cast<void**>(Block);
		--FreeBlocks;
		return Block;
	}
	void pushBlock(void* Block) noexcept
	{
		if (!Block) [[unlikely]]
		{
			return;
		}

		*reinterpret_cast<void**>(Block) = FreeList;
		FreeList = Block;
		if (FreeBlocks < TotalBlocks)
		{
			++FreeBlocks;
		}
	}
	void pushBatch(void* BatchHead, void* BatchTail, std::uint32_t BatchCount) noexcept
	{
		if (!BatchHead || !BatchTail || BatchCount == 0)
		{
			return;
		}

		*reinterpret_cast<void**>(BatchTail) = FreeList;
		FreeList = BatchHead;
		const std::uint32_t NextFreeBlocks = std::min<std::uint32_t>(
			static_cast<std::uint32_t>(TotalBlocks),
			static_cast<std::uint32_t>(FreeBlocks) + BatchCount);
		FreeBlocks = static_cast<std::uint16_t>(NextFreeBlocks);
	}
	void setPartial(bool v) noexcept { IsInPartialList = v; }
	void setEmpty(bool v) noexcept { IsInEmptyList = v; }
	void setReleased(bool v) noexcept { IsReleased = v; }

	[[nodiscard]] bool isFull() const noexcept { return FreeBlocks == 0; }
	[[nodiscard]] bool isEmpty() const noexcept { return FreeBlocks == TotalBlocks; }
	[[nodiscard]] bool isReleased() const noexcept { return IsReleased; }
	[[nodiscard]] bool isPartial() const noexcept { return FreeBlocks > 0 && FreeBlocks < TotalBlocks; }
	[[nodiscard]] bool isInPartialList() const noexcept { return IsInPartialList; }
	[[nodiscard]] bool isInEmptyList() const noexcept { return IsInEmptyList; }
	[[nodiscard]] std::uint16_t getSizeClassIndex() const noexcept { return SizeClassIndex; }
	[[nodiscard]] std::uint16_t getBucketShardIndex() const noexcept { return BucketShardIndex; }
	[[nodiscard]] std::uint32_t getBlockSize() const noexcept { return BlockSize; }
	[[nodiscard]] std::uint16_t getTotalBlocks() const noexcept { return TotalBlocks; }
	[[nodiscard]] std::uint16_t getFreeBlocks() const noexcept { return FreeBlocks; }
	[[nodiscard]] void* getFreeListHead() const noexcept { return FreeList; }
	[[nodiscard]] std::byte* getSpanBase() const noexcept { return SpanBase; }
	[[nodiscard]] const PageSpan& getBackingSpan() const noexcept { return BackingSpan; }
	[[nodiscard]] std::size_t getBytes() const noexcept { return static_cast<std::size_t>(TotalBlocks) * static_cast<std::size_t>(BlockSize); }
public:
	Span* Next = nullptr;
	Span* Prev = nullptr;
	Span* PoolNext = nullptr;

private:
	void pushBlockToList(std::byte* Block) noexcept
	{
		*reinterpret_cast<void**>(Block) = FreeList;
		FreeList = Block;
	}

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





}