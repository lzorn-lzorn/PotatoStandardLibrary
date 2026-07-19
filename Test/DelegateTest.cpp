#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <print>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <Core/delegate.h>

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

template <typename ExceptionType, typename Func>
void ExpectThrows(TestContext& context, Func&& func, const char* message)
{
	bool thrown = false;
	try
	{
		std::forward<Func>(func)();
	}
	catch (const ExceptionType&)
	{
		thrown = true;
	}
	catch (...)
	{
		thrown = false;
	}

	context.Expect(thrown, message);
}

int AddOne(int value)
{
	return value + 1;
}

int TimesTen(int value)
{
	return value * 10;
}

struct Receiver
{
	int Bias = 0;
	int Total = 0;

	int Add(int value)
	{
		return value + Bias;
	}

	int AddConst(int value) const
	{
		return value + Bias;
	}

	void Accumulate(int value)
	{
		Total += value;
	}
};

struct ThrowingMoveCallable
{
	int Bias = 0;
	bool* MoveAttempted = nullptr;

	ThrowingMoveCallable(int bias, bool* move_attempted)
		: Bias(bias), MoveAttempted(move_attempted)
	{
	}

	ThrowingMoveCallable(const ThrowingMoveCallable&) = default;

	ThrowingMoveCallable(ThrowingMoveCallable&& other) noexcept(false)
		: Bias(other.Bias), MoveAttempted(other.MoveAttempted)
	{
		*MoveAttempted = true;
		throw std::runtime_error("ThrowingMoveCallable must not be moved inline");
	}

	int operator()(int value) const
	{
		return value + Bias;
	}
};

struct LargeCallable
{
	std::array<std::byte, core::DelegateSmallStorageSize + 1> Padding{};
	int Bias = 0;

	int operator()(int value) const
	{
		return value + Bias;
	}
};

static_assert(sizeof(LargeCallable) > core::DelegateSmallStorageSize);
static_assert(!std::is_nothrow_move_constructible_v<ThrowingMoveCallable>);

using DisabledSboDelegate = core::delegate<int, int>;
using EnabledSboDelegate = core::basic_delegate<int, true, int>;
using DisabledSboMoveOnlyDelegate = core::move_only_delegate<int, int>;
using EnabledSboMoveOnlyDelegate = core::basic_move_only_delegate<int, true, int>;

static_assert(core::details::InlineStorage<false>::StorageSize == 0);
static_assert(core::details::InlineStorage<true>::StorageSize == core::DelegateSmallStorageSize);
static_assert(sizeof(EnabledSboDelegate) > sizeof(DisabledSboDelegate));
static_assert(sizeof(EnabledSboMoveOnlyDelegate) > sizeof(DisabledSboMoveOnlyDelegate));

struct DeferredDestructionCallback
{
	core::multicast_delegate<void()>* Owner = nullptr;
	core::DelegateHandle* Handle = nullptr;
	int* DestructionCount = nullptr;
	bool* DestroyedDuringInvocation = nullptr;

	~DeferredDestructionCallback()
	{
		++*DestructionCount;
	}

	void operator()() const
	{
		Owner->remove(*Handle);
		*DestroyedDuringInvocation = *DestructionCount != 0;
	}
};

void TestDelegateBindStatic(TestContext& context)
{
	core::delegate<int, int> d;
	d.bind_static<&AddOne>();

	context.Expect(d.bound(), "delegate should be bound after bind_static");
	context.Expect(d.execute(41) == 42, "delegate bind_static should invoke target function");
}

void TestDelegateBindRaw(TestContext& context)
{
	Receiver receiver{.Bias = 7};
	core::delegate<int, int> d;
	d.bind_raw<&Receiver::Add>(&receiver);

	context.Expect(d.execute(5) == 12, "delegate bind_raw should invoke member function");

	const Receiver const_receiver{.Bias = 9};
	core::delegate<int, int> const_delegate;
	const_delegate.bind_raw<&Receiver::AddConst>(&const_receiver);
	context.Expect(const_delegate.execute(5) == 14, "delegate bind_raw should invoke a const member on a const instance");
}

