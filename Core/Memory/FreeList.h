
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
		// 找到 Block 所指向的内存地址，并把这块内存当作一个 void* 类型的变量来使用
		// 把变量 Head 中的值, 拷贝到 Block 的前 8 个字节 里
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
		*reinterpret_cast<void**>(BatchTail) = Head;
		Head = BatchHead;
		Count += BatchCount;
	}

	void* popBatch(std::uint16_t MaxCount, void*& Tail) noexcept
	{
		if (!Head) [[unlikely]]
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