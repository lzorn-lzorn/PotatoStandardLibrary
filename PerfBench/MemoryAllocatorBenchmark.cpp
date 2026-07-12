#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include <benchmark/benchmark.h>

#include <Core/Memory/memory_facade.h>

namespace
{

void EnsureAllocatorConfigured()
{
    static std::once_flag once;
    std::call_once(once, [] {
        core::mem::AllocatorConfig config;
        core::mem::AllocatorFacade::configure(config);
    });
}

void PublishAllocatorPoolTelemetry(benchmark::State& state)
{
    const auto stats = core::mem::AllocatorFacade::getStats();
    state.counters["p2s_l1_usage"] = stats.PageToSpanLevel1UsageRatio;
    state.counters["p2s_l2_usage"] = stats.PageToSpanLevel2UsageRatio;
    state.counters["p2s_warn"] = static_cast<double>(stats.PageToSpanPoolExhaustionWarningCount);
    state.counters["span_pool_usage"] = stats.SpanObjectPoolUsageRatio;

    state.counters["region_l1_usage"] = stats.RegionIndexLevel1UsageRatio;
    state.counters["region_l2_usage"] = stats.RegionIndexLevel2UsageRatio;
    state.counters["region_warn"] = static_cast<double>(stats.RegionIndexPoolExhaustionWarningCount);
}

static void BM_TransientAllocateFree(benchmark::State& state)
{
    EnsureAllocatorConfigured();

    const std::size_t bytes = static_cast<std::size_t>(state.range(0));
    for (auto _ : state)
    {
        void* ptr = core::mem::AllocatorFacade::allocateTransient(
            bytes,
            alignof(std::max_align_t),
            7001,
            false,
            true);
        if (!ptr)
        {
            state.SkipWithError("transient allocation failed");
            break;
        }

        benchmark::DoNotOptimize(ptr);
        core::mem::AllocatorFacade::deallocateTransient(ptr, bytes, alignof(std::max_align_t), 7001);
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(bytes));
    core::mem::AllocatorFacade::onThreadExit();
    PublishAllocatorPoolTelemetry(state);
}

static void BM_PersistentAllocateFree(benchmark::State& state)
{
    EnsureAllocatorConfigured();

    const std::size_t bytes = static_cast<std::size_t>(state.range(0));
    for (auto _ : state)
    {
        void* ptr = core::mem::AllocatorFacade::allocatePersistent(
            bytes,
            alignof(std::max_align_t),
            7002,
            false,
            true);
        if (!ptr)
        {
            state.SkipWithError("persistent allocation failed");
            break;
        }

        benchmark::DoNotOptimize(ptr);
        core::mem::AllocatorFacade::deallocatePersistent(ptr, bytes, alignof(std::max_align_t), 7002);
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(bytes));
    core::mem::AllocatorFacade::onThreadExit();
    PublishAllocatorPoolTelemetry(state);
}

static void BM_TransientAllocateFreeThreaded64(benchmark::State& state)
{
    EnsureAllocatorConfigured();

    constexpr std::size_t bytes = 64;
    for (auto _ : state)
    {
        void* ptr = core::mem::AllocatorFacade::allocateTransient(
            bytes,
            alignof(std::max_align_t),
            7003,
            false,
            true);
        if (!ptr)
        {
            state.SkipWithError("threaded transient allocation failed");
            break;
        }

        benchmark::DoNotOptimize(ptr);
        core::mem::AllocatorFacade::deallocateTransient(ptr, bytes, alignof(std::max_align_t), 7003);
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(bytes));
    core::mem::AllocatorFacade::onThreadExit();
    PublishAllocatorPoolTelemetry(state);
}

static void BM_MixedTransient(benchmark::State& state)
{
    EnsureAllocatorConfigured();

    constexpr std::array<std::size_t, 10> kSizes = {16, 24, 40, 64, 96, 256, 512, 1024, 4096, 16384};
    std::uint64_t processedBytes = 0;
    std::size_t cursor = 0;

    for (auto _ : state)
    {
        const std::size_t bytes = kSizes[cursor % kSizes.size()];
        ++cursor;

        void* ptr = core::mem::AllocatorFacade::allocateTransient(
            bytes,
            alignof(std::max_align_t),
            7004,
            false,
            true);
        if (!ptr)
        {
            state.SkipWithError("mixed transient allocation failed");
            break;
        }

        benchmark::DoNotOptimize(ptr);
        core::mem::AllocatorFacade::deallocateTransient(ptr, bytes, alignof(std::max_align_t), 7004);
        processedBytes += static_cast<std::uint64_t>(bytes);
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()));
    state.SetBytesProcessed(static_cast<std::int64_t>(processedBytes));
    core::mem::AllocatorFacade::onThreadExit();
    PublishAllocatorPoolTelemetry(state);
}

BENCHMARK(BM_TransientAllocateFree)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Arg(65536);

BENCHMARK(BM_PersistentAllocateFree)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096);

BENCHMARK(BM_TransientAllocateFreeThreaded64)
    ->ThreadRange(1, 8);

BENCHMARK(BM_MixedTransient);

} // namespace
