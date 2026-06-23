
#pragma once
// #include <iostream>
#include <chrono>
#include <string>
#include <charconv>
#include <stdexcept>

namespace core
{
/**
 * Full Time String: "year-month-day hours:mins:seconds:milliseconds"
 * Short Time String: "hours:mins:seconds:milliseconds"
 */


using system_time_point_t = std::chrono::system_clock::time_point;



namespace details
{

}


/**
 * @brief 将 steady_clock 时间点序列化为秒级整数字符串。
 * @param tp 要序列化的时间点
 * @param full  true: 在 hours:mins:seconds 前追加 "year-month-day " 前缀(有空格)
 * @param milli true: 在 hours:mins:seconds 后追加 ":milliseconds" 后缀
 * @return 自 epoch 以来的秒数，格式为十进制整数字符串。
 * @usage
 * >  auto tp = std::chrono::steady_clock::now();
 * >  std::string s = serialize(tp, std::chrono::seconds{});
 * @note 返回值为有符号整数，能够处理 epoch 之前的时间点(负 duration)。
 */
inline std::string serialize(const system_time_point_t& tp, bool full = false, bool milli = false)
{
    using namespace std::chrono;
    const auto day = floor<days>(tp);
    const auto ymd = year_month_day{ day };

    if (milli)
    {
        // 需要毫秒精度 → 使用 milliseconds
        const auto time = hh_mm_ss{ duration_cast<milliseconds>(tp - day) };
        if (full)
            return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}:{:03}",
                static_cast<int>(ymd.year()),
                static_cast<unsigned>(ymd.month()),
                static_cast<unsigned>(ymd.day()),
                time.hours().count(),
                time.minutes().count(),
                time.seconds().count(),
                time.subseconds().count());
        else
            return std::format("{:02}:{:02}:{:02}:{:03}",
                time.hours().count(),
                time.minutes().count(),
                time.seconds().count(),
                time.subseconds().count());
    }
    else
    {
        // 仅需秒精度 → 使用 seconds
        const auto time = hh_mm_ss{ duration_cast<seconds>(tp - day) };
        if (full)
            return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}",
                static_cast<int>(ymd.year()),
                static_cast<unsigned>(ymd.month()),
                static_cast<unsigned>(ymd.day()),
                time.hours().count(),
                time.minutes().count(),
                time.seconds().count());
        else
            return std::format("{:02}:{:02}:{:02}",
                time.hours().count(),
                time.minutes().count(),
                time.seconds().count());
    }
}

/**
 * @brief 将 system_clock 时间点序列化为秒级时间戳字符串。
 * @param tp 要序列化的时间点
 * @return 自 epoch 以来的毫秒数，格式为十进制整数字符串。
 * @usage
 * >  auto tp = std::chrono::system_clock::now();
 * >  std::string s = serialize_to_timestamp_s(tp);
 * @note 返回值为有符号整数，毫秒精度足以覆盖绝大多数日志场景。
 */
inline std::string serialize_to_timestamp_s(const system_time_point_t& tp)
{
    const auto s = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
    return std::to_string(s);
}

/**
 * @brief 将 system_clock 时间点序列化为秒级时间戳字符串。
 * @param tp 要序列化的时间点
 * @return 自 epoch 以来的毫秒数，格式为十进制整数字符串。
 * @usage
 * >  auto tp = std::chrono::system_clock::now();
 * >  std::string s = serialize_to_timestamp_s(tp);
 * @note 返回值为有符号整数，毫秒精度足以覆盖绝大多数日志场景。
 */
    inline std::string serialize_to_timestamp_ms(const system_time_point_t& tp)
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
    return std::to_string(ms);
}
// ============================================================================
// deserialize — 字符串反序列化为时间点（模板指定精度）
// ============================================================================

