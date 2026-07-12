#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <numeric>
#include <print>
#include <thread>
#include <vector>

#include "../Core/Containers/radix_tree.h"
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
	if constexpr (core::mem::MemoryTracker::isCompiledEnabled())
	{
		context.Expect(snapshot.AllocateCount >= 2, "tracker should count allocation events");
		context.Expect(snapshot.DeallocateCount >= 2, "tracker should count deallocation events");
		context.Expect(tracker.getEventCount() > 0, "tracker should capture memory timeline events");
	}
	else
	{
		context.Expect(snapshot.EngineEventCount == 0, "release tracker should not process engine events");
		context.Expect(tracker.getEventCount() == 0, "release tracker should keep timeline empty");
	}
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
	if constexpr (core::mem::MemoryCompileEnableUseAfterFreeDetection && core::mem::MemoryCompileEnableDebugGuards)
	{
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
	}
	else
	{
		context.Expect(true, "use-after-free test skipped when compile-time UAF detection is disabled");
	}
}

void TestQuarantineDoesNotDrainOnAllocate(TestContext& context)
{
	if constexpr (core::mem::MemoryCompileEnableUseAfterFreeDetection && core::mem::MemoryCompileEnableDebugGuards)
	{
	UseAfterFreeObserver observer;
	core::mem::AllocatorFacade::bindObserver(observer);

	constexpr std::size_t size = 96;
	void* ptr = core::mem::AllocatorFacade::allocateTransient(size, alignof(std::max_align_t));
	core::mem::AllocatorFacade::deallocateTransient(ptr, size, alignof(std::max_align_t));
	std::memset(ptr, 0xAB, size);

	void* probe = core::mem::AllocatorFacade::allocateTransient(64, alignof(std::max_align_t));
	core::mem::AllocatorFacade::deallocateTransient(probe, 64, alignof(std::max_align_t));

	context.Expect(
		observer.Count.load(std::memory_order_relaxed) == 0,
		"quarantine should not be drained implicitly by allocation hot path");

	core::mem::AllocatorFacade::flushDeferredFrees();
	context.Expect(
		observer.Count.load(std::memory_order_relaxed) > 0,
		"quarantine should report use-after-free when explicitly flushed");

	core::mem::AllocatorFacade::unbindObserver(observer);
	}
	else
	{
		context.Expect(true, "deterministic quarantine test skipped when compile-time UAF detection is disabled");
	}
}

void TestPrewarmApi(TestContext& context)
{
	std::vector<core::mem::MemoryPrewarmRequest> requests;
	requests.push_back({64, alignof(std::max_align_t), 512, 101, false, core::mem::AllocationLifetime::Transient});
	requests.push_back({4096, alignof(std::max_align_t), 128, 102, false, core::mem::AllocationLifetime::Transient});

	const std::size_t warmed = core::mem::AllocatorFacade::prewarm(requests);
	context.Expect(warmed == 640, "prewarm API should execute full batch warm-up requests");
}

