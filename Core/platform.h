
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


#ifdef __cpp_lib_hardware_interference_size
    inline constexpr size_t CacheLineSize = std::hardware_destructive_interference_size;
#else
    inline constexpr size_t CacheLineSize = 64;
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    // x86/x64 平台，用统一的 CPU_PAUSE
#	if defined(_MSC_VER)
#		include <intrin.h>
#		define CPU_RELAX _mm_pause()
#	else
#		define CPU_RELAX __builtin_ia32_pause()
#	endif
#elif defined(__arm__) || defined(__aarch64__)
#	define CPU_RELAX asm volatile("yield")
#else
#	define CPU_RELAX std::this_thread::yield()
#endif

#if defined(_MSC_VER)
#   define FORCE_INLINE __forceinline
#   define INTERFACE __declspec(novtable)
#else
#   define FORCE_INLINE inline __attribute__((always_inline))
#   define INTERFACE
#endif

/**
 * @example
 *      struct INTERFACE Foo {
 *          virtual void f() = 0;
 *          virtual ~Foo() = default;
 *      };
 */