namespace details {
    // 从字符串解析整数的通用辅助函数
    template <typename Rep>
    static Rep Parse_Duration_Rep(const std::string& str, const char* err_msg)
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
 * @brief 将以下类型字符串正确反序列化为 system_clock::time_point 对象
 *     1. "year-month-day hours:mins:seconds:milliseconds"
 *     2. "hours:mins:seconds:milliseconds"
 *     3. "year-month-day hours:mins:seconds"
 *     4. "hours:mins:seconds"
 * @param str 序列化产生的秒数字符串
 * @return 对应的 system_clock::time_point
 * @usage
 * >  std::string s = serialize(tp, std::chrono::seconds{});
 * >  auto tp2 = deserialize<std::chrono::seconds>(s);
 * @note 解析失败抛出 std::invalid_argument
 */
inline system_time_point_t deserialize(const std::string& str)
{

}

/**
 * @brief 将以下类型字符串正确反序列化为 system_clock::time_point 对象
 *     1. "year-month-day hours:mins:seconds:milliseconds"
 *     2. "hours:mins:seconds:milliseconds"
 *     3. "year-month-day hours:mins:seconds"
 *     4. "hours:mins:seconds"
 * @param str 序列化产生的秒数字符串
 * @return 对应的 system_clock::time_point; 解析失败抛出 std::nullopt
 * @usage
 * >  std::string s = serialize(tp, std::chrono::seconds{});
 * >  auto tp2 = deserialize<std::chrono::seconds>(s);
 * @note
 */
inline std::optional<system_time_point_t> safe_deserialize(const std::string& str)
{
    using namespace std::chrono;
    
    std::istringstream iss(str);
    int first;
    if (!(iss >> first))
    {
        return std::nullopt;
    }

    try {
        if (iss.peek() == '-') {   // 格式 1 或 3：包含日期
            int month_, day_, hour_, min_, sec_, ms_ = 0;
            char dash1, dash2, space, colon1, colon2;
            
            iss >> dash1 >> month_ >> dash2 >> day_;
            iss.get(space);
            iss >> hour_ >> colon1 >> min_ >> colon2 >> sec_;
            if (iss.fail() || dash1 != '-' || dash2 != '-' || space != ' ' 
                || colon1 != ':' || colon2 != ':')
            {
                return std::nullopt;
            }


            // 可选的毫秒部分
            if (iss.peek() == ':')
            {
                char colon3;
                iss >> colon3 >> ms_;
                if (iss.fail() || colon3 != ':') return std::nullopt;
            }

            // 检查尾部无多余字符
            iss >> std::ws;
            if (iss.peek() != std::istringstream::traits_type::eof())
            {
                return std::nullopt;
            }

            if (hour_ < 0 || hour_ > 23 || min_ < 0 || min_ > 59 || sec_ < 0 || sec_ > 59 || ms_ < 0 || ms_ > 999)
            {
                return std::nullopt;
            }

            auto ymd = year{first} / month{static_cast<unsigned>(month_)} / day{static_cast<unsigned>(day_)};
            if (!ymd.ok()) return std::nullopt;
            auto sys_day = sys_days{ymd};
            auto time = hours{hour_} + minutes{min_} + seconds{sec_} + milliseconds{ms_};
            return sys_day + time;

        } else { // 格式 2 或 4：仅时间
            int hour = first, min, sec, ms = 0;
            char colon1, colon2;
            
            iss >> colon1 >> min >> colon2 >> sec;
            if (iss.fail() || colon1 != ':' || colon2 != ':')
            {
                return std::nullopt;
            }

            if (iss.peek() == ':')
            {
                char colon3;
                iss >> colon3 >> ms;
                if (iss.fail() || colon3 != ':') return std::nullopt;
            }

            iss >> std::ws;
            if (iss.peek() != std::istringstream::traits_type::eof())
            {
                return std::nullopt;
            }

            if (hour < 0 || hour > 23 || min < 0 || min > 59 ||
                sec < 0 || sec > 59 || ms < 0 || ms > 999)
            {
                return std::nullopt;
            }
            auto time = hours{hour} + minutes{min} + seconds{sec} + milliseconds{ms};
            if (time >= 24h) return std::nullopt;   // 小时不得超出 23

            // 日期默认使用当前系统日期（天为单位）
            auto today = floor<days>(system_clock::now());
            return today + time;
        }
    } catch (...) {
        return std::nullopt;   // 构造 year_month_day 或 sys_days 可能抛异常
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
inline system_time_point_t deserialize_from_timestamp_s(const std::string& str)
{
    using Duration = std::chrono::seconds;
    auto val = details::Parse_Duration_Rep<Duration::rep>(str, "deserialize<seconds>: invalid string");
    return system_time_point_t(Duration(val));
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
inline system_time_point_t deserialize_from_timestamp_ms(const std::string& str)
{
    using Duration = std::chrono::milliseconds;
    auto val = details::Parse_Duration_Rep<Duration::rep>(str, "deserialize<milliseconds>: invalid string");
    return system_time_point_t(Duration(val));
}

}

//
// int main()
// {
//     using namespace std::chrono;
//     using namespace core;
//
//     // 准备一个固定的 system_clock 时间点：2025-06-23 14:30:15.123 UTC（假设）
//     // 我们通过构造一个确定的 time_point 来避免依赖系统时间
//     auto test_tp = [] {
//         auto ymd = year{2025} / June / 23d;
//         auto sys_day = sys_days{ymd};
//         auto time = hours{14} + minutes{30} + seconds{15} + milliseconds{123};
//         return sys_day + time;
//     }();
//
//     // ========================= serialize 测试 =========================
//     // 测试 full=false, milli=false  -> "14:30:15"
//     {
//         auto s = serialize(test_tp, false, false);
//         std::cout << "serialize(full=false, milli=false): " << s << std::endl;
//         assert(s == "14:30:15");
//     }
//
//     // 测试 full=false, milli=true   -> "14:30:15:123"
//     {
//         auto s = serialize(test_tp, false, true);
//         std::cout << "serialize(full=false, milli=true): " << s << std::endl;
//         assert(s == "14:30:15:123");
//     }
//
//     // 测试 full=true, milli=false   -> "2025-06-23 14:30:15"
//     {
//         auto s = serialize(test_tp, true, false);
//         std::cout << "serialize(full=true, milli=false): " << s << std::endl;
//         assert(s == "2025-06-23 14:30:15");
//     }
//
//     // 测试 full=true, milli=true    -> "2025-06-23 14:30:15:123"
//     {
//         auto s = serialize(test_tp, true, true);
//         std::cout << "serialize(full=true, milli=true): " << s << std::endl;
//         assert(s == "2025-06-23 14:30:15:123");
//     }
//
//     // 测试毫秒为 0 的情况
//     {
//         auto tp0ms = sys_days{year{2025}/June/23d} + hours{1} + minutes{2} + seconds{3};
//         auto s = serialize(tp0ms, false, true);
//         std::cout << "serialize(0ms): " << s << std::endl;
//         assert(s == "01:02:03:000");
//     }
//
//     // ========================= serialize_to_timestamp_s / ms 测试 =========================
//     // 已知 test_tp 对应的 epoch 秒数（手动计算或依赖系统，这里使用相对方法）
//     // 我们通过反序列化验证往返正确性，见后文
//
//     // ========================= safe_deserialize 测试 =========================
//     // 测试完整格式带毫秒
//     {
//         auto opt = safe_deserialize("2025-06-23 14:30:15:123");
//         assert(opt.has_value());
//         assert(*opt == test_tp);
//         std::cout << "safe_deserialize full with ms: OK" << std::endl;
//     }
//
//     // 测试完整格式不带毫秒
//     {
//         auto tp_no_ms = sys_days{year{2025}/June/23d} + hours{14} + minutes{30} + seconds{15};
//         auto opt = safe_deserialize("2025-06-23 14:30:15");
//         assert(opt.has_value());
//         assert(*opt == tp_no_ms);
//         std::cout << "safe_deserialize full without ms: OK" << std::endl;
//     }
//
//     // 测试短格式带毫秒（日期使用今天，但我们无法精确预测 today，只测试是否成功解析并检查时间部分）
//     {
//         auto opt = safe_deserialize("14:30:15:123");
//         assert(opt.has_value());
//         // 提取时间部分比较（忽略日期差异）
//         auto tp = *opt;
//         auto time_part = tp - floor<days>(tp);
//         auto expected_time = hours{14} + minutes{30} + seconds{15} + milliseconds{123};
//         assert(time_part == expected_time);
//         std::cout << "safe_deserialize short with ms: OK (time matched)" << std::endl;
//     }
//
//     // 测试短格式不带毫秒
//     {
//         auto opt = safe_deserialize("14:30:15");
//         assert(opt.has_value());
//         auto time_part = (*opt) - floor<days>(*opt);
//         auto expected_time = hours{14} + minutes{30} + seconds{15};
//         assert(time_part == expected_time);
//         std::cout << "safe_deserialize short without ms: OK" << std::endl;
//     }
//
//     // 错误输入测试
//     {
//         auto opt = safe_deserialize("invalid");
//         assert(!opt.has_value());
//         std::cout << "safe_deserialize invalid string: nullopt" << std::endl;
//     }
//     {
//         auto opt = safe_deserialize("2025-13-01 12:00:00"); // 无效月份
//         assert(!opt.has_value());
//         std::cout << "safe_deserialize bad month: nullopt" << std::endl;
//     }
//     {
//         auto opt = safe_deserialize("2025-06-23 25:00:00"); // 无效小时
//         assert(!opt.has_value());
//         std::cout << "safe_deserialize bad hour: nullopt" << std::endl;
//     }
//     {
//         auto opt = safe_deserialize("12:60:00"); // 分钟超范围，但我们的解析不会检查分钟/秒范围（小时有24h检查，但分秒未检查）
//         // 实际上，chrono::minutes{60} 合法，会转换为1小时。根据设计，如果希望严格限制，可能需增强，但目前未做。
//         // 我们仅检查不抛出，并接受其转换为合法 time_point。
//         // 但为了展示，可以注释掉，或者不检查。
//         // 不过分秒超范围在chrono构造中是合法的（duration不限制范围），因此不会返回nullopt。
//         // 这里仅测试小时>=24的情况。
//         // auto opt = safe_deserialize("24:00:00");
//         assert(!opt.has_value());
//         std::cout << "safe_deserialize hour >= 24: nullopt" << std::endl;
//     }
//     {
//         // 多余字符
//         auto opt = safe_deserialize("2025-06-23 14:30:15 extra");
//         assert(!opt.has_value());
//         std::cout << "safe_deserialize trailing chars: nullopt" << std::endl;
//     }
//
//     // ========================= deserialize_from_timestamp_s / ms 测试 =========================
//     // 测试往返：先序列化为时间戳，再反序列化
//     {
//         auto ts_s = serialize_to_timestamp_s(test_tp);
//         auto tp_restored = deserialize_from_timestamp_s(ts_s);
//         // 恢复的精度是秒，应等于 test_tp 去掉毫秒部分
//         auto expected = floor<seconds>(test_tp);
//         assert(tp_restored == expected);
//         std::cout << "timestamp s roundtrip: OK" << std::endl;
//     }
//     {
//         auto ts_ms = serialize_to_timestamp_ms(test_tp);
//         auto tp_restored = deserialize_from_timestamp_ms(ts_ms);
//         // 毫秒精度往返应完全一致
//         assert(tp_restored == test_tp);
//         std::cout << "timestamp ms roundtrip: OK" << std::endl;
//     }
//
//     // 测试错误输入
//     try {
//         deserialize_from_timestamp_s("not_a_number");
//         assert(false && "Should have thrown");
//     } catch (const std::invalid_argument&) {
//         std::cout << "deserialize_from_timestamp_s invalid argument: exception caught" << std::endl;
//     } catch (const std::overflow_error&) {
//         assert(false && "wrong exception type");
//     }
//
//     // 测试大数（不会溢出但远大于当前 epoch）
//     {
//         auto large_s = std::to_string(std::numeric_limits<long long>::max());
//         try {
//             auto tp = deserialize_from_timestamp_s(large_s);
//             // 能构造就算成功（可能超范围但在编译器中可能抛出异常或产生很大时间点）
//             std::cout << "deserialize_from_timestamp_s huge value: OK (no throw)" << std::endl;
//         } catch (const std::overflow_error&) {
//             std::cout << "deserialize_from_timestamp_s huge value: overflow_error caught" << std::endl;
//         }
//     }
//
//     std::cout << "\nAll tests passed!\n";
//     return 0;
// }
