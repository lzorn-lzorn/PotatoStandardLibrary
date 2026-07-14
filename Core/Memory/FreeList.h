
#pragma once

#include <cstddef>
#include <cstdint>

#include "Span.h"
namespace core::mem
{

class FreeList
{
public:
	void push(void* Block) noexcept
	{
		if (!Block) [[unlikely]]
		{
			return;
		}

		*reinterpret_cast<void**>(Block) = Head;
		Head = Block;
		++Count;
	}

	void* pop() noexcept
	{
		if (!Head) [[unlikely]]
		{
			return nullptr;
		}
		void* Block = Head;
		Head = *reinterpret_cast<void**>(Block);
		--Count;
		return Block;
	}

	void pushBatch(void* BatchHead, void* BatchTail, std::uint16_t BatchCount) noexcept
	{
		if (!BatchHead || !BatchTail || BatchCount == 0)
		{
			return;
		}

		*reinterpret_cast<void**>(BatchTail) = Head;
		Head = BatchHead;
		Count += BatchCount;
	}

	void* popBatch(std::uint16_t MaxCount, void*& Tail) noexcept
	{
		if (!Head || MaxCount == 0) [[unlikely]]
		{
			Tail = nullptr;
			return nullptr;
		}
		void* BatchHead = Head;
		void* Current = BatchHead;
		std::uint16_t Popped = 0;
		while (Current && Popped < MaxCount)
		{
			Tail = Current;
			Current = *reinterpret_cast<void**>(Current);
			++Popped;
		}
		*reinterpret_cast<void**>(Tail) = nullptr;
		Head = Current;
		Count -= Popped;
		return BatchHead;
	}

	bool isEmpty() const noexcept
	{
		return Head == nullptr;
	}

	std::uint16_t getCount() const noexcept
	{
		return Count;
	}

	std::uint16_t getMaxCount() const noexcept
	{
		return MaxCount;
	}

	void setMaxCount(const std::uint16_t Value) noexcept
	{
		MaxCount = Value;
	}

	Span* getSpanHint() const noexcept
	{
		return SpanHint;
	}

	void setSpanHint(Span* SpanHint) noexcept
	{
		this->SpanHint = SpanHint;
	}

	void clear() noexcept 
	{
		Head = nullptr;
		SpanHint = nullptr;
		Count = 0;
	}

private:
	void* Head { nullptr };
	std::uint16_t Count { 0 };
	std::uint16_t MaxCount { 0 };
	Span* SpanHint { nullptr };   // 缓存最近使用的 Span 对象
};

}