void TestDelegateSharedWeakLifetime(TestContext& context)
{
	core::delegate<int, int> d;
	auto receiver = std::make_shared<Receiver>(Receiver{.Bias = 3});
	std::weak_ptr<Receiver> shared_weak = receiver;

	d.bind_shared<&Receiver::AddConst>(receiver);
	context.Expect(d.execute(9) == 12, "delegate bind_shared should invoke while object is alive");

	receiver.reset();
	context.Expect(!shared_weak.expired(), "delegate bind_shared should retain strong ownership after caller releases its shared_ptr");
	context.Expect(d.execute(1) == 4, "delegate bind_shared should remain callable after caller releases its shared_ptr");
	d.reset();
	context.Expect(shared_weak.expired(), "delegate bind_shared should release ownership when reset");

	auto receiver2 = std::make_shared<Receiver>(Receiver{.Bias = 4});
	std::weak_ptr<Receiver> weak = receiver2;
	d.bind_weak<&Receiver::Add>(weak);
	context.Expect(d.execute(8) == 12, "delegate bind_weak should invoke while weak target is alive");

	receiver2.reset();
	ExpectThrows<std::bad_function_call>(
		context,
		[&]() {
			(void)d.execute(2);
		},
		"delegate bind_weak should throw when weak target expires"
	);
}

void TestDelegateHeapFallbackAndNoThrowMove(TestContext& context)
{
	bool throwing_move_attempted = false;
	ThrowingMoveCallable throwing_move_callable{6, &throwing_move_attempted};
	core::delegate<int, int> throwing_move_delegate;
	throwing_move_delegate.bind_lambda(throwing_move_callable);

	core::delegate<int, int> moved_throwing_delegate = std::move(throwing_move_delegate);
	context.Expect(!throwing_move_attempted, "a small callable with a throwing move constructor should use heap fallback");
	context.Expect(moved_throwing_delegate.execute(4) == 10, "heap fallback should preserve throwing-move callable behavior");

	LargeCallable large_callable{.Bias = 8};
	core::delegate<int, int> large_delegate;
	large_delegate.bind_lambda(large_callable);
	core::delegate<int, int> copied_large_delegate = large_delegate;
	core::delegate<int, int> moved_large_delegate = std::move(large_delegate);

	context.Expect(copied_large_delegate.execute(2) == 10, "large callable copy should use independent heap-backed storage");
	context.Expect(moved_large_delegate.execute(3) == 11, "large callable move should preserve heap-backed behavior");
}

void TestDelegateSboConfigurations(TestContext& context)
{
	DisabledSboDelegate disabled_sbo;
	disabled_sbo.bind_lambda([bias = 2](int value) {
		return value + bias;
	});
	context.Expect(disabled_sbo.execute(3) == 5, "SBO-disabled delegate should execute heap-backed small callables");

	EnabledSboDelegate enabled_sbo;
	enabled_sbo.bind_lambda([bias = 4](int value) {
		return value + bias;
	});
	EnabledSboDelegate copied_enabled_sbo = enabled_sbo;
	EnabledSboDelegate moved_enabled_sbo = std::move(enabled_sbo);
	context.Expect(copied_enabled_sbo.execute(3) == 7, "SBO-enabled delegate should copy inline callables");
	context.Expect(moved_enabled_sbo.execute(5) == 9, "SBO-enabled delegate should move inline callables");

	EnabledSboMoveOnlyDelegate enabled_move_only_sbo;
	enabled_move_only_sbo.bind_lambda([value = std::make_unique<int>(6)](int input) {
		return *value + input;
	});
	context.Expect(enabled_move_only_sbo.execute(1) == 7, "SBO-enabled move-only delegate should execute inline-capable callables");
}

void TestDelegateLambdaAndConversions(TestContext& context)
{
	core::delegate<int, int> d;
	d.bind_lambda([factor = 3](int value) {
		return value * factor;
	});

	auto std_fn = d.as_std_function();
	context.Expect(std_fn(4) == 12, "as_std_function should preserve callable behavior");

	auto move_only_fn = std::move(d).as_std_move_only_function();
	context.Expect(move_only_fn(5) == 15, "as_std_move_only_function should preserve callable behavior");
}

void TestDelegateResetAndUnboundExecute(TestContext& context)
{
	core::delegate<void, int> d;
	ExpectThrows<std::bad_function_call>(
		context,
		[&]() {
			d.execute(1);
		},
		"executing an unbound delegate should throw bad_function_call"
	);

	d.bind_lambda([](int) {});
	d.reset();
	context.Expect(!d.bound(), "delegate should report unbound after reset");

	ExpectThrows<std::bad_function_call>(
		context,
		[&]() {
			d.execute(1);
		},
		"executing a reset delegate should throw bad_function_call"
	);
}

void TestMoveOnlyDelegate(TestContext& context)
{
	core::move_only_delegate<int, int> d;
	auto state = std::make_unique<int>(10);

	d.bind_lambda([value = std::move(state)](int in) {
		return *value + in;
	});

	context.Expect(d.execute(2) == 12, "move_only_delegate should support move-only lambda captures");

	auto std_mof = std::move(d).as_std_move_only_function();
	context.Expect(std_mof(5) == 15, "move_only_delegate conversion should preserve callable behavior");
}

