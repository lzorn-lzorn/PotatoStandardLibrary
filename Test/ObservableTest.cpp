#include <cstddef>
#include <concepts>
#include <print>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <Observable.h>

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
	ptd::Observable<int> observable(std::in_place, 3);
	observable.SetValue(5);
	observable.Modify([](int& value) {
		value *= 2;
	});
	ptd::Observable<int>::Modify([](int& value, int delta) {
		value += delta;
	}, observable, 1);
	return  observable.GetValue() == 11;
}

constexpr bool ObservableDefaultConstructionValueInitializes()
{
	ptd::Observable<int> observable;
	return observable.GetValue() == 0;
}

template <typename Ty>
void AssignToSelf(Ty& value)
{
	Ty& self = value;
	value = self;
}

void TestContainerMutationNotifications(TestContext& context)
{
	ptd::Observable<std::vector<int>> observable;
	std::size_t notification_count = 0;
	std::size_t last_size = 0;

	context.Expect(observable.GetValue().empty(), "default-constructed observable should report an empty value");

	observable.Subscribe([&](const std::vector<int>& values) {
		++notification_count;
		last_size = values.size();
	});

	observable.SetValue({1, 2});
	context.Expect(notification_count == 1, "SetValue should notify observers once");
	context.Expect(last_size == 2, "SetValue should publish the updated container size");

	observable.Modify([](std::vector<int>& values) {
		values.push_back(3);
	});
	context.Expect(notification_count == 2, "Modify should notify after appending to the container");
	context.Expect(last_size == 3, "Modify should expose the appended size");

	ptd::Observable<std::vector<int>>::Modify([](std::vector<int>& values) {
		values.erase(values.begin());
	}, observable);
	context.Expect(notification_count == 3, "static Modify should notify observers");
	context.Expect(last_size == 2, "static Modify should publish the resized container");

	observable.Modify([](std::vector<int>& values) {
		values[0] = 42;
		values.erase(values.begin() + 1);
	});
	context.Expect(notification_count == 4, "Modify should notify after mutating and shrinking the container");
	context.Expect(last_size == 1, "container shrink should be visible to observers");

	const auto& current = observable.GetValue();
	context.Expect(current.size() == 1, "final container should contain one element");
	context.Expect(current[0] == 42, "final container value should match the last mutation");
}

void TestMultipleObserversKeepRegistrationOrder(TestContext& context)
{
	ptd::Observable<int> observable(0);
	std::vector<int> order;

	observable.Subscribe([&](const int& value) {
		order.push_back(value + 10);
	});
	observable.Subscribe([&](const int& value) {
		order.push_back(value + 20);
	});

	observable.SetValue(5);
	context.Expect(order.size() == 2, "all subscribed observers should be called");
	context.Expect(order[0] == 15, "observers should be invoked in subscription order");
	context.Expect(order[1] == 25, "later observers should see the same committed value");
}

void TestCopyConstructionDoesNotCopyObservers(TestContext& context)
{
	ptd::Observable<int> source(7);
	int source_notifications = 0;

	source.Subscribe([&](const int&) {
		++source_notifications;
	});

	ptd::Observable<int> copy(source);
	copy.SetValue(9);

	context.Expect(copy.GetValue() == 9, "copy-constructed observable should hold the copied value");
	context.Expect(source_notifications == 0, "copy construction should not duplicate source observers into the copy");

	source.SetValue(11);
	context.Expect(source_notifications == 1, "source observers should remain attached to the source after copy construction");
}

void TestCopyAssignmentPreservesDestinationObservers(TestContext& context)
{
	ptd::Observable<std::string> source("alpha");
	ptd::Observable<std::string> destination("omega");
	int source_notifications = 0;
	int destination_notifications = 0;
	std::string seen_value;

	source.Subscribe([&](const std::string&) {
		++source_notifications;
	});
	destination.Subscribe([&](const std::string& value) {
		++destination_notifications;
		seen_value = value;
	});

	destination = source;

	context.Expect(destination.GetValue() == "alpha", "copy assignment should copy the source value");
	context.Expect(destination_notifications == 1, "copy assignment should notify destination observers about the new value");
	context.Expect(seen_value == "alpha", "destination observers should see the copied value");
	context.Expect(source_notifications == 0, "copy assignment should not trigger source observers");

	destination.SetValue("beta");
	context.Expect(destination_notifications == 2, "destination observers should remain registered after copy assignment");
	context.Expect(source_notifications == 0, "destination mutations should not call source observers after copy assignment");
}

