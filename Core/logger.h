#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "Containers/buffer.h"
#include "common.h"

namespace core::component
{
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
    constexpr std::string_view name() const noexcept
    {
        static_assert(
            requires {
                { Derived::category_name } -> std::convertible_to<std::string_view>;
            },
            "Derived category must have a static constexpr category_name member convertible to std::string_view");

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
    /**
     * @brief 构造一条完整日志消息。
     * @param id 全局消息唯一 ID。
     * @param lv 日志级别。
     * @param cat 日志分类。
     * @param called_loc 调用位置信息。
     * @param msg 日志文本。
     * @return 构造后的 message 对象。
     * @usage auto msg = message::make_message(1, level::Info, default_category{}, std::source_location::current(), "hello");
     */
    template <typename CategoryTy = default_category>
    static message make_message(
        uint64_t id,
        level lv,
        const category<CategoryTy>& cat,
        const std::source_location& called_loc,
        std::string_view msg)
    {
        return message(id, lv, cat.name(), called_loc, msg);
    }

    message() = default;
    ~message() = default;
    message(const message&) = default;
    message& operator=(const message&) = default;
    message(message&&) noexcept = default;
    message& operator=(message&&) noexcept = default;

    /**
     * @brief 获取日志级别。
     * @return 当前消息的日志级别。
     * @usage auto lv = msg.get_level();
     */
    [[nodiscard]] level get_level() const noexcept { return m_level; }
    /**
     * @brief 获取消息 ID。
     * @return 当前消息的全局 ID。
     * @usage auto id = msg.get_id();
     */
    [[nodiscard]] uint64_t get_id() const noexcept { return m_id; }
    /**
     * @brief 获取分类名。
     * @return 分类字符串视图。
     * @usage auto cat = msg.category_name();
     */
    [[nodiscard]] std::string_view category_name() const noexcept { return m_category_name; }
    /**
     * @brief 获取调用源文件名。
     * @return 文件名字符串视图。
     * @usage auto file = msg.file_name();
     */
    [[nodiscard]] std::string_view file_name() const noexcept { return m_file_name; }
    /**
     * @brief 获取调用函数名。
     * @return 函数名字符串视图。
     * @usage auto fn = msg.function_name();
     */
    [[nodiscard]] std::string_view function_name() const noexcept { return m_function_name; }
    /**
     * @brief 获取调用行号。
     * @return 行号。
     * @usage auto line = msg.line();
     */
    [[nodiscard]] uint_least32_t line() const noexcept { return m_line; }
    /**
     * @brief 获取调用列号。
     * @return 列号。
     * @usage auto col = msg.column();
     */
    [[nodiscard]] uint_least32_t column() const noexcept { return m_column; }
    /**
     * @brief 获取日志文本。
     * @return 日志文本字符串视图。
     * @usage auto text = msg.text();
     */
    [[nodiscard]] std::string_view text() const noexcept { return m_message_text; }
    /**
     * @brief 获取消息时间戳。
     * @return system_clock 时间点。
     * @usage auto ts = msg.timestamp();
     */
    [[nodiscard]] std::chrono::system_clock::time_point timestamp() const noexcept { return m_time; }

    /**
     * @brief 将消息序列化为一行文本。
     * @param rec 要序列化的消息。
     * @return 序列化后的字符串。
     * @usage auto line = message::serialize(msg);
     */
    [[nodiscard]] static std::string serialize(const message& rec)
    {
        const auto stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            rec.m_time.time_since_epoch())
                               .count();

        return std::format(
            "{}|{}|{}|{}|{}|{}|{}|{}|{}",
            rec.m_id,
            static_cast<unsigned>(rec.m_level),
            stamp,
            rec.m_line,
            rec.m_column,
            escape_field(rec.m_category_name),
            escape_field(rec.m_file_name),
            escape_field(rec.m_function_name),
            escape_field(rec.m_message_text));
    }

    /**
     * @brief 从序列化文本反序列化消息。
     * @param str 序列化字符串。
     * @return 反序列化后的 message。
     * @usage auto msg = message::deserialize(line);
     */
    [[nodiscard]] static message deserialize(const std::string& str)
    {
        const auto fields = split_escaped_fields(str);
        if (fields.size() != 9)
        {
            throw std::invalid_argument("message::deserialize invalid field count");
        }

        message out;
        out.m_id = std::stoull(fields[0]);
        out.m_level = static_cast<level>(std::stoul(fields[1]));
        const auto ns_since_epoch = static_cast<std::int64_t>(std::stoll(fields[2]));
        out.m_time = std::chrono::system_clock::time_point(
            std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(ns_since_epoch)));
        out.m_line = static_cast<uint_least32_t>(std::stoul(fields[3]));
        out.m_column = static_cast<uint_least32_t>(std::stoul(fields[4]));
        out.m_category_name = unescape_field(fields[5]);
        out.m_file_name = unescape_field(fields[6]);
        out.m_function_name = unescape_field(fields[7]);
        out.m_message_text = unescape_field(fields[8]);
        return out;
    }

