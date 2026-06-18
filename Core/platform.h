
#pragma once

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