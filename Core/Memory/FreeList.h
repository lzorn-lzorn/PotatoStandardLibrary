
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
		return nullptr;
	}

	void pushBatch(void* BatchHead, void* BatchTail, std::uint16_t BatchCount) noexcept
	{

	}

	void* popBatch(std::uint16_t MaxCount, void*& Tail) noexcept
	{

	}

	bool isEmpty() const noexcept
	{
		return true;
	}

	std::uint16_t getCount() const noexcept
	{
		return 0;
	}

	std::uint16_t getMaxCount() const noexcept
	{
		return 0;
	}

	Span* getSpanHint() const noexcept
	{
		return nullptr;
	}

	void setSpanHint(Span* SpanHint) noexcept
	{

	}

	void clear() noexcept 
	{

	}

private:
	void* Head { nullptr };
	std::uint16_t Count { 0 };
	std::uint16_t MaxCount { 0 };
	Span* SpanHint { nullptr };   // 缓存最近使用的 Span 对象
};


}