private:
    message(
        uint64_t id,
        level lv,
        std::string_view cat,
        const std::source_location& called_loc,
        std::string_view msg)
        : m_level(lv)
        , m_category_name(cat)
        , m_id(id)
        , m_file_name(called_loc.file_name())
        , m_function_name(called_loc.function_name())
        , m_line(called_loc.line())
        , m_column(called_loc.column())
        , m_message_text(msg)
        , m_time(std::chrono::system_clock::now())
    {
    }

    static std::string escape_field(std::string_view input)
    {
        std::string output;
        output.reserve(input.size());
        for (const char ch : input)
        {
            switch (ch)
            {
            case '\\':
                output += "\\\\";
                break;
            case '|':
                output += "\\|";
                break;
            case '\n':
                output += "\\n";
                break;
            case '\r':
                output += "\\r";
                break;
            case '\t':
                output += "\\t";
                break;
            default:
                output.push_back(ch);
                break;
            }
        }
        return output;
    }

    static std::string unescape_field(std::string_view input)
    {
        std::string output;
        output.reserve(input.size());

        bool escaped = false;
        for (const char ch : input)
        {
            if (!escaped)
            {
                if (ch == '\\')
                {
                    escaped = true;
                }
                else
                {
                    output.push_back(ch);
                }
                continue;
            }

            switch (ch)
            {
            case 'n':
                output.push_back('\n');
                break;
            case 'r':
                output.push_back('\r');
                break;
            case 't':
                output.push_back('\t');
                break;
            default:
                output.push_back(ch);
                break;
            }
            escaped = false;
        }

        if (escaped)
        {
            output.push_back('\\');
        }

        return output;
    }

    static dynamic_array<std::string> split_escaped_fields(const std::string& line)
    {
        dynamic_array<std::string> out;
        std::string current;
        current.reserve(line.size());

        bool escaped = false;
        for (const char ch : line)
        {
            if (!escaped)
            {
                if (ch == '\\')
                {
                    escaped = true;
                    current.push_back(ch);
                }
                else if (ch == '|')
                {
                    out.push_back(current);
                    current.clear();
                }
                else
                {
                    current.push_back(ch);
                }
                continue;
            }

            escaped = false;
            current.push_back(ch);
        }

        out.push_back(current);
        return out;
    }

private:
    level m_level{level::Temp};
    std::string m_category_name;
    uint64_t m_id{0};
    std::string m_file_name;
    std::string m_function_name;
    uint_least32_t m_line{0};
    uint_least32_t m_column{0};
    std::string m_message_text;
    std::chrono::system_clock::time_point m_time{};
};

class logger
{
    using sink_callback = std::function<void(const message&)>;
    using sink_list = dynamic_array<sink_callback>;

public:
	/**
	 * @brief logger 运行模式。
	 * @usage mode() 返回当前模式，可用于引擎在启动阶段校验配置。
	 */
    enum class run_mode : uint8_t
    {
        BackgroundThread,
        ManualFramePump,
    };

    static constexpr std::size_t RingBufferCapacity = 8192;
    static constexpr std::size_t DefaultFrameBudgetMessages = 1024;
    static constexpr std::size_t DefaultBackgroundBatchMessages = 256;

    /**
     * @brief 获取 logger 单例实例。
     * @return logger 单例引用。
     * @usage auto& lg = logger::self();
     */
    static logger& self()
    {
        static logger instance;
        return instance;
    }

