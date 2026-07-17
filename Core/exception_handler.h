#pragma once
#include <exception>
#include <functional>
#include <source_location>
#include <iostream>
#include <string_view>

namespace core
{
struct default_exception_policy {
    static void handle(const std::exception& e,
                       std::source_location loc = std::source_location::current()) {
        std::cerr << "[EXCEPTION] " << loc.function_name() << " at "
                  << loc.file_name() << ":" << loc.line()
                  << "\n  what(): " << e.what() << '\n';
    }

    static void handle_unknown(
        std::source_location loc = std::source_location::current()) {
        std::cerr << "[UNKNOWN EXCEPTION] " << loc.function_name() << " at "
                  << loc.file_name() << ":" << loc.line() << '\n';
    }
};

template <typename Policy = default_exception_policy>
struct exception_policy : Policy {};

using current_policy = exception_policy<>;


namespace custom {
    inline std::function<void(const std::exception&, std::source_location)>
        on_exception = current_policy::handle;

    inline std::function<void(std::source_location)>
        on_unknown = current_policy::handle_unknown;
}

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
// 异常启用:
#   define TRY try
// 默认捕获：调用全局处理函数
#	define CATCH                                                \
		catch (...) {                                           \
			::custom::on_exception(std::current_exception(),    \
			std::source_location::current());                   \
		}

#	define CATCH_WITH(handler)                                           \
		catch (...)                                                      \
		{                                                                \
			(handler)(std::current_exception(),                          \
					std::source_location::current());                    \
		}

#	define TRY_CATCH(...)                                                \
		try {                                                            \
			__VA_ARGS__                                                  \
		} catch (const std::exception& e) {                              \
			::custom::on_exception(e, std::source_location::current());  \
		} catch (...) {                                                  \
			::custom::on_unknown(std::source_location::current());       \
		}

#	define TRY_CATCH_WITH(handler, ...)                                  \
        try {                                                            \
            __VA_ARGS__                                                  \
        } catch (const std::exception& e) {                              \
            (handler)(e, std::source_location::current());               \
        } catch (...) {                                                  \
            auto eptr = std::current_exception();                        \
            (handler)(eptr, std::source_location::current());            \
        }
#   define THROW(expr) throw expr
#else
#   define TRY
#   define CATCH
#   define CATCH_WITH(handler)
#   define TRY_CATCH(...)
#   define TRY_CATCH_WITH(handler, ...)
#   define THROW(expr) std::abort() // std::terminate() 本事也属于异常处理框架
#endif
struct scoped_exception_handler {
    using handler_t = std::function<void(const std::exception&, std::source_location)>;
    using unknown_t = std::function<void(std::source_location)>;

    handler_t old;
    unknown_t old_unknown;

    scoped_exception_handler(handler_t h, unknown_t u = nullptr)
        : old(std::move(custom::on_exception)),
          old_unknown(std::move(custom::on_unknown)) {
        custom::on_exception = std::move(h);
        if (u) custom::on_unknown = std::move(u);
    }

    ~scoped_exception_handler() {
        custom::on_exception = std::move(old);
        custom::on_unknown = std::move(old_unknown);
    }
};

}


/** @example
void risky_business(int x) {
    if (x == 0) throw std::runtime_error("x is zero");
    if (x < 0) throw 42;  // 非标准异常
}

void demo_default() {
    TRY_CATCH({
        risky_business(0);
        risky_business(-1);
    });
    // 输出两次错误日志，程序继续运行
}

void demo_custom() {
    // 自定义处理器：忽略所有异常并打印简洁信息
    auto ignore_all = []<typename E>(const E&, std::source_location loc) {
        std::cout << "Ignored error at " << loc.function_name() << '\n';
    };
    TRY_CATCH_WITH(ignore_all, {
        risky_business(0);
        risky_business(-1);
    });
}

void demo_scoped_custom() {
    // 作用域内全局覆盖默认处理器
    scoped_exception_handler guard(
        [](const std::exception& e, std::source_location loc) {
            std::cout << "Scoped: " << e.what() << " at " << loc.line() << '\n';
        },
        [](std::source_location loc) {
            std::cout << "Scoped unknown at " << loc.line() << '\n';
        }
    );

    TRY_CATCH({ risky_business(0); });
    // 此 TRY_CATCH 使用 guard 指定的处理逻辑
}

if (x < 0) TRY {
    throw std::runtime_error("negative");
} CATCH;


auto log_only = [](std::exception_ptr eptr, std::source_location loc) {
    try { if (eptr) std::rethrow_exception(eptr); }
    catch (const std::exception& e) { std::cout << "quiet: " << e.what() << '\n'; }
    catch (...) { std::cout << "quiet unknown\n"; }
};

if (x < 0) TRY {
    throw std::runtime_error("negative");
} CATCH_WITH(log_only);
*/