void TestAllocatorStatsApi(TestContext& context)
{
	constexpr std::size_t size = 128;
	void* ptr = core::mem::AllocatorFacade::allocateTransient(size, alignof(std::max_align_t), 88);
	core::mem::AllocatorFacade::deallocateTransient(ptr, size, alignof(std::max_align_t), 88);

	auto before_flush = core::mem::AllocatorFacade::getStats();
	core::mem::AllocatorFacade::flushDeferredFrees();
	auto after_flush = core::mem::AllocatorFacade::getStats();

	if constexpr (core::mem::MemoryCompileEnableUseAfterFreeDetection && core::mem::MemoryCompileEnableDebugGuards)
	{
		context.Expect(
			before_flush.QuarantineEntryCount >= 1,
			"stats API should expose quarantine entries before flush in debug path");
	}
	else
	{
		context.Expect(
			before_flush.QuarantineEntryCount == 0,
			"stats API should keep quarantine empty when compile-time UAF detection is disabled");
	}

	context.Expect(
		after_flush.QuarantineEntryCount == 0,
		"stats API should report zero quarantine entries after flush");

	context.Expect(
		after_flush.PageToSpanLevel1NodesUsed <= after_flush.PageToSpanLevel1NodesCapacity,
		"PageToSpan L1 usage should not exceed capacity");
	context.Expect(
		after_flush.PageToSpanLevel2NodesUsed <= after_flush.PageToSpanLevel2NodesCapacity,
		"PageToSpan L2 usage should not exceed capacity");
	context.Expect(
		after_flush.PageToSpanLevel1UsageRatio >= 0.0 && after_flush.PageToSpanLevel1UsageRatio <= 1.0,
		"PageToSpan L1 usage ratio should stay within [0, 1]");
	context.Expect(
		after_flush.PageToSpanLevel2UsageRatio >= 0.0 && after_flush.PageToSpanLevel2UsageRatio <= 1.0,
		"PageToSpan L2 usage ratio should stay within [0, 1]");

	context.Expect(
		after_flush.RegionIndexLevel1NodesUsed <= after_flush.RegionIndexLevel1NodesCapacity,
		"RegionIndex L1 usage should not exceed capacity");
	context.Expect(
		after_flush.RegionIndexLevel2NodesUsed <= after_flush.RegionIndexLevel2NodesCapacity,
		"RegionIndex L2 usage should not exceed capacity");
	context.Expect(
		after_flush.RegionIndexLevel1UsageRatio >= 0.0 && after_flush.RegionIndexLevel1UsageRatio <= 1.0,
		"RegionIndex L1 usage ratio should stay within [0, 1]");
	context.Expect(
		after_flush.RegionIndexLevel2UsageRatio >= 0.0 && after_flush.RegionIndexLevel2UsageRatio <= 1.0,
		"RegionIndex L2 usage ratio should stay within [0, 1]");

	context.Expect(
		after_flush.SpanObjectPoolInUse <= after_flush.SpanObjectPoolAllocated,
		"span object pool in-use count should not exceed allocated count");
	context.Expect(
		after_flush.SpanObjectPoolUsageRatio >= 0.0 && after_flush.SpanObjectPoolUsageRatio <= 1.0,
		"span object pool usage ratio should stay within [0, 1]");
}

void TestPageAllocatorSpanCacheReuseSafety(TestContext& context)
{
	core::mem::AllocatorConfig local_config;
	local_config.SmallRegionReserveBytes = 128u * 1024u;
	auto page_allocator = std::make_unique<core::mem::PageAllocator>(local_config);

	core::mem::PageSpan cached = page_allocator->acquireSpan(2);
	context.Expect(cached.isValid(), "page allocator should provide initial span");
	if (!cached.isValid())
	{
		return;
	}

	const std::byte* cached_base = cached.Base;
	page_allocator->releaseSpan(cached);

	std::vector<core::mem::PageSpan> inflight;
	inflight.reserve(256);
	for (int i = 0; i < 128; ++i)
	{
		core::mem::PageSpan span = page_allocator->acquireSpan(32);
		if (!span.isValid())
		{
			break;
		}
		inflight.push_back(span);
	}

	core::mem::PageSpan reused = page_allocator->acquireSpan(2);
	context.Expect(reused.isValid(), "cached span should remain reusable after region growth");
	if (reused.isValid())
	{
		context.Expect(reused.Base == cached_base, "cached span should preserve original base address");
		page_allocator->releaseSpan(reused);
	}

	for (const core::mem::PageSpan& span : inflight)
	{
		page_allocator->releaseSpan(span);
	}
}

