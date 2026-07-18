#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace core
{
/**
 * @brief Inline byte budget used by delegate small-object optimization.
 * @breif Inline byte budget used by delegate small-object optimization.
 * @usage
 *   - constexpr std::size_t inline_budget = core::DelegateSmallStorageSize;
 */
constexpr std::uint32_t DelegateSmallStorageSize = 64;

/**
 * @brief Runtime token identifying a callback registration inside multicast_delegate.
 * @breif Runtime token identifying a callback registration inside multicast_delegate.
 * @usage
 *   - core::DelegateHandle handle;
 *   - if (handle.isValid()) { multicast.remove(handle); }
 */
struct DelegateHandle
{
	uint64_t Id = 0;

	/**
	 * @brief Returns true when this handle references a valid registration.
	 * @breif Returns true when this handle references a valid registration.
	 * @usage
	 *   - if (handle.isValid()) { remove registration by handle; }
	 */
	[[nodiscard]] bool isValid() const noexcept
	{
		return Id != 0;
	}

	/**
	 * @brief Resets this handle to an invalid value.
	 * @breif Resets this handle to an invalid value.
	 * @usage
	 *   - handle.reset();
	 */
	void reset() noexcept
	{
		Id = 0;
	}

	/**
	 * @brief Compares two handles by underlying id.
	 * @breif Compares two handles by underlying id.
	 * @usage
	 *   - if (left == right) { same registration; }
	 */
	bool operator==(const DelegateHandle& Other) const noexcept
	{
		return Id == Other.Id;
	}
};

namespace details
{
template <bool IsCopyable, typename RetType, typename... Args>
class DelegateStorage
{
public:
	using InvokerType = RetType(*)(void*, Args&&...);
	using DestructorType = void(*)(void*);
	using MoveConstructorType = void*(*)(void*, void*);
	using CopyConstructorType = void*(*)(void*, const void*);

	DelegateStorage() noexcept = default;

	DelegateStorage(const DelegateStorage& Other)
		requires (IsCopyable)
	{
		copyFrom(Other);
	}

	DelegateStorage(const DelegateStorage&)
		requires (!IsCopyable)
		= delete;

	DelegateStorage(DelegateStorage&& Other)
	{
		moveFrom(std::move(Other));
	}

	~DelegateStorage()
	{
		reset();
	}

	DelegateStorage& operator=(const DelegateStorage& Other)
		requires (IsCopyable)
	{
		if (this != &Other)
		{
			reset();
			copyFrom(Other);
		}
		return *this;
	}

	DelegateStorage& operator=(const DelegateStorage&)
		requires (!IsCopyable)
		= delete;

	DelegateStorage& operator=(DelegateStorage&& Other)
	{
		if (this != &Other)
		{
			reset();
			moveFrom(std::move(Other));
		}
		return *this;
	}

	[[nodiscard]] bool bound() const noexcept
	{
		return Invoker != nullptr;
	}

	RetType execute(Args... InArgs) const
	{
		if (!Invoker)
		{
			throw std::bad_function_call();
		}

		return Invoker(const_cast<void*>(ObjectPtr), std::forward<Args>(InArgs)...);
	}

	void reset() noexcept
	{
		if (Destroyer && ObjectPtr)
		{
			Destroyer(ObjectPtr);
		}
		clearBinding();
	}

protected:
	template <typename Callable>
	void initFromCallable(Callable&& InCallable)
	{
		using DecayedType = std::decay_t<Callable>;
		static_assert(
			std::is_invocable_r_v<RetType, DecayedType&, Args...>,
			"Callable must be invocable with Args... and return RetType"
		);

		if constexpr (IsCopyable)
		{
			static_assert(std::copy_constructible<DecayedType>, "Callable must be copy constructible for delegate");
		}
		else
		{
			static_assert(std::move_constructible<DecayedType>, "Callable must be move constructible for move_only_delegate");
		}

		reset();

		if constexpr (fitsInline<DecayedType>())
		{
			void* Inline = storagePtr();
			std::construct_at(static_cast<DecayedType*>(Inline), std::forward<Callable>(InCallable));

			ObjectPtr = Inline;
			Invoker = +[](void* StoragePtr, Args&&... ArgsPack) -> RetType {
				return std::invoke(*static_cast<DecayedType*>(StoragePtr), std::forward<Args>(ArgsPack)...);
			};

			Destroyer = +[](void* StoragePtr) {
				std::destroy_at(static_cast<DecayedType*>(StoragePtr));
			};

			Mover = +[](void* DestStorage, void* SourceObject) -> void* {
				auto* Src = static_cast<DecayedType*>(SourceObject);
				auto* Dst = static_cast<DecayedType*>(DestStorage);
				std::construct_at(Dst, std::move(*Src));
				std::destroy_at(Src);
				return Dst;
			};

			if constexpr (IsCopyable)
			{
				Copier = +[](void* DestStorage, const void* SourceObject) -> void* {
					auto* Src = static_cast<const DecayedType*>(SourceObject);
					auto* Dst = static_cast<DecayedType*>(DestStorage);
					std::construct_at(Dst, *Src);
					return Dst;
				};
			}
		}
		else
		{
			auto* HeapObj = std::allocator<DecayedType>{}.allocate(1);
			std::construct_at(HeapObj, std::forward<Callable>(InCallable));

			ObjectPtr = HeapObj;
			Invoker = +[](void* StoragePtr, Args&&... ArgsPack) -> RetType {
				return std::invoke(*static_cast<DecayedType*>(StoragePtr), std::forward<Args>(ArgsPack)...);
			};

			Destroyer = +[](void* StoragePtr) {
				auto* Ptr = static_cast<DecayedType*>(StoragePtr);
				std::destroy_at(Ptr);
				std::allocator<DecayedType>{}.deallocate(Ptr, 1);
			};

			Mover = +[](void*, void* SourceObject) -> void* {
				return SourceObject;
			};

			if constexpr (IsCopyable)
			{
				Copier = +[](void*, const void* SourceObject) -> void* {
					auto* Src = static_cast<const DecayedType*>(SourceObject);
					auto* Clone = std::allocator<DecayedType>{}.allocate(1);
					std::construct_at(Clone, *Src);
					return Clone;
				};
			}
		}
	}

	template <auto Callable>
	void initFromStaticBinding()
	{
		static_assert(std::is_invocable_r_v<RetType, decltype(Callable), Args...>, "Callable signature mismatch");
		reset();

		ObjectPtr = nullptr;
		Invoker = +[](void*, Args&&... ArgsPack) -> RetType {
			return std::invoke(Callable, std::forward<Args>(ArgsPack)...);
		};
		Destroyer = nullptr;
		Mover = +[](void*, void* SourceObject) -> void* {
			return SourceObject;
		};
		if constexpr (IsCopyable)
		{
			Copier = +[](void*, const void* SourceObject) -> void* {
				return const_cast<void*>(SourceObject);
			};
		}
	}

	template <auto Method, typename ClassType>
	void initFromRawBinding(ClassType* Instance)
	{
		reset();

		ObjectPtr = Instance;
		Invoker = +[](void* StoragePtr, Args&&... ArgsPack) -> RetType {
			return std::invoke(Method, static_cast<ClassType*>(StoragePtr), std::forward<Args>(ArgsPack)...);
		};
		Destroyer = nullptr;
		Mover = +[](void*, void* SourceObject) -> void* {
			return SourceObject;
		};
		if constexpr (IsCopyable)
		{
			Copier = +[](void*, const void* SourceObject) -> void* {
				return const_cast<void*>(SourceObject);
			};
		}
	}

private:
	template <typename Ty>
	static consteval bool fitsInline()
	{
		return sizeof(Ty) <= DelegateSmallStorageSize && alignof(Ty) <= alignof(std::max_align_t);
	}

	void copyFrom(const DelegateStorage& Other)
		requires (IsCopyable)
	{
		if (!Other.bound())
		{
			return;
		}

		void* NewObject = nullptr;
		if (Other.Copier)
		{
			NewObject = Other.Copier(storagePtr(), Other.ObjectPtr);
		}
		else
		{
			NewObject = const_cast<void*>(Other.ObjectPtr);
		}

		ObjectPtr = NewObject;
		Invoker = Other.Invoker;
		Destroyer = Other.Destroyer;
		Mover = Other.Mover;
		Copier = Other.Copier;
	}

	void moveFrom(DelegateStorage&& Other)
	{
		if (!Other.bound())
		{
			return;
		}

		void* NewObject = nullptr;
		if (Other.Mover)
		{
			NewObject = Other.Mover(storagePtr(), Other.ObjectPtr);
		}
		else
		{
			NewObject = Other.ObjectPtr;
		}

		ObjectPtr = NewObject;
		Invoker = Other.Invoker;
		Destroyer = Other.Destroyer;
		Mover = Other.Mover;
		Copier = Other.Copier;

		Other.clearBinding();
	}

	void clearBinding() noexcept
	{
		ObjectPtr = nullptr;
		Invoker = nullptr;
		Destroyer = nullptr;
		Mover = nullptr;
		Copier = nullptr;
	}

	void* storagePtr() noexcept
	{
		return static_cast<void*>(StorageBuffer);
	}

	const void* storagePtr() const noexcept
	{
		return static_cast<const void*>(StorageBuffer);
	}

private:
	alignas(std::max_align_t) std::byte StorageBuffer[DelegateSmallStorageSize]{};

	void* ObjectPtr = nullptr;
	InvokerType Invoker = nullptr;
	DestructorType Destroyer = nullptr;
	MoveConstructorType Mover = nullptr;
	CopyConstructorType Copier = nullptr;
};
} // namespace details

/**
 * @brief Copyable high-performance delegate with SBO and type-safe bindings.
 * @breif Copyable high-performance delegate with SBO and type-safe bindings.
 * @usage
 *   - core::delegate<void, int> d;
 *   - d.bind_static<&OnValue>();
 *   - d.execute(42);
 */
template <typename RetType, typename ... Args>
class delegate : public details::DelegateStorage<true, RetType, Args...>
{
	using Super = details::DelegateStorage<true, RetType, Args...>;

public:
	using Super::bound;
	using Super::execute;
	using Super::reset;

	/**
	 * @brief Creates an empty delegate.
	 * @breif Creates an empty delegate.
	 * @usage
	 *   - core::delegate<void, int> d;
	 */
	delegate() noexcept = default;
	delegate(const delegate&) = default;
	delegate(delegate&&) = default;
	delegate& operator=(const delegate&) = default;
	delegate& operator=(delegate&&) = default;

	/**
	 * @brief Constructs a delegate from std::function by copy.
	 * @breif Constructs a delegate from std::function by copy.
	 * @usage
	 *   - std::function<int(int)> fn = [](int v) { return v * 2; };
	 *   - core::delegate<int, int> d(fn);
	 */
	explicit delegate(const std::function<RetType(Args...)>& InFunction)
	{
		bind_std_function(InFunction);
	}

	/**
	 * @brief Constructs a delegate from std::function by move.
	 * @breif Constructs a delegate from std::function by move.
	 * @usage
	 *   - core::delegate<int, int> d(std::function<int(int)>{[](int v) { return v * 2; }});
	 */
	explicit delegate(std::function<RetType(Args...)>&& InFunction)
	{
		bind_std_function(std::move(InFunction));
	}

	/**
	 * @brief Binds a compile-time free/static callable without owning storage.
	 * @breif Binds a compile-time free/static callable without owning storage.
	 * @usage
	 *   - d.bind_static<&OnValue>();
	 */
	template <auto Callable>
		requires std::is_invocable_r_v<RetType, decltype(Callable), Args...>
	void bind_static()
	{
		Super::template initFromStaticBinding<Callable>();
	}

	/**
	 * @brief Binds a member function with a raw instance pointer.
	 * @breif Binds a member function with a raw instance pointer.
	 * @usage
	 *   - Receiver receiver;
	 *   - d.bind_raw<&Receiver::Handle>(&receiver);
	 */
	template <auto Method, typename ClassType>
		requires std::is_member_function_pointer_v<decltype(Method)>
	void bind_raw(ClassType* Instance)
	{
		if (Instance == nullptr)
		{
			throw std::invalid_argument("delegate::bind_raw requires non-null instance");
		}
		Super::template initFromRawBinding<Method>(Instance);
	}

	/**
	 * @brief Binds a member function via shared_ptr lifetime tracking.
	 * @breif Binds a member function via shared_ptr lifetime tracking.
	 * @usage
	 *   - auto receiver = std::make_shared<Receiver>();
	 *   - d.bind_shared<&Receiver::Handle>(receiver);
	 */
	template <auto Method, typename ClassType>
		requires std::is_member_function_pointer_v<decltype(Method)>
	void bind_shared(const std::shared_ptr<ClassType>& InstancePtr)
	{
		bind_weak<Method>(std::weak_ptr<ClassType>(InstancePtr));
	}

	/**
	 * @brief Binds a member function via weak_ptr and throws if object expired.
	 * @breif Binds a member function via weak_ptr and throws if object expired.
	 * @usage
	 *   - std::weak_ptr<Receiver> weak = receiver;
	 *   - d.bind_weak<&Receiver::Handle>(weak);
	 */
	template <auto Method, typename ClassType>
		requires std::is_member_function_pointer_v<decltype(Method)>
	void bind_weak(const std::weak_ptr<ClassType>& InstanceWeakPtr)
	{
		Super::initFromCallable(
			[InstanceWeakPtr](Args... InArgs) -> RetType {
				auto Shared = InstanceWeakPtr.lock();
				if (!Shared)
				{
					throw std::bad_function_call();
				}
				return std::invoke(Method, Shared.get(), std::forward<Args>(InArgs)...);
			}
		);
	}

	/**
	 * @brief Binds a copy-constructible callable object.
	 * @breif Binds a copy-constructible callable object.
	 * @usage
	 *   - d.bind_lambda([factor = 2](int v) { return v * factor; });
	 */
	template <typename Callable>
		requires std::is_invocable_r_v<RetType, std::decay_t<Callable>&, Args...>
			&& std::copy_constructible<std::decay_t<Callable>>
	void bind_lambda(Callable&& InCallable)
	{
		Super::initFromCallable(std::forward<Callable>(InCallable));
	}

	/**
	 * @brief Binds from std::function by copy for interoperability.
	 * @breif Binds from std::function by copy for interoperability.
	 * @usage
	 *   - std::function<void(int)> fn = [](int) {};
	 *   - d.bind_std_function(fn);
	 */
	void bind_std_function(const std::function<RetType(Args...)>& InFunction)
	{
		Super::initFromCallable(InFunction);
	}

	/**
	 * @brief Binds from std::function by move for interoperability.
	 * @breif Binds from std::function by move for interoperability.
	 * @usage
	 *   - d.bind_std_function(std::function<void(int)>{[](int) {}});
	 */
	void bind_std_function(std::function<RetType(Args...)>&& InFunction)
	{
		Super::initFromCallable(std::move(InFunction));
	}

	/**
	 * @brief Exports this delegate as std::function.
	 * @breif Exports this delegate as std::function.
	 * @usage
	 *   - std::function<int(int)> fn = d.as_std_function();
	 */
	[[nodiscard]] std::function<RetType(Args...)> as_std_function() const&
	{
		delegate Copy = *this;
		return [Copy = std::move(Copy)](Args... InArgs) mutable -> RetType {
			return Copy.execute(std::forward<Args>(InArgs)...);
		};
	}

	/**
	 * @brief Moves this delegate into std::move_only_function.
	 * @breif Moves this delegate into std::move_only_function.
	 * @usage
	 *   - auto fn = std::move(d).as_std_move_only_function();
	 */
	[[nodiscard]] std::move_only_function<RetType(Args...)> as_std_move_only_function() &&
	{
		delegate Moved = std::move(*this);
		return [Moved = std::move(Moved)](Args... InArgs) mutable -> RetType {
			return Moved.execute(std::forward<Args>(InArgs)...);
		};
	}
};

/**
 * @brief Move-only high-performance delegate with SBO and type-safe bindings.
 * @breif Move-only high-performance delegate with SBO and type-safe bindings.
 * @usage
 *   - core::move_only_delegate<void, int> d;
 *   - d.bind_lambda([state = std::make_unique<int>(1)](int) mutable { ++(*state); });
 */
template <typename RetType, typename ... Args>
class move_only_delegate : public details::DelegateStorage<false, RetType, Args...>
{
	using Super = details::DelegateStorage<false, RetType, Args...>;

public:
	using Super::bound;
	using Super::execute;
	using Super::reset;

	/**
	 * @breif Creates an empty move-only delegate.
	 * @usage
	 *   - core::move_only_delegate<void, int> d;
	 */
	move_only_delegate() noexcept = default;
	move_only_delegate(const move_only_delegate&) = delete;
	move_only_delegate& operator=(const move_only_delegate&) = delete;
	move_only_delegate(move_only_delegate&&) = default;
	move_only_delegate& operator=(move_only_delegate&&) = default;

	/**
	 * @breif Constructs a move-only delegate from std::move_only_function.
	 * @usage
	 *   - core::move_only_delegate<int, int> d(std::move(std::move_only_function<int(int)>{...}));
	 */
	explicit move_only_delegate(std::move_only_function<RetType(Args...)>&& InFunction)
	{
		bind_std_move_only_function(std::move(InFunction));
	}

	/**
	 * @breif Binds a compile-time free/static callable without owning storage.
	 * @usage
	 *   - d.bind_static<&OnValue>();
	 */
	template <auto Callable>
		requires std::is_invocable_r_v<RetType, decltype(Callable), Args...>
	void bind_static()
	{
		Super::template initFromStaticBinding<Callable>();
	}

	/**
	 * @breif Binds a member function with a raw instance pointer.
	 * @usage
	 *   - Receiver receiver;
	 *   - d.bind_raw<&Receiver::Handle>(&receiver);
	 */
	template <auto Method, typename ClassType>
		requires std::is_member_function_pointer_v<decltype(Method)>
	void bind_raw(ClassType* Instance)
	{
		if (Instance == nullptr)
		{
			throw std::invalid_argument("move_only_delegate::bind_raw requires non-null instance");
		}
		Super::template initFromRawBinding<Method>(Instance);
	}

	/**
	 * @breif Binds a member function via shared_ptr lifetime tracking.
	 * @usage
	 *   - auto receiver = std::make_shared<Receiver>();
	 *   - d.bind_shared<&Receiver::Handle>(receiver);
	 */
	template <auto Method, typename ClassType>
		requires std::is_member_function_pointer_v<decltype(Method)>
	void bind_shared(const std::shared_ptr<ClassType>& InstancePtr)
	{
		bind_weak<Method>(std::weak_ptr<ClassType>(InstancePtr));
	}

	/**
	 * @breif Binds a member function via weak_ptr and throws if object expired.
	 * @usage
	 *   - std::weak_ptr<Receiver> weak = receiver;
	 *   - d.bind_weak<&Receiver::Handle>(weak);
	 */
	template <auto Method, typename ClassType>
		requires std::is_member_function_pointer_v<decltype(Method)>
	void bind_weak(const std::weak_ptr<ClassType>& InstanceWeakPtr)
	{
		Super::initFromCallable(
			[InstanceWeakPtr](Args... InArgs) -> RetType {
				auto Shared = InstanceWeakPtr.lock();
				if (!Shared)
				{
					throw std::bad_function_call();
				}
				return std::invoke(Method, Shared.get(), std::forward<Args>(InArgs)...);
			}
		);
	}

	/**
	 * @breif Binds a move-constructible callable object.
	 * @usage
	 *   - d.bind_lambda([state = std::make_unique<int>(1)](int v) { return *state + v; });
	 */
	template <typename Callable>
		requires std::is_invocable_r_v<RetType, std::decay_t<Callable>&, Args...>
			&& std::move_constructible<std::decay_t<Callable>>
	void bind_lambda(Callable&& InCallable)
	{
		Super::initFromCallable(std::forward<Callable>(InCallable));
	}

	/**
	 * @breif Binds from std::move_only_function for interoperability.
	 * @usage
	 *   - std::move_only_function<void(int)> fn = [](int) {};
	 *   - d.bind_std_move_only_function(std::move(fn));
	 */
	void bind_std_move_only_function(std::move_only_function<RetType(Args...)>&& InFunction)
	{
		Super::initFromCallable(std::move(InFunction));
	}

	/**
	 * @breif Moves this delegate into std::move_only_function.
	 * @usage
	 *   - auto fn = std::move(d).as_std_move_only_function();
	 */
	[[nodiscard]] std::move_only_function<RetType(Args...)> as_std_move_only_function() &&
	{
		move_only_delegate Moved = std::move(*this);
		return [Moved = std::move(Moved)](Args... InArgs) mutable -> RetType {
			return Moved.execute(std::forward<Args>(InArgs)...);
		};
	}
};

template <typename Ty>
class multicast_delegate;

/**
 * @breif Multi-cast delegate storing multiple callbacks under DelegateHandle ids.
 * @usage
 *   - core::multicast_delegate<void(int)> onValue;
 *   - auto handle = onValue.add_lambda([](int) {});
 *   - onValue.broadcast(3);
 */
template <typename RetType, typename ... Args>
class multicast_delegate <RetType(Args...)>
{
	using DelegateType = delegate<RetType, Args...>;

	struct DelegateEntry
	{
		DelegateHandle Handle;
		DelegateType Delegate;
	};

public:
	/**
	 * @breif Adds a delegate by move and returns its registration handle.
	 * @usage
	 *   - core::delegate<void, int> d;
	 *   - auto h = multicast.add(std::move(d));
	 */
	DelegateHandle add(DelegateType&& InDelegate)
	{
		DelegateHandle Handle;
		Handle.Id = NextHandleId++;
		Delegates.push_back(DelegateEntry{Handle, std::move(InDelegate)});
		return Handle;
	}

	/**
	 * @breif Adds a delegate by copy and returns its registration handle.
	 * @usage
	 *   - core::delegate<void, int> d;
	 *   - auto h = multicast.add(d);
	 */
	DelegateHandle add(const DelegateType& InDelegate)
	{
		DelegateHandle Handle;
		Handle.Id = NextHandleId++;
		Delegates.push_back(DelegateEntry{Handle, InDelegate});
		return Handle;
	}

	/**
	 * @breif Adds a raw member-function binding and returns its handle.
	 * @usage
	 *   - auto h = multicast.add_raw<&Receiver::Handle>(&receiver);
	 */
	template <auto Method, typename ClassType>
	DelegateHandle add_raw(ClassType* Instance)
	{
		DelegateType Bound;
		Bound.template bind_raw<Method>(Instance);
		return add(std::move(Bound));
	}

	/**
	 * @brief Adds a static/free function binding and returns its handle.
	 * @usage
	 *   - auto h = multicast.add_static<&OnValue>();
	 */
	template <auto FunctionType>
	DelegateHandle add_static()
	{
		DelegateType Bound;
		Bound.template bind_static<FunctionType>();
		return add(std::move(Bound));
	}

	/**
	 * @breif Adds a shared_ptr-tracked member-function binding and returns its handle.
	 * @usage
	 *   - auto h = multicast.add_shared<&Receiver::Handle>(receiver);
	 */
	template <auto Method, typename ClassType>
	DelegateHandle add_shared(const std::shared_ptr<ClassType>& InstancePtr)
	{
		DelegateType Bound;
		Bound.template bind_shared<Method>(InstancePtr);
		return add(std::move(Bound));
	}

	/**
	 * @breif Adds a weak_ptr-tracked member-function binding and returns its handle.
	 * @usage
	 *   - auto h = multicast.add_weak<&Receiver::Handle>(weak_receiver);
	 */
	template <auto Method, typename ClassType>
	DelegateHandle add_weak(const std::weak_ptr<ClassType>& InstanceWeakPtr)
	{
		DelegateType Bound;
		Bound.template bind_weak<Method>(InstanceWeakPtr);
		return add(std::move(Bound));
	}

	/**
	 * @breif Adds a callable/lambda binding and returns its handle.
	 * @usage
	 *   - auto h = multicast.add_lambda([](int v) { return v + 1; });
	 */
	template <typename Callable>
	DelegateHandle add_lambda(Callable&& InCallable)
	{
		DelegateType Bound;
		Bound.bind_lambda(std::forward<Callable>(InCallable));
		return add(std::move(Bound));
	}

	/**
	 * @breif Removes a callback by handle.
	 * @usage
	 *   - bool removed = multicast.remove(handle);
	 */
	bool remove(const DelegateHandle& Handle)
	{
		auto It = std::find_if(
			Delegates.begin(),
			Delegates.end(),
			[&](const DelegateEntry& Entry) {
				return Entry.Handle == Handle;
			}
		);

		if (It == Delegates.end())
		{
			return false;
		}

		Delegates.erase(It);
		return true;
	}

	/**
	 * @brief Returns whether the handle is currently registered.
	 * @usage
	 *   - if (multicast.contains(handle)) { handle is still active; }
	 */
	[[nodiscard]] bool contains(const DelegateHandle& Handle) const
	{
		return std::find_if(
			Delegates.begin(),
			Delegates.end(),
			[&](const DelegateEntry& Entry) {
				return Entry.Handle == Handle;
			}
		) != Delegates.end();
	}

	/**
	 * @brief Clears all registered callbacks.
	 * @usage
	 *   - multicast.clear();
	 */
	void clear()
	{
		Delegates.clear();
	}

	/**
	 * @brief Returns true when at least one callback is registered.
	 * @usage
	 *   - if (multicast.bound()) { multicast.broadcast(value); }
	 */
	[[nodiscard]] bool bound() const noexcept
	{
		return !Delegates.empty();
	}

	/**
	 * @breif Returns the number of registered callbacks.
	 * @usage
	 *   - std::size_t count = multicast.size();
	 */
	[[nodiscard]] std::size_t size() const noexcept
	{
		return Delegates.size();
	}

	/**
	 * @breif Broadcasts arguments to all delegates for void signatures.
	 * @usage
	 *   - multicast.broadcast(42);
	 */
	void broadcast(Args... InArgs) const
		requires std::is_void_v<RetType>
	{
		for (const auto& Entry : Delegates)
		{
			Entry.Delegate.execute(std::forward<Args>(InArgs)...);
		}
	}

	/**
	 * @breif Invokes all delegates and collects return values for non-void signatures.
	 * @usage
	 *   - std::vector<int> values = multicast.collect(42);
	 */
	[[nodiscard]] std::vector<RetType> collect(Args... InArgs) const
		requires (!std::is_void_v<RetType>)
	{
		std::vector<RetType> Results;
		Results.reserve(Delegates.size());
		for (const auto& Entry : Delegates)
		{
			Results.push_back(Entry.Delegate.execute(std::forward<Args>(InArgs)...));
		}
		return Results;
	}

private:
	std::vector<DelegateEntry> Delegates;
	uint64_t NextHandleId = 1;
};
} // namespace core
