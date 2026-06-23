#pragma once 

#include <chrono>
#include <source_location>
#include <string_view>
#include <format>
#include "common.h"
#include "enum_flag.h"

namespace core::component 
{

enum class level : uint8_t
{
	temp,
	normal,
	warning,
	error,
};

enum class output : uint8_t
{
	std_buffer = 0x01,
	console = 0x02,
	file = 0x04,
	screen = 0x08,
};
using output_flags = flags<output>;

template <class Derived>
class category
{
	// 基类(Derived)必须要有 category_name 成员, 
    // 且必须是可转换为 std::string_view 的静态常量表达式,
	// 相当于一个简单的反射机制
	static_assert(
        requires { { Derived::category_name } -> std::convertible_to<std::string_view>; },
        "Derived category must have a static constexpr category_name member convertible to std::string_view"
    );

	constexpr std::string_view name() noexcept {
        return Derived::category_name;
    }
};

template <typename Category>
class record
{
	explicit record(uint64_t id, level lv, category<Category> cat, output_flags output, std::source_location called_loc,std::string_view msg)
		: m_level(lv)
		, m_category(cat)
		, m_output(output)
		, m_id(id)
		, m_called_loc(called_loc)
		, m_message(msg)
		, m_time(std::chrono::steady_clock::now())
	{}

	~record() = default;
	record(const record&) = delete;
	record& operator=(const record&) = delete;
	record(record&&) noexcept = default;
	record& operator=(record&&) noexcept = default;
	
	static std::string serialize(const record& rec)
	{
		// [{level}:{category}](called_loc:time) {message}
		return std::format("[{}:{}]({}:{}) {}", rec.m_level, rec.m_category.name(), rec.m_called_loc.file_name(), rec.m_time.time_since_epoch().count(), rec.m_message);
	}

	static record deserialize(const std::string& str)
	{

	}
private:
	level m_level { level::temp };
	category<Category> m_category {  };
	output_flags m_output { output::console | output::file };
	uint64_t m_id { 0 };
	std::source_location m_called_loc { std::source_location::current() };
	std::string_view m_message;
	std::chrono::steady_clock::time_point m_time;
};

class logger
{

};

}