    /**
     * @brief 记录一条日志（自动采集调用位置）。
     * @param lvl 日志级别。
     * @param fmt 格式化模板。
     * @return
     * @usage logger::log(level::Info, "hp={} mp={}", hp, mp);
     */
    template <typename... Args>
    static void log(level lvl, std::format_string<Args...> fmt, Args&&... args)
    {
        self().log_with_location_impl(lvl, std::source_location::current(), fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief 记录一条日志（显式指定调用位置，常用于日志包装器）。
     * @param lvl 日志级别。
     * @param loc 调用位置。
     * @param fmt 格式化模板。
     * @return
     * @usage logger::log_with_location(level::Warning, loc, "network timeout id={}", id);
     */
    template <typename... Args>
    static void log_with_location(level lvl, const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args)
    {
        self().log_with_location_impl(lvl, loc, fmt, std::forward<Args>(args)...);
    }

    /**
     * @brief 以“手动帧泵”模式启动 logger。
     * @return 当前 logger 引用，便于链式调用。
     * @usage logger::self().start_manual();
     */
    logger& start_manual()
    {
        stop();

        bool expected = false;
        if (!m_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return *this;
        }

        m_mode.store(run_mode::ManualFramePump, std::memory_order_release);
        open_log_file();
        return *this;
    }

    /**
     * @brief 以后台线程模式启动 logger。
     * @return 当前 logger 引用，便于链式调用。
     * @usage logger::self().start_background();
     */
    logger& start_background()
    {
        stop();

        bool expected = false;
        if (!m_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return *this;
        }

        m_mode.store(run_mode::BackgroundThread, std::memory_order_release);
        open_log_file();
        m_worker_thread = std::jthread([this](std::stop_token stop_token) { worker_loop(stop_token); });
        return *this;
    }

	/**
	 * @brief 引擎每帧调用：在预算内批量消费并落盘。
	 * @param max_messages 本帧最多处理消息数。
	 * @param time_budget 本帧允许消耗的时间预算；为空则仅按消息数限制。
	 * @return true 表示本帧消费到了至少一条消息。
	 * @usage logger::self().pump_frame(512, std::chrono::microseconds(300));
	 */
    bool pump_frame(
        std::size_t max_messages = DefaultFrameBudgetMessages,
        std::optional<std::chrono::nanoseconds> time_budget = std::nullopt)
    {
        if (!m_running.load(std::memory_order_acquire))
        {
            return false;
        }

        const auto begin = std::chrono::steady_clock::now();
        std::size_t consumed = 0;

        while (consumed < max_messages)
        {
            if (!consume_once())
            {
                break;
            }
            ++consumed;

            if (time_budget.has_value() && (std::chrono::steady_clock::now() - begin) >= *time_budget)
            {
                break;
            }
        }

        flush_file_buffer();
        return consumed > 0;
    }

    /**
     * @brief 兼容接口，等价于 start_background()。
     * @return 当前 logger 引用。
     * @usage logger::self().start();
     */
    logger& start()
    {
        return start_background();
    }

    /**
     * @brief 停止 logger 并尽力冲刷剩余日志。
     * @return 当前 logger 引用。
     * @usage logger::self().stop();
     */
    logger& stop()
    {
        const bool was_running = m_running.exchange(false, std::memory_order_acq_rel);
        if (!was_running)
        {
            return *this;
        }

        if (m_worker_thread.joinable())
        {
            m_worker_thread.request_stop();
            m_worker_thread.join();
        }

        flush_remaining_messages();
        flush_file_buffer();

        if (m_log_file.is_open())
        {
            m_log_file.flush();
            m_log_file.close();
        }

        return *this;
    }

    /**
     * @brief 注册自定义 sink 回调。
     * @param sink 接收 message 的回调。
     * @return
     * @usage logger::self().register_sink([](const message& m){ std::println("{}", m.text()); });
     */
    void register_sink(sink_callback sink)
    {
        if (!sink)
        {
            return;
        }

        std::lock_guard lock(m_sink_update_mutex);
        auto current = m_sinks_snapshot.load(std::memory_order_acquire);
        sink_list next = current ? *current : sink_list{};
        next.push_back(std::move(sink));
        m_sinks_snapshot.store(std::make_shared<const sink_list>(std::move(next)), std::memory_order_release);
    }

    /**
     * @brief 清空所有自定义 sink。
     * @return
     * @usage logger::self().clear_sinks();
     */
    void clear_sinks()
    {
        std::lock_guard lock(m_sink_update_mutex);
        m_sinks_snapshot.store(std::make_shared<const sink_list>(), std::memory_order_release);
    }

    /**
     * @brief 获取因队列满被丢弃的日志计数。
     * @return 丢弃消息数量。
     * @usage auto dropped = logger::self().dropped_messages();
     */
    [[nodiscard]] std::uint64_t dropped_messages() const noexcept
    {
        return m_dropped_messages.load(std::memory_order_relaxed);
    }

    /**
     * @brief 获取当前日志文件路径。
     * @return 日志文件路径。
     * @usage auto path = logger::self().log_file_path();
     */
    [[nodiscard]] std::filesystem::path log_file_path() const
    {
        return m_log_file_path;
    }

    /**
     * @brief 查询当前运行模式。
     * @return 当前 run_mode。
     * @usage if (logger::self().mode() == logger::run_mode::ManualFramePump) { ... }
     */
    [[nodiscard]] run_mode mode() const noexcept
    {
        return m_mode.load(std::memory_order_acquire);
    }

private:
    logger() : m_msg_queue(RingBufferCapacity)
    {
        m_sinks_snapshot.store(std::make_shared<const sink_list>(), std::memory_order_release);
        m_serialized_buffer.reserve(64 * 1024);
    }

    ~logger()
    {
        stop();
    }

    logger(const logger&) = delete;
    logger& operator=(const logger&) = delete;
    logger(logger&&) = delete;
    logger& operator=(logger&&) = delete;

    template <typename... Args>
    void log_with_location_impl(level lvl, const std::source_location& loc, std::format_string<Args...> fmt, Args&&... args)
    {
        // 生产者热路径：只做格式化+入队，消费/IO 全部在消费侧完成。
        if (!m_running.load(std::memory_order_acquire))
        {
            return;
        }

        auto formatted = std::vformat(fmt.get(), std::make_format_args(args...));
        auto msg = message::make_message(message_uid.fetch_add(1, std::memory_order_relaxed), lvl, default_category{}, loc, formatted);
        if (!m_msg_queue.try_push(std::move(msg)))
        {
            m_dropped_messages.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void worker_loop(const std::stop_token& stop_token)
    {
        // 后台线程模式：以批处理驱动消费，空闲时短暂休眠降低 CPU 占用。
        while (!stop_token.stop_requested())
        {
            if (!pump_frame(DefaultBackgroundBatchMessages))
            {
                // 如果没有消息可消费, 稍微休眠以避免忙等待
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            if (!m_running.load(std::memory_order_acquire) && m_msg_queue.empty())
            {
                break;
            }
        }
    }

    bool consume_once()
    {
        // 消费顺序：先写文件缓冲，再分发 sink，确保默认落盘路径与外部 sink 观察到一致顺序。
        auto popped = m_msg_queue.try_pop();
        if (!popped.has_value())
        {
            return false;
        }

        write_to_file(*popped);
        dispatch_to_sinks(*popped);
        return true;
    }

    void flush_remaining_messages()
    {
        // 仅在 stop 或收尾流程调用，尽力清空队列中残留消息。
        while (consume_once())
        {
        }
    }

    void dispatch_to_sinks(const message& msg)
    {
        // 无锁读快照：注册/清理 sink 时替换快照，消费线程只读取不可变列表。
        const auto sinks = m_sinks_snapshot.load(std::memory_order_acquire);
        if (!sinks)
        {
            return;
        }

        for (const auto& sink : *sinks)
        {
            if (sink)
            {
                sink(msg);
            }
        }
    }

    void open_log_file()
    {
        // 当前采用按启动时间生成新文件策略，避免多次启动覆盖历史日志。
        const auto now = std::chrono::system_clock::now();
        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch())
                               .count();

        const auto log_dir = std::filesystem::current_path() / "logs";
        std::error_code ec;
        std::filesystem::create_directories(log_dir, ec);

        m_log_file_path = log_dir / std::format("potato_{}.log", stamp);
        m_log_file.open(m_log_file_path, std::ios::binary | std::ios::out | std::ios::trunc);
    }

    void write_to_file(const message& msg)
    {
        // 将序列化结果先写入内存缓冲，以降低每条日志触发一次系统写调用的开销。
        if (!m_log_file.is_open())
        {
            return;
        }

        m_serialized_buffer.append(message::serialize(msg));
        m_serialized_buffer.push_back('\n');

        // 达到阈值时批量写，减少系统调用次数。
        if (m_serialized_buffer.size() >= m_flush_threshold_bytes)
        {
            flush_file_buffer();
        }
    }

    void flush_file_buffer()
    {
        // 批量刷盘：由帧泵、后台循环和 stop 流程触发。
        if (!m_log_file.is_open() || m_serialized_buffer.empty())
        {
            return;
        }

        m_log_file.write(m_serialized_buffer.data(), static_cast<std::streamsize>(m_serialized_buffer.size()));
        m_serialized_buffer.clear();
    }

private:
    ring_buffer<message, std::allocator<message>, ring_buffer_policy::MPSC> m_msg_queue;
    std::atomic<std::shared_ptr<const sink_list>> m_sinks_snapshot;

    std::jthread m_worker_thread;

    mutable std::mutex m_sink_update_mutex;

    std::ofstream m_log_file;
    std::filesystem::path m_log_file_path;
    std::string m_serialized_buffer;
    std::size_t m_flush_threshold_bytes{32 * 1024};

    std::atomic<run_mode> m_mode{run_mode::BackgroundThread};
    std::atomic<bool> m_running{false};
    std::atomic<std::uint64_t> m_dropped_messages{0};
};

} // namespace core::component
