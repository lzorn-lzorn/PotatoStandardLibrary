#include <cstdint>
#include <memory>
#include <print>
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
}

void TestDelegateSharedWeakLifetime(TestContext& context)
{
	core::delegate<int, int> d;
	auto receiver = std::make_shared<Receiver>(Receiver{.Bias = 3});

	d.bind_shared<&Receiver::AddConst>(receiver);
	context.Expect(d.execute(9) == 12, "delegate bind_shared should invoke while object is alive");

	receiver.reset();
	ExpectThrows<std::bad_function_call>(
		context,
		[&]() {
			(void)d.execute(1);
		},
		"delegate bind_shared should throw when tracked object expires"
	);

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

	const std::vector<int> result = multicast.collect(3);
	context.Expect(result.size() == 3, "collect should return one result per registered delegate");
	context.Expect(result[0] == 4, "collect should preserve registration order for first callback");
	context.Expect(result[1] == 5, "collect should preserve registration order for second callback");
	context.Expect(result[2] == 30, "collect should include static callback result");
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
	TestDelegateLambdaAndConversions(context);
	TestDelegateResetAndUnboundExecute(context);
	TestMoveOnlyDelegate(context);
	TestMulticastBroadcastAndRemove(context);
	TestMulticastCollect(context);

	if (context.failures == 0)
	{
		std::println("[PASS] DelegateTest");
		return 0;
	}

	std::println("[FAIL] DelegateTest with {} failure(s)", context.failures);
	return 1;
}