void TestMulticastBroadcastAndRemove(TestContext& context)
{
	core::multicast_delegate<void(int)> multicast;
	int sum_a = 0;
	int sum_b = 0;

	auto h1 = multicast.add_lambda([&](int value) {
		sum_a += value;
	});

	core::delegate<void, int> d;
	d.bind_lambda([&](int value) {
		sum_b += value * 2;
	});
	auto h2 = multicast.add(d);

	context.Expect(multicast.bound(), "multicast should report bound after adding delegates");
	context.Expect(multicast.size() == 2, "multicast size should match number of delegates");
	context.Expect(multicast.contains(h1), "multicast should contain first handle");
	context.Expect(multicast.contains(h2), "multicast should contain second handle");

	multicast.broadcast(5);
	context.Expect(sum_a == 5, "multicast should broadcast to first delegate");
	context.Expect(sum_b == 10, "multicast should broadcast to second delegate");

	context.Expect(multicast.remove(h1), "multicast remove should succeed for valid handle");
	context.Expect(!multicast.contains(h1), "removed handle should no longer be present");

	core::DelegateHandle invalid{};
	invalid.Id = 999999;
	context.Expect(!multicast.remove(invalid), "multicast remove should fail for unknown handle");

	multicast.broadcast(3);
	context.Expect(sum_a == 5, "removed delegate should not receive further broadcasts");
	context.Expect(sum_b == 16, "remaining delegate should still receive broadcasts");

	multicast.clear();
	context.Expect(!multicast.bound(), "multicast should report unbound after clear");	
	context.Expect(multicast.size() == 0, "multicast size should be zero after clear");
}

void TestMulticastCollect(TestContext& context)
{
	core::multicast_delegate<int(int)> multicast;
	multicast.add_lambda([](int value) {
		return value + 1;
	});
	multicast.add_lambda([](int value) {
		return value + 2;
	});
	multicast.add_static<&TimesTen>();

	const core::multicast_delegate<int(int)>& const_multicast = multicast;
	const std::vector<int> result = const_multicast.collect(3);
	context.Expect(result.size() == 3, "collect should return one result per registered delegate");
	context.Expect(result[0] == 4, "collect should preserve registration order for first callback");
	context.Expect(result[1] == 5, "collect should preserve registration order for second callback");
	context.Expect(result[2] == 30, "collect should include static callback result");
}

void TestMulticastValueArguments(TestContext& context)
{
	core::multicast_delegate<void(std::string)> multicast;
	std::string first;
	std::string second;

	multicast.add_lambda([&](std::string value) {
		first = std::move(value);
	});
	multicast.add_lambda([&](std::string value) {
		second = std::move(value);
	});

	const core::multicast_delegate<void(std::string)>& const_multicast = multicast;
	const_multicast.broadcast(std::string{"potato"});
	context.Expect(first == "potato", "first multicast listener should receive the complete value argument");
	context.Expect(second == "potato", "later multicast listeners should not receive moved-from value arguments");
}

void TestMulticastMutationSafety(TestContext& context)
{
	core::multicast_delegate<void()> multicast;
	std::vector<int> order;
	core::DelegateHandle self;
	core::DelegateHandle later;
	bool added = false;
	bool later_removed = false;

	self = multicast.add_lambda([&]() {
		order.push_back(1);
		context.Expect(multicast.remove(self), "self-removal during broadcast should succeed");
	});

	multicast.add_lambda([&]() {
		order.push_back(2);
		if (!later_removed)
		{
			later_removed = true;
			context.Expect(multicast.remove(later), "removing a later callback during broadcast should succeed");
		}
		if (!added)
		{
			added = true;
			multicast.add_lambda([&]() {
				order.push_back(4);
			});
		}
	});

	later = multicast.add_lambda([&]() {
		order.push_back(3);
	});

	multicast.broadcast();
	context.Expect(
		order == std::vector<int>{1, 2},
		"self-removal and later removal should suppress only callbacks not yet invoked"
	);

	multicast.broadcast();
	context.Expect(
		order == std::vector<int>{1, 2, 2, 4},
		"a callback added during broadcast should run only on the next broadcast"
	);
}

