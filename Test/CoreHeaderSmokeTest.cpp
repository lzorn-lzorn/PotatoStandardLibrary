#include <chrono>
#include <cstdint>
#include <print>

#include <Core/Core.h>

namespace
{

enum class TestBits : std::uint8_t
{
    A = 1u << 0,
    B = 1u << 1,
};

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

} // namespace

int main()
{
    TestContext context;

    core::dynamic_array<int> values;
    values.push_back(7);
    context.Expect(values.size() == 1, "Core/Core.h should expose std_interface aliases");

    const auto flags = core::flags<TestBits>(TestBits::A) | TestBits::B;
    context.Expect(flags.has(TestBits::A) && flags.has(TestBits::B), "Core/Core.h should expose enum_flag helpers");

    context.Expect(core::math::approx_equal(1.0f, 1.0f), "Core/Core.h should expose math helpers");

    core::ring_buffer<int, std::allocator<int>, core::ring_buffer_policy::SingleThread> buffer(8);
    buffer.push_back(123);
    context.Expect(buffer.front() == 123, "Core/Core.h should expose container headers");

    const auto now = std::chrono::system_clock::now();
    const auto timestamp = core::serialize_to_timestamp_ms(now);
    context.Expect(!timestamp.empty(), "Core/Core.h should expose time helpers");

    if (context.failures == 0)
    {
        std::println("[PASS] CoreHeaderSmokeTest");
        return 0;
    }

    std::println("[FAIL] CoreHeaderSmokeTest with {} failure(s)", context.failures);
    return 1;
}
