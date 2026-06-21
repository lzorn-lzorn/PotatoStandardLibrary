
#pragma once

#include <new>

#if defined(__cpp_assume) && __cpp_assume >= 202207L
#	define ASSUME(expr) [[assume(expr)]]
#else
#	ifdef _MSC_VER
#		define ASSUME(expr) __assume(expr)
#	elif defined(__clang__) || defined(__GNUC__)
#		if __has_builtin(__builtin_assume)
#			define ASSUME(expr) __builtin_assume(expr)
#		else
#			define ASSUME(expr) do { if (!(expr)) __builtin_unreachable(); } while(0)
#		endif
#	else
#		define ASSUME(expr)
#	endif
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#	define CPU_RELAX asm volatile("pause" ::: "memory")
#else
#	define CPU_RELAX std::this_thread::yield()
#endif

// Cache Line 大小常量, 用于对齐以避免伪共享 (false sharing).
// 伪共享: 两个无关的原子变量位于同一 Cache Line, 一个线程修改其中一个时,
// 会导致其他线程对同一 Cache Line 上另一个变量的缓存失效.
#ifdef __cpp_lib_hardware_interference_size
    inline constexpr size_t CacheLineSize = std::hardware_destructive_interference_size;
#else
    inline constexpr size_t CacheLineSize = 64;
#endif