void TestMulticastDeferredDestruction(TestContext& context)
{
	core::multicast_delegate<void()> multicast;
	core::DelegateHandle handle;
	int destruction_count = 0;
	bool destroyed_during_invocation = false;

	handle = multicast.add_lambda(DeferredDestructionCallback{
		.Owner = &multicast,
		.Handle = &handle,
		.DestructionCount = &destruction_count,
		.DestroyedDuringInvocation = &destroyed_during_invocation
	});

	destruction_count = 0;
	multicast.broadcast();

	context.Expect(!destroyed_during_invocation, "self-removal must not destroy the callback while its invocation is active");
	context.Expect(destruction_count == 1, "deferred callback destruction should occur after the outermost broadcast returns");
	context.Expect(!multicast.contains(handle), "a self-removed callback handle should be invalid immediately");
}

void TestMulticastClearDuringBroadcast(TestContext& context)
{
	core::multicast_delegate<void()> multicast;
	std::vector<int> calls;

	multicast.add_lambda([&]() {
		calls.push_back(1);
		multicast.clear();
	});
	const auto later = multicast.add_lambda([&]() {
		calls.push_back(2);
	});

	multicast.broadcast();
	context.Expect(calls == std::vector<int>{1}, "clear during broadcast should suppress callbacks that have not run yet");
	context.Expect(!multicast.bound() && !multicast.contains(later), "clear during broadcast should invalidate every registration immediately");
}

void TestMulticastNestedBroadcastAndOrder(TestContext& context)
{
	core::multicast_delegate<void(int)> multicast;
	std::vector<int> events;
	bool entered_nested_broadcast = false;

	multicast.add_lambda([&](int depth) {
		events.push_back(10 + depth);
		if (depth == 0 && !entered_nested_broadcast)
		{
			entered_nested_broadcast = true;
			multicast.broadcast(1);
		}
	});
	multicast.add_lambda([&](int depth) {
		events.push_back(20 + depth);
	});

	multicast.broadcast(0);
	context.Expect(
		events == std::vector<int>{10, 11, 21, 20},
		"nested broadcasts should preserve registration order per dispatch"
	);

	core::multicast_delegate<void()> ordering;
	std::vector<int> order;
	const auto first = ordering.add_lambda([&]() { order.push_back(1); });
	const auto second = ordering.add_lambda([&]() { order.push_back(2); });
	const auto third = ordering.add_lambda([&]() { order.push_back(3); });
	context.Expect(ordering.remove(second), "removing middle callback should succeed");
	ordering.broadcast();
	context.Expect(order == std::vector<int>{1, 3}, "deleting a callback should retain surviving registration order");
	context.Expect(ordering.contains(first) && ordering.contains(third), "surviving callbacks should retain valid handles");
}

void TestMulticastHandleGenerationReuse(TestContext& context)
{
	core::multicast_delegate<void()> multicast;
	const auto original = multicast.add_lambda([]() {});
	context.Expect(multicast.remove(original), "initial callback removal should succeed");

	const auto replacement = multicast.add_lambda([]() {});
	context.Expect(!multicast.contains(original), "a stale handle must not resolve after its slot is reused");
	context.Expect(!multicast.remove(original), "a stale handle must not remove a replacement callback");
	context.Expect(multicast.contains(replacement), "replacement callback should have a valid handle");
	context.Expect(original.Id != replacement.Id, "generation reuse should produce a distinct opaque handle ID");
}

} // namespace

int main()
{
	static_assert(std::copy_constructible<core::delegate<int, int>>);
	static_assert(!std::copy_constructible<core::move_only_delegate<int, int>>);

	TestContext context;

	TestDelegateBindStatic(context);
	TestDelegateBindRaw(context);
	TestDelegateSharedWeakLifetime(context);
	TestDelegateHeapFallbackAndNoThrowMove(context);
	TestDelegateSboConfigurations(context);
	TestDelegateLambdaAndConversions(context);
	TestDelegateResetAndUnboundExecute(context);
	TestMoveOnlyDelegate(context);
	TestMulticastBroadcastAndRemove(context);
	TestMulticastCollect(context);
	TestMulticastValueArguments(context);
	TestMulticastMutationSafety(context);
	TestMulticastDeferredDestruction(context);
	TestMulticastClearDuringBroadcast(context);
	TestMulticastNestedBroadcastAndOrder(context);
	TestMulticastHandleGenerationReuse(context);

	std::println(
		"[INFO] sizeof Delegate is {}; sizeof SBO Delegate is {}; sizeof MultiDelegate is {};",
		sizeof(DisabledSboDelegate),
		sizeof(EnabledSboDelegate),
		sizeof(core::multicast_delegate<void()>)
	);
	if (context.failures == 0)
	{
		std::println("[PASS] DelegateTest");
		return 0;
	}

	std::println("[FAIL] DelegateTest with {} failure(s)", context.failures);
	return 1;
}
