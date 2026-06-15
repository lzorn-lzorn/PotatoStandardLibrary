#include <cstddef>
#include <concepts>
#include <print>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <observed.h>

namespace
{

struct MoveOnly
{
	int value = 0;

	constexpr MoveOnly() = default;
	explicit constexpr MoveOnly(int initial_value) noexcept : value(initial_value) {}
	MoveOnly(const MoveOnly&) = delete;
	MoveOnly& operator=(const MoveOnly&) = delete;
	constexpr MoveOnly(MoveOnly&&) noexcept = default;
	constexpr MoveOnly& operator=(MoveOnly&&) noexcept = default;
};

struct NonDefaultConstructible
{
	int value;

	NonDefaultConstructible() = delete;
	explicit constexpr NonDefaultConstructible(int initial_value) noexcept : value(initial_value) {}
	constexpr NonDefaultConstructible(const NonDefaultConstructible&) noexcept = default;
	constexpr NonDefaultConstructible& operator=(const NonDefaultConstructible&) noexcept = default;
	constexpr NonDefaultConstructible(NonDefaultConstructible&&) noexcept = default;
	constexpr NonDefaultConstructible& operator=(NonDefaultConstructible&&) noexcept = default;
};

struct CopyAssignOnly
{
	int value = 0;

	constexpr CopyAssignOnly() = default;
	explicit constexpr CopyAssignOnly(int initial_value) noexcept : value(initial_value) {}
	constexpr CopyAssignOnly(const CopyAssignOnly&) noexcept = default;
	constexpr CopyAssignOnly& operator=(const CopyAssignOnly&) noexcept = default;
	constexpr CopyAssignOnly(CopyAssignOnly&&) noexcept = default;
	CopyAssignOnly& operator=(CopyAssignOnly&&) = delete;
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

constexpr bool ObservableConstexprWorks()
{
	core::observed<int> observable(std::in_place, 3);
	observable.set_value(5);
	observable.modify([](int& value) {
		value *= 2;
	});

	return  observable.value() == 11;
}

constexpr bool ObservableDefaultConstructionValueInitializes()
{
	core::observed<int> observable;
	return observable.value() == 0;
}

template <typename Ty>
void AssignToSelf(Ty& value)
{
	Ty& self = value;
	value = self;
}

void TestContainerMutationNotifications(TestContext& context)
{
	core::observed<std::vector<int>> observable;
	std::size_t notification_count = 0;
	std::size_t last_size = 0;

	context.Expect(observable.value().empty(), "default-constructed observable should report an empty value");

	observable.subscribe([&](const std::vector<int>& values) {
		++notification_count;
		last_size = values.size();
	});

	observable.set_value({1, 2});
	context.Expect(notification_count == 1, "set_value should notify observers once");
	context.Expect(last_size == 2, "set_value should publish the updated container size");

	observable.modify([](std::vector<int>& values) {
		values.push_back(3);
	});
	context.Expect(notification_count == 2, "modify should notify after appending to the container");
	context.Expect(last_size == 3, "modify should expose the appended size");

	observable.modify([](std::vector<int>& values) {
		values[0] = 42;
		values.erase(values.begin() + 1, values.end()); // 删除索引 1 及之后所有元素
	});
	
	context.Expect(last_size == 1, "container shrink should be visible to observers");

	const auto& current = observable.value();
	context.Expect(current.size() == 1, "final container should contain one element");
	context.Expect(current[0] == 42, "final container value should match the last mutation");
}

void TestMultipleObserversKeepRegistrationOrder(TestContext& context)
{
	core::observed<int> observable(0);
	std::vector<int> order;

	observable.subscribe([&](const int& value) {
		order.push_back(value + 10);
	});
	observable.subscribe([&](const int& value) {
		order.push_back(value + 20);
	});

	observable.set_value(5);
	context.Expect(order.size() == 2, "all subscribed observers should be called");
	context.Expect(order[0] == 15, "observers should be invoked in subscription order");
	context.Expect(order[1] == 25, "later observers should see the same committed value");
}

void TestCopyConstructionDoesNotCopyObservers(TestContext& context)
{
	core::observed<int> source(7);
	int source_notifications = 0;

	source.subscribe([&](const int&) {
		++source_notifications;
	});

	core::observed<int> copy(source);
	copy.set_value(9);

	context.Expect(copy.value() == 9, "copy-constructed observable should hold the copied value");
	context.Expect(source_notifications == 0, "copy construction should not duplicate source observers into the copy");

	source.set_value(11);
	context.Expect(source_notifications == 1, "source observers should remain attached to the source after copy construction");
}

void TestCopyAssignmentPreservesDestinationObservers(TestContext& context)
{
	core::observed<std::string> source("alpha");
	core::observed<std::string> destination("omega");
	int source_notifications = 0;
	int destination_notifications = 0;
	std::string seen_value;

	source.subscribe([&](const std::string&) {
		++source_notifications;
	});
	destination.subscribe([&](const std::string& value) {
		++destination_notifications;
		seen_value = value;
	});

	destination = source;

	context.Expect(destination.value() == "alpha", "copy assignment should copy the source value");
	context.Expect(destination_notifications == 1, "copy assignment should notify destination observers about the new value");
	context.Expect(seen_value == "alpha", "destination observers should see the copied value");
	context.Expect(source_notifications == 0, "copy assignment should not trigger source observers");

	destination.set_value("beta");
	context.Expect(destination_notifications == 2, "destination observers should remain registered after copy assignment");
	context.Expect(source_notifications == 0, "destination mutations should not call source observers after copy assignment");
}

void TestMoveConstructionDoesNotTransferObservers(TestContext& context)
{
	core::observed<int> source(12);
	int source_notifications = 0;

	source.subscribe([&](const int&) {
		++source_notifications;
	});

	core::observed<int> moved(std::move(source));
	moved.set_value(20);

	context.Expect(moved.value() == 20, "move construction should transfer the value state");
	context.Expect(source_notifications == 0, "move construction should not transfer source observers into the destination");
}

void TestMoveAssignmentPreservesDestinationObservers(TestContext& context)
{
	core::observed<int> source(3);
	core::observed<int> destination(8);
	int source_notifications = 0;
	int destination_notifications = 0;
	int seen_value = 0;

	source.subscribe([&](const int&) {
		++source_notifications;
	});
	destination.subscribe([&](const int& value) {
		++destination_notifications;
		seen_value = value;
	});

	destination = std::move(source);

	context.Expect(destination.value() == 3, "move assignment should move the source value into the destination");
	context.Expect(destination_notifications == 1, "move assignment should notify destination observers");
	context.Expect(seen_value == 3, "destination observers should see the moved value");
	context.Expect(source_notifications == 0, "move assignment should not trigger source observers");

	destination.set_value(13);
	context.Expect(destination_notifications == 2, "destination observers should remain registered after move assignment");
	context.Expect(source_notifications == 0, "destination mutations should not call source observers after move assignment");
}

void TestSelfAssignmentKeepsObservers(TestContext& context)
{
	core::observed<int> observable(4);
	int notifications = 0;

	observable.subscribe([&](const int&) {
		++notifications;
	});

	AssignToSelf(observable);
	observable.set_value(6);

	context.Expect(observable.value() == 6, "self copy assignment should leave the observable usable");
	context.Expect(notifications == 1, "self copy assignment should preserve observer registration exactly once");
}

void TestValueAssignmentFallbackForNonMoveAssignable(TestContext& context)
{
	core::observed<CopyAssignOnly> observable(std::in_place, 1);
	observable = CopyAssignOnly{9};
	context.Expect(observable.value().value == 9, "rvalue value assignment should fall back to copy assignment when move assignment is unavailable");
}

}  // namespace

int main()
{
	static_assert(std::copy_constructible<core::observed<std::string>>);
	static_assert(std::is_copy_assignable_v<core::observed<std::string>>);
	static_assert(!std::copy_constructible<core::observed<MoveOnly>>);
	static_assert(!std::is_copy_assignable_v<core::observed<MoveOnly>>);
	static_assert(std::move_constructible<core::observed<MoveOnly>>);
	static_assert(std::is_move_assignable_v<core::observed<MoveOnly>>);
	static_assert(!std::default_initializable<core::observed<NonDefaultConstructible>>);
	static_assert(std::constructible_from<core::observed<NonDefaultConstructible>, std::in_place_t, int>);
	static_assert(std::same_as<typename core::observed<int>::value_type, int>);


	ObservableDefaultConstructionValueInitializes();
	ObservableConstexprWorks();


	TestContext context;
	TestContainerMutationNotifications(context);
	TestMultipleObserversKeepRegistrationOrder(context);
	TestCopyConstructionDoesNotCopyObservers(context);
	TestCopyAssignmentPreservesDestinationObservers(context);
	TestMoveConstructionDoesNotTransferObservers(context);
	TestMoveAssignmentPreservesDestinationObservers(context);
	TestSelfAssignmentKeepsObservers(context);
	TestValueAssignmentFallbackForNonMoveAssignable(context);

	std::println("Final sizeof observed int: {}", sizeof(core::observed<int>));
	std::println("Final sizeof vector int: {}", sizeof(std::vector<int*>));
	return context.failures;
}