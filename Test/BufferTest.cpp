#include <atomic>
#include <cstdint>
#include <print>
#include <thread>
#include <vector>

#include <Core/buffer.h>

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

void TestSingleThreadBasic(TestContext& context)
{
    core::ring_buffer<int, std::allocator<int>, core::ring_buffer_policy::SingleThread> buffer(8);

    for (int i = 0; i < 8; ++i)
    {
        buffer.push_back(i);
    }

    bool full_throw = false;
    try
    {
        buffer.push_back(9);
    }
    catch (const std::out_of_range&)
    {
        full_throw = true;
    }

    context.Expect(full_throw, "single-thread ring_buffer should throw on push when full");
    context.Expect(buffer.front() == 0, "front should return first inserted value");
    context.Expect(buffer.back() == 7, "back should return last inserted value");

    for (int i = 0; i < 4; ++i)
    {
        context.Expect(buffer.front() == i, "front should advance in FIFO order");
        buffer.pop_front();
    }

    for (int i = 8; i < 12; ++i)
    {
        buffer.push_back(i);
    }

    const auto [first, second] = buffer.linearize_views();
    context.Expect(first.size() + second.size() == buffer.size(), "linearized spans should cover all live elements");

    int expected = 4;
    for (int value : buffer)
    {
        context.Expect(value == expected, "iterator traversal should preserve ring order");
        ++expected;
    }

    while (!buffer.empty())
    {
        buffer.pop_front();
    }

    bool empty_throw = false;
    try
    {
        buffer.pop_front();
    }
    catch (const std::out_of_range&)
    {
        empty_throw = true;
    }

    context.Expect(empty_throw, "single-thread ring_buffer should throw on pop when empty");
}

void TestSpscBasic(TestContext& context)
{
    core::ring_buffer<int, std::allocator<int>, core::ring_buffer_policy::SPSC> buffer(8);

    for (int i = 0; i < 8; ++i)
    {
        context.Expect(buffer.try_push(i), "SPSC should accept pushes until full");
    }

    context.Expect(!buffer.try_push(9), "SPSC should reject push when full");

    for (int i = 0; i < 8; ++i)
    {
        auto popped = buffer.try_pop();
        context.Expect(popped.has_value(), "SPSC pop should return value when not empty");
        if (popped.has_value())
        {
            context.Expect(*popped == i, "SPSC should pop in FIFO order");
        }
    }

    context.Expect(!buffer.try_pop().has_value(), "SPSC pop should fail when empty");
}

void TestMpscMultiProducer(TestContext& context)
{
    constexpr int producer_count = 4;
    constexpr int per_producer = 200;
    constexpr int total = producer_count * per_producer;

    core::ring_buffer<int, std::allocator<int>, core::ring_buffer_policy::MPSC> buffer(1024);

    std::atomic<bool> start{false};
    std::atomic<int> consumed{0};
    std::atomic<std::int64_t> sum{0};

    std::vector<std::thread> producers;
    producers.reserve(producer_count);

    for (int p = 0; p < producer_count; ++p)
    {
        producers.emplace_back([&, p]() {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            for (int i = 0; i < per_producer; ++i)
            {
                const int value = p * 100000 + i;
                while (!buffer.try_push(value))
                {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::thread consumer([&]() {
        while (!start.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }

        while (consumed.load(std::memory_order_relaxed) < total)
        {
            auto item = buffer.try_pop();
            if (!item.has_value())
            {
                std::this_thread::yield();
                continue;
            }
            sum.fetch_add(*item, std::memory_order_relaxed);
            consumed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    start.store(true, std::memory_order_release);

    for (auto& producer : producers)
    {
        producer.join();
    }
    consumer.join();

    std::int64_t expected_sum = 0;
    for (int p = 0; p < producer_count; ++p)
    {
        for (int i = 0; i < per_producer; ++i)
        {
            expected_sum += p * 100000 + i;
        }
    }

    context.Expect(consumed.load(std::memory_order_relaxed) == total, "MPSC should consume all produced values");
    context.Expect(sum.load(std::memory_order_relaxed) == expected_sum, "MPSC sum should match produced values");
}

void TestMpmcMultiProducerMultiConsumer(TestContext& context)
{
    constexpr int producer_count = 4;
    constexpr int consumer_count = 3;
    constexpr int per_producer = 150;
    constexpr int total = producer_count * per_producer;

    core::ring_buffer<int, std::allocator<int>, core::ring_buffer_policy::MPMC> buffer(1024);

    std::atomic<bool> start{false};
    std::atomic<int> consumed{0};
    std::atomic<std::int64_t> sum{0};

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    producers.reserve(producer_count);
    consumers.reserve(consumer_count);

    for (int p = 0; p < producer_count; ++p)
    {
        producers.emplace_back([&, p]() {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            for (int i = 0; i < per_producer; ++i)
            {
                const int value = p * 10000 + i;
                while (!buffer.try_push(value))
                {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (int c = 0; c < consumer_count; ++c)
    {
        consumers.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            while (consumed.load(std::memory_order_relaxed) < total)
            {
                auto item = buffer.try_pop();
                if (!item.has_value())
                {
                    std::this_thread::yield();
                    continue;
                }

                sum.fetch_add(*item, std::memory_order_relaxed);
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    start.store(true, std::memory_order_release);

    for (auto& producer : producers)
    {
        producer.join();
    }
    for (auto& consumer : consumers)
    {
        consumer.join();
    }

    std::int64_t expected_sum = 0;
    for (int p = 0; p < producer_count; ++p)
    {
        for (int i = 0; i < per_producer; ++i)
        {
            expected_sum += p * 10000 + i;
        }
    }

    context.Expect(consumed.load(std::memory_order_relaxed) == total, "MPMC should consume all produced values");
    context.Expect(sum.load(std::memory_order_relaxed) == expected_sum, "MPMC sum should match produced values");
}

} // namespace

int main()
{
    TestContext context;

    TestSingleThreadBasic(context);
    TestSpscBasic(context);
    TestMpscMultiProducer(context);
    TestMpmcMultiProducerMultiConsumer(context);

    return context.failures;
}
