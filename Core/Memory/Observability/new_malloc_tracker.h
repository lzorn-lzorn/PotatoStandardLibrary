#pragma once

#include <cstddef>
#include <cstdint>

#include "memory_tracker.h"

#if defined(__GNUC__) || defined(__clang__)
#define CORE_MEM_HOOK_RETURN_ADDRESS __builtin_extract_return_addr(__builtin_return_address(0))
#elif defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#define CORE_MEM_HOOK_RETURN_ADDRESS _ReturnAddress()
#else
#define CORE_MEM_HOOK_RETURN_ADDRESS ((void*)0)
#endif

namespace core::mem
{

using LegacyAllocOp = HookAllocationOp;
inline constexpr std::size_t LegacyUnknownSize = MemoryTrackerUnknownSize;

} // namespace core::mem