void TestRadixTreeMap(TestContext& context)
{
	static core::containers::RadixTreeMap<void*, 12, 16, 10, 10, 32, 256> page_map;
	page_map.clear();
	int value_a = 1;
	int value_b = 2;

	constexpr std::uint64_t key_a = 0x000012340000ull;
	constexpr std::uint64_t key_b = 0x000056780000ull;

	context.Expect(page_map.empty(), "radix tree should start empty");
	context.Expect(page_map.insertOrAssign(key_a, &value_a), "radix tree insert should succeed");
	context.Expect(page_map.insertOrAssign(key_b, &value_b), "radix tree second insert should succeed");

	context.Expect(page_map.find(key_a) == &value_a, "radix tree should find first key");
	context.Expect(page_map.find(key_b) == &value_b, "radix tree should find second key");
	context.Expect(page_map.size() == 2, "radix tree size should track inserted entries");

	context.Expect(page_map.insertOrAssign(key_a, &value_b), "radix tree assign should succeed");
	context.Expect(page_map.find(key_a) == &value_b, "radix tree assign should replace existing value");

	context.Expect(page_map.erase(key_b), "radix tree erase should remove existing key");
	context.Expect(page_map.find(key_b) == nullptr, "radix tree erase should clear value");
	context.Expect(page_map.size() == 1, "radix tree size should shrink after erase");

	constexpr std::uint64_t unsupported_key = (std::uint64_t{1} << 48);
	context.Expect(!page_map.insertOrAssign(unsupported_key, &value_a), "radix tree should reject out-of-range key");
	context.Expect(page_map.find(unsupported_key) == nullptr, "radix tree should return null for out-of-range key");

	page_map.clear();
	context.Expect(page_map.empty(), "radix tree clear should remove all entries");
}

void TestNoThrowAllocationBehavior(TestContext& context)
{
	const std::size_t impossible_size = std::numeric_limits<std::size_t>::max() / 2;

	bool threw_bad_alloc = false;
	try
	{
		(void)core::mem::AllocatorFacade::allocate(impossible_size, alignof(std::max_align_t));
	}
	catch (const std::bad_alloc&)
	{
		threw_bad_alloc = true;
	}
	catch (...)
	{
		context.Expect(false, "throwing allocation should only throw std::bad_alloc");
	}

	context.Expect(threw_bad_alloc, "default allocate should throw std::bad_alloc on allocation failure");

	void* ptr = core::mem::AllocatorFacade::allocate(
		impossible_size,
		alignof(std::max_align_t),
		0,
		false,
		true);
	context.Expect(ptr == nullptr, "allocate with IsNoThrow should return nullptr on allocation failure");

	void* ptr_no_throw = core::mem::AllocatorFacade::allocateNoThrow(impossible_size, alignof(std::max_align_t));
	context.Expect(ptr_no_throw == nullptr, "allocateNoThrow should return nullptr on allocation failure");
}

void TestZeroSizeAllocationBehavior(TestContext& context)
{
	void* ptr = core::mem::AllocatorFacade::allocate(0, alignof(std::max_align_t));
	void* ptr_no_throw = core::mem::AllocatorFacade::allocateNoThrow(0, alignof(std::max_align_t));

	context.Expect(ptr != nullptr, "zero-size allocate should return a non-null sentinel pointer");
	context.Expect(ptr_no_throw != nullptr, "zero-size allocateNoThrow should return a non-null sentinel pointer");
	context.Expect(ptr == ptr_no_throw, "zero-size allocations should use a stable sentinel pointer");

	core::mem::AllocatorFacade::deallocate(ptr, 0, alignof(std::max_align_t));
	core::mem::AllocatorFacade::deallocate(ptr_no_throw, 0, alignof(std::max_align_t));

	void* normal_ptr = core::mem::AllocatorFacade::allocate(64, alignof(std::max_align_t));
	context.Expect(normal_ptr != nullptr, "regular allocation should continue to work after zero-size operations");
	if (normal_ptr)
	{
		core::mem::AllocatorFacade::deallocate(normal_ptr, 64, alignof(std::max_align_t));
	}
}