void TestMoveConstructionDoesNotTransferObservers(TestContext& context)
{
	ptd::Observable<int> source(12);
	int source_notifications = 0;

	source.Subscribe([&](const int&) {
		++source_notifications;
	});

	ptd::Observable<int> moved(std::move(source));
	moved.SetValue(20);

	context.Expect(moved.GetValue() == 20, "move construction should transfer the value state");
	context.Expect(source_notifications == 0, "move construction should not transfer source observers into the destination");
}

void TestMoveAssignmentPreservesDestinationObservers(TestContext& context)
{
	ptd::Observable<int> source(3);
	ptd::Observable<int> destination(8);
	int source_notifications = 0;
	int destination_notifications = 0;
	int seen_value = 0;

	source.Subscribe([&](const int&) {
		++source_notifications;
	});
	destination.Subscribe([&](const int& value) {
		++destination_notifications;
		seen_value = value;
	});

	destination = std::move(source);

	context.Expect(destination.GetValue() == 3, "move assignment should move the source value into the destination");
	context.Expect(destination_notifications == 1, "move assignment should notify destination observers");
	context.Expect(seen_value == 3, "destination observers should see the moved value");
	context.Expect(source_notifications == 0, "move assignment should not trigger source observers");

	destination.SetValue(13);
	context.Expect(destination_notifications == 2, "destination observers should remain registered after move assignment");
	context.Expect(source_notifications == 0, "destination mutations should not call source observers after move assignment");
}

void TestSelfAssignmentKeepsObservers(TestContext& context)
{
	ptd::Observable<int> observable(4);
	int notifications = 0;

	observable.Subscribe([&](const int&) {
		++notifications;
	});

	AssignToSelf(observable);
	observable.SetValue(6);

	context.Expect(observable.GetValue() == 6, "self copy assignment should leave the observable usable");
	context.Expect(notifications == 1, "self copy assignment should preserve observer registration exactly once");
}

void TestValueAssignmentFallbackForNonMoveAssignable(TestContext& context)
{
	ptd::Observable<CopyAssignOnly> observable(std::in_place, 1);
	observable = CopyAssignOnly{9};
	context.Expect(observable.GetValue().value == 9, "rvalue value assignment should fall back to copy assignment when move assignment is unavailable");
}

}  // namespace

int main()
{
	static_assert(std::copy_constructible<ptd::Observable<std::string>>);
	static_assert(std::is_copy_assignable_v<ptd::Observable<std::string>>);
	static_assert(!std::copy_constructible<ptd::Observable<MoveOnly>>);
	static_assert(!std::is_copy_assignable_v<ptd::Observable<MoveOnly>>);
	static_assert(std::move_constructible<ptd::Observable<MoveOnly>>);
	static_assert(std::is_move_assignable_v<ptd::Observable<MoveOnly>>);
	static_assert(!std::default_initializable<ptd::Observable<NonDefaultConstructible>>);
	static_assert(std::constructible_from<ptd::Observable<NonDefaultConstructible>, std::in_place_t, int>);
	static_assert(std::same_as<typename ptd::Observable<int>::value_type, int>);
	static_assert(ObservableDefaultConstructionValueInitializes());
	static_assert(ObservableConstexprWorks());

	TestContext context;
	TestContainerMutationNotifications(context);
	TestMultipleObserversKeepRegistrationOrder(context);
	TestCopyConstructionDoesNotCopyObservers(context);
	TestCopyAssignmentPreservesDestinationObservers(context);
	TestMoveConstructionDoesNotTransferObservers(context);
	TestMoveAssignmentPreservesDestinationObservers(context);
	TestSelfAssignmentKeepsObservers(context);
	TestValueAssignmentFallbackForNonMoveAssignable(context);

	std::println("Final sizeof Observable int: {}", sizeof(ptd::Observable<int>));
	std::println("Final sizeof vector int: {}", sizeof(std::vector<int*>));
	return context.failures;
}