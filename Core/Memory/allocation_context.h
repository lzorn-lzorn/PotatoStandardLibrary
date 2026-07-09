
#pragma once
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <concepts>
#include <memory>
#include <atomic>
#include <vector>
#include "memory_common.h"
namespace core::mem
{

/**
 * User 申请时首先会拿到一个 AllocationRequest, 基于 AllocationRequest
 * 得到整体的 AllocationContext. 具体来说:
 *
 * 包含内存分配请求、布局、元数据、运行时信息和分配结果。
 */


struct AllocationRequest
{
	size_t Size;
	size_t Alignment = alignof(std::max_align_t);
	bool IsZeroMemory = false; // 是否申请零初始化的内存
	bool IsNoThrow = false;    // 是否在分配失败时抛出异常
};

struct AllocationLayout
{
	// 用户真正申请的内存
	size_t RequestedSize      = 0; // 用户实际请求的内存大小
	size_t RequestedAlignment = 0; // 用户实际请求的内存对齐方式
	
	// 在进行 Layout 之后实际分配的内存
	size_t TotalSize          = 0; // 实际分配的内存大小
	size_t TotalAlignment     = 0; // 实际分配的内存对齐方式
	
	// Header 用于 Guard
	size_t HeaderSize         = 0; // 分配头部大小
	size_t FrontGuard         = 0; // 前置保护区大小
	size_t BackGuard          = 0; // 后置保护区大小
	
	// User 内存偏移量
	size_t UserOffset         = 0; // 用户内存偏移量
};

struct AllocationMetadata
{
	uint64_t AllocationId = 0; 
	uint32_t StackId      = 0;      
	uint32_t ThreadId     = 0;     
	uint32_t ResourceId   = 0;   
	uint32_t ArenaId      = 0;      
	uint64_t Timestamp    = 0;
	uint32_t FrameIndex   = 0;
};

struct AllocationRuntime
{
	std::byte* RawPtr = nullptr; // 原始内存指针
	std::byte* UserPtr = nullptr; // 用户内存指针
	void* Owner = nullptr; // 内存所有者指针
	bool IsFromThreadCache = false; // 是否来自线程缓存
	bool IsFromCentralPool = false; // 是否来自中央内存池
	bool IsFromOS = false; // 是否来自操作系统
};

struct AllocationResult
{
	bool IsSuccess = false; // 分配是否成功
	std::error_code ErrorCode; // 分配错误码
};

struct AllocationContext
{
	AllocationRequest Request;
	AllocationLayout Layout;
	AllocationMetadata Metadata;
	AllocationRuntime Runtime;
	AllocationResult Result;
	AllocationStage Stage = AllocationStage::Prepare;

	constexpr void reset()
	{
		Request = {};
		Layout = {};
		Metadata = {};
		Runtime = {};
		Result = {};
	}
};

struct MemoryEvent
{
	MemoryEventType Type;
	AllocationContext* Context;
};

template <typename Ty>
concept CMemoryObserver = requires(Ty Observer, const MemoryEvent& Event)
{
	{ Observer.onMemoryEvent(Event) } -> std::same_as<void>;
};

struct ObserverEntry
{
	void* Object;
	void (*Callback)(void* , const MemoryEvent&);
};

class MemoryEventBus
{
public:
	template <CMemoryObserver ObserverType>
	void bind(ObserverType& Observer)
	{
		ObserverEntry Entry;
		Entry.Object = &Observer;
		Entry.Callback = [](void* Obj, const MemoryEvent& Event)
		{
			static_cast<ObserverType*>(Obj)->onMemoryEvent(Event);
		};
		Observers.push_back(Entry);
	}

	template <CMemoryObserver ObserverType>
	void unbind(ObserverType& Observer)
	{

	}

	void emit(const MemoryEvent& Event)
	{
		for (const auto& Entry : Observers)
		{
			Entry.Callback(Entry.Object, Event);
		}
	}
private:
	/**
	 * 实际上, 由于 MemoryEventBus 是用于实现各种内存分配事件的观察者, 用于
	 * 统计, 监控, Debug 等功能, 所以实际上这里的读操作的频率远远高于写操作,
     * 因此, 写操作作为冷路径, 在实际修改 Observers 时, 生成一个新的 Snopshot,
     * 对于 emit 操作来说, 仅仅遍历 Snapshot.
	 */
	std::pmr::vector<ObserverEntry> Observers;
	std::atomic<std::shared_ptr<const std::vector<ObserverEntry>>> Snapshot;
};




} // namespace core::mem