void TestHookIssueTracking(TestContext& context)
{
	if constexpr (!core::mem::MemoryTracker::isCompiledEnabled())
	{
		context.Expect(true, "hook issue tracking test skipped when tracker is compile-time disabled");
		return;
	}

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

void TestTrackerEngineHookDedup(TestContext& context)
{
	if constexpr (!core::mem::MemoryTracker::isCompiledEnabled())
	{
		context.Expect(true, "engine/hook dedup test skipped when tracker is compile-time disabled");
		return;
	}

	auto& tracker = core::mem::MemoryTracker::global();
	tracker.clear();
	core::mem::MemoryTracker::setHookEnabled(true);

	std::array<std::uint8_t, 64> payload{};
	void* ptr = payload.data();

	core::mem::MemoryEvent alloc_event;
	alloc_event.Type = core::mem::MemoryEventType::Allocate;
	alloc_event.UserPtr = ptr;
	alloc_event.Size = payload.size();
	alloc_event.Alignment = alignof(std::max_align_t);
	alloc_event.AllocationId = 1001;
	alloc_event.Timestamp = core::mem::timestampNs();
	alloc_event.ThreadId = std::this_thread::get_id();

	core::mem::MemoryEvent free_event = alloc_event;
	free_event.Type = core::mem::MemoryEventType::Deallocate;
	free_event.Timestamp = core::mem::timestampNs();

	tracker.onMemoryEvent(alloc_event);
	core::mem::MemoryTracker::onHookAllocation(
		core::mem::HookAllocationOp::Malloc,
		ptr,
		payload.size(),
		core::mem::MemoryTrackerUnknownSize,
		nullptr);
	core::mem::MemoryTracker::onHookFree(
		core::mem::HookAllocationOp::Free,
		ptr,
		core::mem::MemoryTrackerUnknownSize,
		core::mem::MemoryTrackerUnknownSize,
		nullptr);
	tracker.onMemoryEvent(free_event);

	const auto report = tracker.getReport();
	context.Expect(report.Statistics.AllocateCount == 1, "engine allocate count should not be duplicated by hook path");
	context.Expect(report.Statistics.DeallocateCount == 1, "engine deallocate count should not be duplicated by hook path");
	context.Expect(report.Statistics.CurrentLiveBlocks == 0, "dedup path should leave zero live blocks");
	context.Expect(report.Statistics.CurrentLiveBytes == 0, "dedup path should leave zero live bytes");
	context.Expect(report.Statistics.DoubleAllocationCount == 0, "dedup path should not report false double allocation");
	context.Expect(report.Statistics.DoubleFreeCount == 0, "dedup path should not report false double free");
	context.Expect(report.Statistics.InvalidFreeCount == 0, "dedup path should not report false invalid free");

	core::mem::MemoryTracker::setHookEnabled(false);
	tracker.clear();
}

void TestTrackerTimelineCap(TestContext& context)
{
	if constexpr (!core::mem::MemoryTracker::isCompiledEnabled())
	{
		context.Expect(true, "timeline cap test skipped when tracker is compile-time disabled");
		return;
	}

	auto& tracker = core::mem::MemoryTracker::global();
	tracker.clear();
	core::mem::MemoryTracker::setHookEnabled(true);
	core::mem::MemoryTracker::setMaxTimelineEntries(8);

	for (std::size_t i = 0; i < 32; ++i)
	{
		core::mem::MemoryTracker::onHookAllocationFailure(
			core::mem::HookAllocationOp::Malloc,
			16,
			core::mem::MemoryTrackerUnknownSize,
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x1000 + i)));
	}

	context.Expect(tracker.getEventCount() == 8, "timeline should be capped to configured max entries");

	core::mem::MemoryTracker::setMaxTimelineEntries(core::mem::MemoryTrackerDefaultMaxTimelineEntries);
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
	core::mem::AllocatorFacade::configure(config);
	TestSmallAllocationRoundTrip(context);
	TestConcurrentAllocation(context);
	TestStlCompatibility(context);
	TestPmrCompatibility(context);
	TestObserversAndLeakDetection(context);
	TestDebugGuardDetection(context);
	TestDeallocateNullptr(context);
	TestUseAfterFreeDetection(context);
	TestQuarantineDoesNotDrainOnAllocate(context);
	TestPrewarmApi(context);
	TestAllocatorStatsApi(context);
	TestPageAllocatorSpanCacheReuseSafety(context);
	TestRadixTreeMap(context);
	TestNoThrowAllocationBehavior(context);
	TestZeroSizeAllocationBehavior(context);
	TestHookIssueTracking(context);
	TestTrackerEngineHookDedup(context);
	TestTrackerTimelineCap(context);
	BenchmarkLargeAllocation(context);
	BenchmarkHighFrequencyAllocation(context);
	core::mem::AllocatorFacade::onThreadExit();
	return context.failures;
}
