
#pragma once 

#include <cstddef>
#include <cstdint>

#include "virtual_memory_backend.h"
namespace core::mem
{

class Span
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





}