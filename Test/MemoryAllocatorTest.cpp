#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory_resource>
#include <numeric>
#include <print>
#include <thread>
#include <vector>

#include <Core/Memory/memory_facade.h>
#include <Core/Memory/Observability/memory_observability.h>

namespace
{

struct TestContext
{
	int failures = 0;
	void Expect(const bool condition, const char* message)
	{
		if (!condition)
		{
			++failures;
			std::printf("[FAIL] %s\n", message);
			std::fflush(stdout);
		}
	}
};

struct GuardCorruptionObserver
{
	std::atomic<int> Count = 0;
	void onMemoryEvent(const core::mem::MemoryEvent& event)
	{
		if (event.Type == core::mem::MemoryEventType::GuardCorruption)
		{
			Count.fetch_add(1, std::memory_order_relaxed);
		}
	}
};

struct UseAfterFreeObserver
{
	std::atomic<int> Count = 0;
	void onMemoryEvent(const core::mem::MemoryEvent& event)
	{
		if (event.Type == core::mem::MemoryEventType::UseAfterFree)
		{
			Count.fetch_add(1, std::memory_order_relaxed);
		}
	}
};

void TestSmallAllocationRoundTrip(TestContext& context)
{
	constexpr std::array<std::size_t, 9> sizes = {8, 24, 40, 64, 96, 256, 1024, 4096, 65536};
	for (std::size_t size : sizes)
	{
		void* ptr = core::mem::AllocatorFacade::allocate(size, alignof(std::max_align_t));
		context.Expect(ptr != nullptr, "small allocation should return non-null pointer");
		if (ptr)
		{
			std::memset(ptr, 0xA5, size);
			core::mem::AllocatorFacade::deallocate(ptr, size, alignof(std::max_align_t));
		}
	}
}

void TestConcurrentAllocation(TestContext& context)
{
	constexpr int thread_count = 8;
	constexpr int iterations_per_thread = 4000;
	constexpr std::array<std::size_t, 8> sizes = {16, 24, 40, 64, 128, 320, 1024, 4096};
	std::atomic<int> failure_count = 0;
	std::atomic<bool> start = false;
	std::vector<std::thread> workers;
	workers.reserve(thread_count);
	for (int thread_index = 0; thread_index < thread_count; ++thread_index)
	{
		workers.emplace_back([&, thread_index]() {
			while (!start.load(std::memory_order_acquire))
			{
				std::this_thread::yield();
			}
			for (int i = 0; i < iterations_per_thread; ++i)
			{
				const std::size_t size = sizes[(i + thread_index) % sizes.size()];
				void* ptr = core::mem::AllocatorFacade::allocate(size, alignof(std::max_align_t));
				if (!ptr)
				{
					failure_count.fetch_add(1, std::memory_order_relaxed);
					continue;
				}
				std::memset(ptr, 0x5A, size);
				core::mem::AllocatorFacade::deallocate(ptr, size, alignof(std::max_align_t));
			}
			core::mem::AllocatorFacade::onThreadExit();
		});
	}
	start.store(true, std::memory_order_release);
	for (auto& worker : workers)
	{
		worker.join();
	}
	context.Expect(
		failure_count.load(std::memory_order_relaxed) == 0,
		"concurrent allocation should complete without null allocation results");
}

void TestStlCompatibility(TestContext& context)
{
	std::vector<int, core::mem::EngineAllocator<int>> values;
	values.reserve(1024);
	for (int i = 0; i < 1024; ++i)
	{
		values.push_back(i);
	}
	const std::int64_t sum = std::accumulate(values.begin(), values.end(), std::int64_t{0});
	context.Expect(values.size() == 1024, "STL allocator should support std::vector growth");
	context.Expect(sum == 523776, "STL allocator vector contents should remain correct");
}

void TestPmrCompatibility(TestContext& context)
{
	core::mem::EngineMemoryResource resource;
	std::pmr::vector<int> values(&resource);
	for (int i = 0; i < 512; ++i)
	{
		values.push_back(i * 2);
	}
	const std::int64_t sum = std::accumulate(values.begin(), values.end(), std::int64_t{0});
	context.Expect(values.size() == 512, "PMR resource should support pmr::vector growth");
	context.Expect(sum == 261632, "PMR vector contents should remain correct");
}

void TestObserversAndLeakDetection(TestContext& context)
{
	core::mem::MemoryLeakDetectorObserver leaks;
	auto& tracker = core::mem::MemoryTracker::global();
	tracker.clear();
	core::mem::AllocatorFacade::bindObserver(leaks);
	core::mem::AllocatorFacade::bindObserver(tracker);
	void* ptr = core::mem::AllocatorFacade::allocate(128, alignof(std::max_align_t), 7);
	core::mem::AllocatorFacade::deallocate(ptr, 128, alignof(std::max_align_t), 7);
	void* leaked = core::mem::AllocatorFacade::allocate(96, alignof(std::max_align_t), 13);
	const std::size_t leak_count_before_free = leaks.getLeakCount();
	context.Expect(leak_count_before_free == 1, "leak detector should report one live allocation");
	core::mem::AllocatorFacade::deallocate(leaked, 96, alignof(std::max_align_t), 13);
	const auto snapshot = tracker.getStatistics();
	context.Expect(snapshot.AllocateCount >= 2, "tracker should count allocation events");
	context.Expect(snapshot.DeallocateCount >= 2, "tracker should count deallocation events");
	context.Expect(tracker.getEventCount() > 0, "tracker should capture memory timeline events");
	const auto trace_path = std::filesystem::temp_directory_path() / "potato_memory_report_test.json";
	const bool flushed = tracker.flushToFile(trace_path);
	context.Expect(flushed, "tracker should flush JSON report file");
	context.Expect(std::filesystem::exists(trace_path), "memory report should be generated in temp directory");
	std::error_code ec;
	std::filesystem::remove(trace_path, ec);
	core::mem::AllocatorFacade::unbindObserver(tracker);
	core::mem::AllocatorFacade::unbindObserver(leaks);
	tracker.clear();
}

void TestDebugGuardDetection(TestContext& context)
{
#if !defined(NDEBUG) || defined(CORE_MEM_FORCE_DEBUG)
	GuardCorruptionObserver guard_observer;
	core::mem::AllocatorFacade::bindObserver(guard_observer);
	void* ptr = core::mem::AllocatorFacade::allocate(32, alignof(std::max_align_t));
	auto* bytes = static_cast<std::uint8_t*>(ptr);
	bytes[32] = 0xEE;
	core::mem::AllocatorFacade::deallocate(ptr, 32, alignof(std::max_align_t));
	core::mem::AllocatorFacade::unbindObserver(guard_observer);
	context.Expect(
		guard_observer.Count.load(std::memory_order_relaxed) > 0,
		"debug mode should detect back-guard corruption");
#else
	context.Expect(true, "debug guard test skipped in release mode");
#endif
}

void TestDeallocateNullptr(TestContext& context)
{
	core::mem::AllocatorFacade::deallocate(nullptr, 64, alignof(std::max_align_t));
	core::mem::AllocatorFacade::deallocateTransient(nullptr, 64, alignof(std::max_align_t));
	core::mem::AllocatorFacade::deallocatePersistent(nullptr, 64, alignof(std::max_align_t));
	context.Expect(true, "deallocate(nullptr) should be a no-op");
}

void TestUseAfterFreeDetection(TestContext& context)
{
#if !defined(NDEBUG) || defined(CORE_MEM_FORCE_DEBUG)
	UseAfterFreeObserver observer;
	core::mem::AllocatorFacade::bindObserver(observer);

	constexpr std::size_t size = 96;
	void* ptr = core::mem::AllocatorFacade::allocateTransient(size, alignof(std::max_align_t));
	core::mem::AllocatorFacade::deallocateTransient(ptr, size, alignof(std::max_align_t));

	std::memset(ptr, 0xAB, size);
	core::mem::AllocatorFacade::flushDeferredFrees();

	core::mem::AllocatorFacade::unbindObserver(observer);
	context.Expect(
		observer.Count.load(std::memory_order_relaxed) > 0,
		"use-after-free writes should be reported when deferred free is flushed");
#else
	context.Expect(true, "use-after-free test skipped in release mode");
#endif
}

void TestPrewarmApi(TestContext& context)
{
	std::vector<core::mem::MemoryPrewarmRequest> requests;
	requests.push_back({64, alignof(std::max_align_t), 512, 101, false, core::mem::AllocationLifetime::Transient});
	requests.push_back({4096, alignof(std::max_align_t), 128, 102, false, core::mem::AllocationLifetime::Transient});

	const std::size_t warmed = core::mem::AllocatorFacade::prewarm(requests);
	context.Expect(warmed == 640, "prewarm API should execute full batch warm-up requests");
}

void TestHookIssueTracking(TestContext& context)
{
	auto& tracker = core::mem::MemoryTracker::global();
	tracker.clear();
	core::mem::MemoryTracker::setHookEnabled(true);
	core::mem::MemoryTracker::setPayloadPreviewBytes(16);

	std::array<std::uint8_t, 32> payload_a{};
	std::array<std::uint8_t, 32> payload_b{};
	for (std::size_t i = 0; i < payload_a.size(); ++i)
	{
		payload_a[i] = static_cast<std::uint8_t>(i + 1);
		payload_b[i] = static_cast<std::uint8_t>(0xE0u + (i & 0x0F));
	}

	void* ptr_a = payload_a.data();
	void* ptr_b = payload_b.data();

	core::mem::MemoryTracker::onHookAllocation(
		core::mem::HookAllocationOp::Malloc,
		ptr_a,
		payload_a.size(),
		core::mem::MemoryTrackerUnknownSize,
		nullptr);
	core::mem::MemoryTracker::onHookFree(
		core::mem::HookAllocationOp::Free,
		ptr_a,
		core::mem::MemoryTrackerUnknownSize,
		core::mem::MemoryTrackerUnknownSize,
		nullptr);
	core::mem::MemoryTracker::onHookFree(
		core::mem::HookAllocationOp::Free,
		ptr_a,
		core::mem::MemoryTrackerUnknownSize,
		core::mem::MemoryTrackerUnknownSize,
		nullptr);

	std::array<std::uint8_t, 16> invalid_ptr_memory{};
	core::mem::MemoryTracker::onHookFree(
		core::mem::HookAllocationOp::Delete,
		invalid_ptr_memory.data(),
		core::mem::MemoryTrackerUnknownSize,
		core::mem::MemoryTrackerUnknownSize,
		nullptr);

	core::mem::MemoryTracker::onHookAllocation(
		core::mem::HookAllocationOp::New,
		ptr_b,
		payload_b.size(),
		core::mem::MemoryTrackerUnknownSize,
		nullptr);
	core::mem::MemoryTracker::onHookFree(
		core::mem::HookAllocationOp::Free,
		ptr_b,
		core::mem::MemoryTrackerUnknownSize,
		core::mem::MemoryTrackerUnknownSize,
		nullptr);

	const auto report = tracker.getReport();
	context.Expect(report.Statistics.DoubleFreeCount >= 1, "tracker should detect double free from hook stream");
	context.Expect(report.Statistics.InvalidFreeCount >= 1, "tracker should detect invalid free from hook stream");
	context.Expect(
		report.Statistics.MismatchedFreeFunctionCount >= 1,
		"tracker should detect mismatched free function from hook stream");

	bool has_double_free_payload = false;
	for (const auto& issue : report.Issues)
	{
		if (issue.Type == core::mem::MemoryIssueType::DoubleFree && issue.Payload.CapturedBytes > 0)
		{
			has_double_free_payload = true;
			break;
		}
	}
	context.Expect(has_double_free_payload, "double free issue should contain payload preview bytes");
	core::mem::MemoryTracker::setHookEnabled(false);
	tracker.clear();
}

void BenchmarkLargeAllocation(TestContext& context)
{
	constexpr std::size_t bytes = 32u * 1024u * 1024u;
	constexpr int iterations = 12;

	const auto begin = std::chrono::steady_clock::now();
	for (int i = 0; i < iterations; ++i)
	{
		void* ptr = core::mem::AllocatorFacade::allocateTransient(bytes, alignof(std::max_align_t), 2001);
		context.Expect(ptr != nullptr, "large allocation benchmark should return non-null pointer");
		if (!ptr)
		{
			continue;
		}

		auto* data = static_cast<std::uint8_t*>(ptr);
		data[0] = 0x5A;
		data[bytes - 1] = 0xA5;
		core::mem::AllocatorFacade::deallocateTransient(ptr, bytes, alignof(std::max_align_t), 2001);
	}
	const auto end = std::chrono::steady_clock::now();

	const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
	std::println(
		"[BENCH] large allocation: {} iters x {} MB, total={} ms",
		iterations,
		bytes / (1024 * 1024),
		elapsed_ms);
}

void BenchmarkHighFrequencyAllocation(TestContext& context)
{
	constexpr std::size_t bytes = 64;
	constexpr int iterations = 200000;

	const auto begin = std::chrono::steady_clock::now();
	for (int i = 0; i < iterations; ++i)
	{
		void* ptr = core::mem::AllocatorFacade::allocateTransient(bytes, alignof(std::max_align_t), 3001);
		if (!ptr)
		{
			context.Expect(false, "high-frequency benchmark should not produce null allocations");
			break;
		}

		core::mem::AllocatorFacade::deallocateTransient(ptr, bytes, alignof(std::max_align_t), 3001);
	}
	const auto end = std::chrono::steady_clock::now();

	const double elapsed_s = std::chrono::duration<double>(end - begin).count();
	const double ops_per_sec = elapsed_s > 0.0 ? static_cast<double>(iterations) / elapsed_s : 0.0;
	std::println(
		"[BENCH] high-frequency allocation: {} ops, {:.2f} ops/s",
		iterations,
		ops_per_sec);
}

} // namespace

int main()
{
	TestContext context;
	core::mem::MemoryTracker::setHookEnabled(false);
	core::mem::AllocatorConfig config;
	config.CaptureStack = false;
	core::mem::AllocatorFacade::configure(config);
	TestSmallAllocationRoundTrip(context);
	TestConcurrentAllocation(context);
	TestStlCompatibility(context);
	TestPmrCompatibility(context);
	TestObserversAndLeakDetection(context);
	TestDebugGuardDetection(context);
	TestDeallocateNullptr(context);
	TestUseAfterFreeDetection(context);
	TestPrewarmApi(context);
	TestHookIssueTracking(context);
	BenchmarkLargeAllocation(context);
	BenchmarkHighFrequencyAllocation(context);
	core::mem::AllocatorFacade::onThreadExit();
	return context.failures;
}
