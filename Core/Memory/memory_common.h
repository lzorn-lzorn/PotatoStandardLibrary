
#pragma once
#include <cstddef>
#include <cstdint>

namespace core::mem
{
enum class AllocationStage : uint16_t
{
	Prepare    = 0, // 准备阶段
	Layout     = 1, // 布局阶段
	Allocate   = 2, // 分配阶段
	Initialize = 3, // 初始化阶段
	Validate   = 4, // 验证阶段
	Notify     = 5, // 通知阶段
	Return     = 6, // 返回阶段
	Free       = 7, // 释放阶段
	Destroy    = 8, // 销毁阶段
};
enum class MemoryEventType : uint8_t
{
    Allocate,
    Deallocate,
    Reallocate,

    Reserve,
    Commit,
    Decommit,
    Release,

    ThreadCacheHit,
    ThreadCacheMiss,

    CentralFetch,
    CentralReturn,

    PageAllocate,
    PageFree,

    ArenaCreate,
    ArenaDestroy
};



}