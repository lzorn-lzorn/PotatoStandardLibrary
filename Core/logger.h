#pragma once 

#include <array>
#include <chrono>
#include <source_location>
#include <string_view>
#include <format>
#include <filesystem>
#include <fstream>
#include <deque>
#include <mutex>
#include <functional>
#include <map>
#include <thread>

#include "common.h"
#include "buffer.h"

#ifndef NDEBUG // release 模式
#	define BufferSize 64 * 1024 //64 KB
#else
#	define BufferSize 256 * 1024 // 256kb
#endif
namespace core::component 
{
static inline std::atomic<unsigned long long> report_uid = 1;
static inline std::atomic<unsigned long long> message_uid = 1;

enum class level : uint8_t
{
	Temp,
	Info,
	Warning,
	Error,
};


template <class Derived>
struct category
{
	
	constexpr std::string_view name() noexcept {
		// 基类(Derived)必须要有 category_name 成员, 
		// 且必须是可转换为 std::string_view 的静态常量表达式,
		// 相当于一个简单的反射机制
		static_assert(
			requires { { Derived::category_name } -> std::convertible_to<std::string_view>; },
			"Derived category must have a static constexpr category_name member convertible to std::string_view"
		);

        return Derived::category_name;
    }
};

struct default_category : public category<default_category>
{
	static constexpr std::string_view category_name = "default";
};

class message
{
public:
	template <typename CategoryTy = default_category>
	static message make_message(uint64_t id, level lv, category<CategoryTy> cat, std::source_location called_loc,std::string_view msg) 
	{
		return message(id, lv, cat.name(), called_loc, msg);
	}
	
	message() = default;
	~message() = default;
	message(const message&) = default;
	message& operator=(const message&) = default;
	message(message&&) noexcept = default;
	message& operator=(message&&) noexcept = default;
	
	static std::string serialize(const message& rec)
	{
		// [{level}:{category}](called_loc:time) {message}
		
	}

	static message deserialize(const std::string& str)
	{

	}

private:
	message(uint64_t id, level lv, std::string_view cat, std::source_location called_loc,std::string_view msg)
		: m_level(lv)
		, m_category_name(cat)
		, m_id(id)
		, m_called_loc(called_loc)
		, m_message_text(msg)
		, m_time(std::chrono::steady_clock::now())
	{}
private:
	level m_level { level::Temp };
	std::string_view m_category_name {  };
	uint64_t m_id { 0 };
	std::source_location m_called_loc { std::source_location::current() };
	std::string_view m_message_text;
	std::chrono::steady_clock::time_point m_time;
};


class logger
{
	using sink_callback = std::function<void(const message&)>;
public:
	static logger& self() {
		static logger instance;
		return instance;
	}


private:
	logger() = default;
	~logger() = default;
	logger(const logger&) = delete;
	logger& operator=(const logger&) = delete;
	logger(logger&&) = delete;
	logger& operator=(logger&&) = delete;

private:
	ring_buffer<message, std::allocator<message>, ring_buffer_policy::MPSC> m_message_buffer;
	double_buffer<message, BufferSize> m_message_double_buffer;
	dynamic_array<std::string> m_sinks;
	std::jthread m_worker_thread;
	std::jthread m_file_writer_thread;

};

}