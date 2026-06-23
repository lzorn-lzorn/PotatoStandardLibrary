#pragma once

#include <chrono>
#include <concepts>
#include <string>
#include <charconv>
#include <stdexcept>
#include <type_traits>

namespace core
{

using steady_time_point_t = std::chrono::steady_clock::time_point;

/**
 * @brief 将 steady_clock 时间点序列化为秒级整数字符串。
 * @param tp 要序列化的时间点
 * @param precision_tag 精度标签，类型为 std::chrono::seconds（值忽略，仅用于重载决议）
 * @return 自 epoch 以来的秒数，格式为十进制整数字符串。
 * @usage
 * >  auto tp = std::chrono::steady_clock::now();
 * >  std::string s = serialize(tp, std::chrono::seconds{});
 * @note 返回值为有符号整数，能够处理 epoch 之前的时间点（负 duration）。
 */
static inline std::string serialize(const steady_time_point_t& tp, std::chrono::seconds /*precision_tag*/)
{
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
    return std::to_string(secs);
}

/**
 * @brief 将 steady_clock 时间点序列化为毫秒级整数字符串。
 * @param tp 要序列化的时间点
 * @param precision_tag 精度标签，类型为 std::chrono::milliseconds（值忽略，仅用于重载决议）
 * @return 自 epoch 以来的毫秒数，格式为十进制整数字符串。
 * @usage
 * >  auto tp = std::chrono::steady_clock::now();
 * >  std::string s = serialize(tp, std::chrono::milliseconds{});
 * @note 返回值为有符号整数，毫秒精度足以覆盖绝大多数日志场景。
 */
static inline std::string serialize(const steady_time_point_t& tp, std::chrono::milliseconds /*precision_tag*/)
{
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
    return std::to_string(ms);
}

// ============================================================================
// deserialize — 字符串反序列化为时间点（模板指定精度）
// ============================================================================

namespace details {
    // 从字符串解析整数的通用辅助函数
    template <typename Rep>
    static Rep parse_duration_rep(const std::string& str, const char* err_msg)
    {
        Rep val = 0;
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), val);
        if (ec == std::errc::invalid_argument)
            throw std::invalid_argument(err_msg);
        if (ec == std::errc::result_out_of_range)
            throw std::overflow_error(err_msg);
        return val;
    }
}

/**
 * @brief 将秒级整数字符串反序列化为 steady_clock 时间点。
 * @tparam Precision 精度类型，应为 std::chrono::seconds
 * @param str 序列化产生的秒数字符串（例如 "123456"）
 * @return 对应的 steady_clock::time_point
 * @usage
 * >  std::string s = serialize(tp, std::chrono::seconds{});
 * >  auto tp2 = deserialize<std::chrono::seconds>(s);
 * @note 解析失败抛出 std::invalid_argument 或 std::overflow_error。
 */
template <typename Precision>
	requires std::same_as<Precision, std::chrono::seconds>
static inline steady_time_point_t deserialize(const std::string& str)
{
    using Duration = std::chrono::seconds;
    auto val = details::parse_duration_rep<Duration::rep>(str, "deserialize<seconds>: invalid string");
    return steady_time_point_t(Duration(val));
}

/**
 * @brief 将毫秒级整数字符串反序列化为 steady_clock 时间点。
 * @tparam Precision 精度类型，应为 std::chrono::milliseconds
 * @param str 序列化产生的毫秒数字符串（例如 "123456789"）
 * @return 对应的 steady_clock::time_point
 * @usage
 * >  std::string s = serialize(tp, std::chrono::milliseconds{});
 * >  auto tp2 = deserialize<std::chrono::milliseconds>(s);
 * @note 解析失败抛出 std::invalid_argument 或 std::overflow_error。
 */
template <typename Precision>
	requires std::same_as<Precision, std::chrono::milliseconds>
static inline steady_time_point_t deserialize(const std::string& str)
{
    using Duration = std::chrono::milliseconds;
    auto val = details::parse_duration_rep<Duration::rep>(str, "deserialize<milliseconds>: invalid string");
    return steady_time_point_t(Duration(val));
}

}