#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <thread>
#include <vector>

#include <Core/logger.h>

namespace
{

struct TestContext
{
    int failures = 0;

    void Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            ++failures;
            std::println("[FAIL] {}", message);
        }
    }
};

void TestMessageSerializationRoundTrip(TestContext& context)
{
    auto source = core::component::message::make_message(
        42,
        core::component::level::Warning,
        core::component::default_category{},
        std::source_location::current(),
        "a|b\\c\\nline");

    const auto encoded = core::component::message::serialize(source);
    const auto decoded = core::component::message::deserialize(encoded);

    context.Expect(decoded.get_id() == 42, "deserialize should preserve message id");
    context.Expect(decoded.get_level() == core::component::level::Warning, "deserialize should preserve level");
    context.Expect(decoded.category_name() == "default", "deserialize should preserve category");
    context.Expect(decoded.text() == "a|b\\c\\nline", "deserialize should preserve escaped text");
    context.Expect(!decoded.file_name().empty(), "deserialize should preserve file name");
}

void TestLoggerStartStopAndDispatch(TestContext& context)
{
    auto& log = core::component::logger::self();
    log.stop();
    log.clear_sinks();

    std::atomic<int> observed{0};
    std::vector<std::string> captured;
    std::mutex captured_mutex;

    log.register_sink([&](const core::component::message& msg) {
        std::lock_guard lock(captured_mutex);
        captured.emplace_back(msg.text());
        observed.fetch_add(1, std::memory_order_relaxed);
    });

    core::component::logger::log(core::component::level::Info, "should_not_be_seen_before_start");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    context.Expect(observed.load(std::memory_order_relaxed) == 0, "logger should drop log calls before start");

    log.start();

    constexpr int thread_count = 4;
    constexpr int per_thread = 120;
    std::vector<std::thread> workers;
    workers.reserve(thread_count);

    for (int t = 0; t < thread_count; ++t)
    {
        workers.emplace_back([t]() {
            for (int i = 0; i < per_thread; ++i)
            {
                core::component::logger::log(core::component::level::Info, "worker {} item {}", t, i);
            }
        });
    }

    for (auto& worker : workers)
    {
        worker.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    log.stop();

    const int expected = thread_count * per_thread;
    const int observed_count = observed.load(std::memory_order_relaxed);
    context.Expect(observed_count == expected, "logger should dispatch all queued messages to registered sinks");

    bool found_marker = false;
    {
        std::lock_guard lock(captured_mutex);
        for (const auto& entry : captured)
        {
            if (entry.find("worker 0 item 0") != std::string::npos)
            {
                found_marker = true;
                break;
            }
        }
    }
    context.Expect(found_marker, "captured sink output should contain expected formatted message");

    const auto log_file = log.log_file_path();
    context.Expect(!log_file.empty(), "logger should expose output file path after start");
    context.Expect(std::filesystem::exists(log_file), "logger should create output log file");

    if (std::filesystem::exists(log_file))
    {
        std::ifstream input(log_file, std::ios::binary);
        std::string line;
        bool has_content = false;
        while (std::getline(input, line))
        {
            if (!line.empty())
            {
                has_content = true;
                break;
            }
        }
        context.Expect(has_content, "output log file should contain serialized entries");
    }
}

void TestLoggerManualFramePump(TestContext& context)
{
    auto& log = core::component::logger::self();
    log.stop();
    log.clear_sinks();

    std::atomic<int> observed{0};
    log.register_sink([&](const core::component::message&) {
        observed.fetch_add(1, std::memory_order_relaxed);
    });

    log.start_manual();
    context.Expect(log.mode() == core::component::logger::run_mode::ManualFramePump,
        "logger should enter manual frame mode");

    constexpr int producer_count = 3;
    constexpr int per_producer = 90;
    constexpr int expected = producer_count * per_producer;

    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (int p = 0; p < producer_count; ++p)
    {
        producers.emplace_back([p]() {
            for (int i = 0; i < per_producer; ++i)
            {
                core::component::logger::log(core::component::level::Info, "manual producer {} item {}", p, i);
            }
        });
    }

    for (auto& producer : producers)
    {
        producer.join();
    }

    for (int frame = 0; frame < 200; ++frame)
    {
        [[maybe_unused]] const bool had_work = log.pump_frame(64, std::chrono::milliseconds(1));
        if (observed.load(std::memory_order_relaxed) >= expected)
        {
            break;
        }
        std::this_thread::yield();
    }

    log.stop();
    context.Expect(observed.load(std::memory_order_relaxed) == expected,
        "manual frame pump should dispatch all queued messages");
}

} // namespace

int main()
{
    TestContext context;

    TestMessageSerializationRoundTrip(context);
    TestLoggerStartStopAndDispatch(context);
    TestLoggerManualFramePump(context);

    return context.failures;
}
