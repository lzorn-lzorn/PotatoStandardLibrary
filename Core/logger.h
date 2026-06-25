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
	// void(*)(const message&)
	using sink_callback = std::function<void(const message&)>;
public:
	// config
    static constexpr size_t RingBufferCapacity   = 8192;          // MPSC 环形缓冲区大小
    static constexpr size_t BatchSize            = 256;           // 一次收集多少条消息后序列化
    static constexpr size_t DoubleBufferCapacity = 16;          // 双缓冲最多保留 16 个块
    static constexpr auto   BatchTimeout         = std::chrono::milliseconds(10); // 即使不满也刷新	
public:
	static logger& self() {
		static logger instance;
		return instance;
	}

	template <typename ... Args>
	static void log(level lvl, std::format_string<Args...> fmt, Args&& ... args, std::source_location loc = std::source_location::current())
	{
		if(self().m_running)
		{
			return ;
		}
		self().m_msg_queue.try_push(message::make_message(	
			message_uid.fetch_add(1), 
			lvl, 
			default_category{}, 
			loc, 
			std::vformat(fmt.get(), std::make_format_args(args...))
		));
	}
	logger& start()
	{
		if (m_running.exchange(true)) 
		{
			// 已经在运行
			return *this;
		}
		m_worker_thread = std::jthread([this]() { worker_loop(); });
		m_file_writer_thread = std::jthread([this]() { file_writer_loop(); });
		return *this;
	}

	logger& stop()
	{
		m_running = false;
		if (m_worker_thread.joinable()) 
		{
			m_worker_thread.join();
		}
		if (m_file_writer_thread.joinable()) 
		{
			m_file_writer_thread.join();
		}
		return *this;
	}

	void register_sink(sink_callback sink) 
	{
		std::lock_guard lock(m_sink_mutex);
		m_sinks.push_back(std::move(sink));
	}

	void worker_loop()
	{
		std::vector<message> batch;
        batch.reserve(BatchSize);
		auto last_flush = std::chrono::steady_clock::now();

		while(m_running)
		{
			message msg;
 			while (batch.size() < BatchSize && m_msg_queue.try_pop(msg)) {
                // 同时扇出到控制台/屏幕 Sinks（非文件）
                for (auto& sink : m_sinks) {
                    sink(msg);
                }
                batch.push_back(std::move(msg));
            }

			// 2. 判断是否需要提交批次
            auto now = std::chrono::steady_clock::now();
            bool batch_full = (batch.size() >= BatchSize);
            bool timeout = (now - last_flush >= BatchTimeout) && !batch.empty();

			if (batch_full || timeout) {
                // 提交批次到文件写入线程
                // 这里可以使用双缓冲或者其他机制

				while (!m_batch_buffer.try_push(std::move(batch))) {
                    // 若双缓冲满，等待文件线程消费（极短自旋）
                    std::this_thread::yield();
                }

                batch.clear();
                last_flush = now;
            }
		}	
	}

	void file_writer_loop()
	{
		// 这里可能存在问题: std::filesystem::current_path() 可能是 build/ 下的目录
		// 每次运行生成一个新文件
		auto file_path = std::filesystem::current_path() / std::format("game_{}.log", std::time(nullptr));
        std::ofstream file(file_path, std::ios::binary | std::ios::app);
        if (!file.is_open()) {
            // 如果无法打开文件，直接丢弃所有块（可通知错误）
            return;
        }

		while(m_running)
		{

		}
	}
private:
	logger()
	{
		// 手动添加默认的 sink
	} 
	~logger() 
	{
		stop();
	}
	logger(const logger&) = delete;
	logger& operator=(const logger&) = delete;
	logger(logger&&) = delete;
	logger& operator=(logger&&) = delete;

private:
	ring_buffer<message, std::allocator<message>, ring_buffer_policy::MPSC> m_msg_queue;
	double_buffer<message, BufferSize> m_batch_buffer;
	dynamic_array<sink_callback> m_sinks; // 用于注册不同的日志输出方式, 例如控制台输出等
	std::jthread m_worker_thread;
	std::jthread m_file_writer_thread;
	std::mutex m_sink_mutex;
	std::atomic<bool> m_running { false };
};

}