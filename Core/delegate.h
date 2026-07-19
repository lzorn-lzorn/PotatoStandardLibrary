#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace core
{

constexpr std::uint32_t DelegateSmallStorageSize = 32;

struct DelegateHandle
{
	std::uint64_t Id = 0;

	[[nodiscard]] constexpr bool isValid() const noexcept
	{
		return Id != 0;
	}

	constexpr void reset() noexcept
	{
		Id = 0;
	}

	[[nodiscard]] constexpr bool operator==(const DelegateHandle& Other) const noexcept = default;
};

namespace details
{
template <bool EnableSBO>
struct InlineStorage
{
	static constexpr std::size_t StorageSize = 0;
};

template <>
struct InlineStorage<true>
{
	static constexpr std::size_t StorageSize = DelegateSmallStorageSize;

	alignas(std::max_align_t) std::byte Buffer[StorageSize]{};
};

template <bool IsCopyable, bool EnableSBO, typename RetType, typename... Args>
class DelegateStorage
{
private:
	using InlineStorageType = InlineStorage<EnableSBO>;
	using InvokerType = RetType(*)(void*, Args&&...);
	using DestroyerType = void(*)(void*) noexcept;
	using MoverType = void*(*)(void*, void*) noexcept;
	using CopierType = void*(*)(void*, const void*);

	/** The table is static per callable type, so delegates carry one lifecycle metadata pointer. */
	struct Operations
	{
		DestroyerType Destroy = nullptr;
		MoverType Move = nullptr;
		CopierType Copy = nullptr;
	};

public:
	DelegateStorage() noexcept = default;

	DelegateStorage(const DelegateStorage& Other)
		requires IsCopyable
	{
		copyFrom(Other);
	}

	DelegateStorage(const DelegateStorage&)
		requires (!IsCopyable)
		= delete;

	DelegateStorage(DelegateStorage&& Other) noexcept
	{
		moveFrom(std::move(Other));
	}

	~DelegateStorage()
	{
		reset();
	}

	DelegateStorage& operator=(const DelegateStorage& Other)
		requires IsCopyable
	{
		if (this != &Other)
		{
			DelegateStorage Copy(Other);
			reset();
			moveFrom(std::move(Copy));
		}
		return *this;
	}

	DelegateStorage& operator=(const DelegateStorage&)
		requires (!IsCopyable)
		= delete;

	DelegateStorage& operator=(DelegateStorage&& Other) noexcept
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
		return Invoker(ObjectPtr, std::forward<Args>(InArgs)...);
	}

	void reset() noexcept
	{
		if (Ops && Ops->Destroy && ObjectPtr)
		{
			Ops->Destroy(ObjectPtr);
		}
		clearBinding();
	}

protected:
	template <typename Callable>
	void initFromCallable(Callable&& InCallable)
	{
		using CallableType = std::decay_t<Callable>;
		static_assert(std::is_invocable_r_v<RetType, CallableType&, Args...>, "Callable signature does not match delegate");

		if constexpr (IsCopyable)
		{
			static_assert(std::copy_constructible<CallableType>, "delegate requires a copy-constructible callable");
		}
		else
		{
			static_assert(std::move_constructible<CallableType>, "move_only_delegate requires a move-constructible callable");
		}

		reset();

		if constexpr (fitsInline<CallableType>())
		{
			void* InlineStoragePtr = inlineStorage();
			std::construct_at(static_cast<CallableType*>(InlineStoragePtr), std::forward<Callable>(InCallable));
			ObjectPtr = InlineStoragePtr;
			Invoker = inlineInvoker<CallableType>();
			Ops = inlineOperations<CallableType>();
		}
		else
		{
			auto* HeapObject = std::allocator<CallableType>{}.allocate(1);

			try
			{
				std::construct_at(HeapObject, std::forward<Callable>(InCallable));
			}
			catch (...)
			{
				std::allocator<CallableType>{}.deallocate(HeapObject, 1);
				throw;
			}

			ObjectPtr = HeapObject;
			Invoker = heapInvoker<CallableType>();
			Ops = heapOperations<CallableType>();
		}
	}

	template <auto Callable>
	void initFromStaticBinding()
	{
		static_assert(std::is_invocable_r_v<RetType, decltype(Callable), Args...>, "Static function signature does not match delegate");
		reset();
		ObjectPtr = nullptr;
		Invoker = staticInvoker<Callable>();
		Ops = staticOperations<Callable>();
	}

	template <auto Method, typename ClassType>
	void initFromRawBinding(ClassType* Instance)
	{
		static_assert(std::is_member_function_pointer_v<decltype(Method)>, "Method must be a member-function pointer");
		reset();
		ObjectPtr = Instance;
		Invoker = rawInvoker<Method, ClassType>();
		Ops = rawOperations<Method, ClassType>();
	}

	template <auto Method, typename ClassType>
	void initFromConstRawBinding(const ClassType* Instance)
	{
		static_assert(std::is_member_function_pointer_v<decltype(Method)>, "Method must be a member-function pointer");
		static_assert(std::is_invocable_r_v<RetType, decltype(Method), const ClassType*, Args...>, "Const method signature does not match delegate");
		reset();
		ObjectPtr = const_cast<ClassType*>(Instance);
		Invoker = constRawInvoker<Method, ClassType>();
		Ops = constRawOperations<Method, ClassType>();
	}

private:
	template <typename CallableType>
	static consteval bool fitsInline()
	{
		if constexpr (EnableSBO)
		{
			return sizeof(CallableType) <= DelegateSmallStorageSize
				&& alignof(CallableType) <= alignof(std::max_align_t)
				&& std::is_nothrow_move_constructible_v<CallableType>;
		}
		else
		{
			return false;
		}
	}

	template <typename CallableType>
	static RetType invokeCallable(CallableType& Callable, Args&&... InArgs)
	{
		if constexpr (requires(CallableType& Candidate, Args&&... CandidateArgs) {
			Candidate(std::forward<Args>(CandidateArgs)...);
		})
		{
			return Callable(std::forward<Args>(InArgs)...);
		}
		else
		{
			return std::invoke(Callable, std::forward<Args>(InArgs)...);
		}
	}

	template <auto Callable>
	static RetType invokeStatic(Args&&... InArgs)
	{
		if constexpr (requires { Callable(std::declval<Args>()...); })
		{
			return Callable(std::forward<Args>(InArgs)...);
		}
		else
		{
			return std::invoke(Callable, std::forward<Args>(InArgs)...);
		}
	}

	template <typename CallableType>
	static InvokerType gitinlineInvoker()
	{
		return +[](void* Object, Args&&... InArgs) -> RetType {
			return invokeCallable(*static_cast<CallableType*>(Object), std::forward<Args>(InArgs)...);
		};
	}

	template <typename CallableType>
	static InvokerType heapInvoker()
	{
		return inlineInvoker<CallableType>();
	}

	template <auto Callable>
	static InvokerType staticInvoker()
	{
		return +[](void*, Args&&... InArgs) -> RetType {
			return invokeStatic<Callable>(std::forward<Args>(InArgs)...);
		};
	}

	template <auto Method, typename ClassType>
	static InvokerType rawInvoker()
	{
		static_assert(std::is_invocable_r_v<RetType, decltype(Method), ClassType*, Args...>, "Raw method signature does not match delegate");
		return +[](void* Object, Args&&... InArgs) -> RetType {
			return (static_cast<ClassType*>(Object)->*Method)(std::forward<Args>(InArgs)...);
		};
	}

	template <auto Method, typename ClassType>
	static InvokerType constRawInvoker()
	{
		static_assert(std::is_invocable_r_v<RetType, decltype(Method), const ClassType*, Args...>, "Const raw method signature does not match delegate");
		return +[](void* Object, Args&&... InArgs) -> RetType {
			return (static_cast<const ClassType*>(Object)->*Method)(std::forward<Args>(InArgs)...);
		};
	}

	template <typename CallableType>
	static const Operations* inlineOperations()
	{
		static const Operations Table{
			.Destroy = +[](void* Object) noexcept {
				std::destroy_at(static_cast<CallableType*>(Object));
			},
			.Move = +[](void* Destination, void* Source) noexcept -> void* {
				auto* SourceCallable = static_cast<CallableType*>(Source);
				auto* DestinationCallable = static_cast<CallableType*>(Destination);
				std::construct_at(DestinationCallable, std::move(*SourceCallable));
				std::destroy_at(SourceCallable);
				return DestinationCallable;
			},
			.Copy = copyOperation<CallableType>()
		};
		return &Table;
	}

	template <typename CallableType>
	static const Operations* heapOperations()
	{
		static const Operations Table{
			.Destroy = +[](void* Object) noexcept {
				auto* Callable = static_cast<CallableType*>(Object);
				std::destroy_at(Callable);
				std::allocator<CallableType>{}.deallocate(Callable, 1);
			},
			.Move = +[](void*, void* Source) noexcept -> void* {
				return Source;
			},
			.Copy = heapCopyOperation<CallableType>()
		};
		return &Table;
	}

	template <auto Callable>
	static const Operations* staticOperations()
	{
		static const Operations Table{
			.Destroy = nullptr,
			.Move = +[](void*, void*) noexcept -> void* { return nullptr; },
			.Copy = +[](void*, const void*) -> void* { return nullptr; }
		};
		return &Table;
	}

	template <auto Method, typename ClassType>
	static const Operations* rawOperations()
	{
		static const Operations Table{
			.Destroy = nullptr,
			.Move = +[](void*, void* Source) noexcept -> void* { return Source; },
			.Copy = +[](void*, const void* Source) -> void* { return const_cast<void*>(Source); }
		};
		return &Table;
	}

	template <auto Method, typename ClassType>
	static const Operations* constRawOperations()
	{
		static const Operations Table{
			.Destroy = nullptr,
			.Move = +[](void*, void* Source) noexcept -> void* { return Source; },
			.Copy = +[](void*, const void* Source) -> void* { return const_cast<void*>(Source); }
		};
		return &Table;
	}

	template <typename CallableType>
	static consteval CopierType copyOperation()
	{
		if constexpr (IsCopyable)
		{
			return +[](void* Destination, const void* Source) -> void* {
				auto* DestinationCallable = static_cast<CallableType*>(Destination);
				std::construct_at(DestinationCallable, *static_cast<const CallableType*>(Source));
				return DestinationCallable;
			};
		}
		else
		{
			return nullptr;
		}
	}

	template <typename CallableType>
	static consteval CopierType heapCopyOperation()
	{
		if constexpr (IsCopyable)
		{
			return +[](void*, const void* Source) -> void* {
				auto* Clone = std::allocator<CallableType>{}.allocate(1);
				try
				{
					std::construct_at(Clone, *static_cast<const CallableType*>(Source));
				}
				catch (...)
				{
					std::allocator<CallableType>{}.deallocate(Clone, 1);
					throw;
				}
				return Clone;
			};
		}
		else
		{
			return nullptr;
		}
	}

	void copyFrom(const DelegateStorage& Other)
		requires IsCopyable
	{
		if (!Other.Invoker)
		{
			return;
		}

		ObjectPtr = Other.Ops->Copy(inlineStorage(), Other.ObjectPtr);
		Invoker = Other.Invoker;
		Ops = Other.Ops;
	}

	void moveFrom(DelegateStorage&& Other) noexcept
	{
		if (!Other.Invoker)
		{
			return;
		}

		ObjectPtr = Other.Ops->Move(inlineStorage(), Other.ObjectPtr);
		Invoker = Other.Invoker;
		Ops = Other.Ops;
		Other.clearBinding();
	}

	void clearBinding() noexcept
	{
		ObjectPtr = nullptr;
		Invoker = nullptr;
		Ops = nullptr;
	}

	void* inlineStorage() noexcept
	{
		if constexpr (EnableSBO)
		{
			return static_cast<void*>(Inline.Buffer);
		}
		else
		{
			return nullptr;
		}
	}

private:
	[[no_unique_address]] InlineStorageType Inline{};
	void* ObjectPtr = nullptr;
	InvokerType Invoker = nullptr;
	const Operations* Ops = nullptr;
};


template <typename RetType, bool EnableSBO, typename... Args>
class DelegateBase : public DelegateStorage<true, EnableSBO, RetType, Args...>
{
	using Super = DelegateStorage<true, EnableSBO, RetType, Args...>;

public:
	using Super::bound;
	using Super::execute;
	using Super::reset;

	DelegateBase() noexcept = default;
	DelegateBase(const DelegateBase&) = default;
	DelegateBase(DelegateBase&&) noexcept = default;
	DelegateBase& operator=(const DelegateBase&) = default;
	DelegateBase& operator=(DelegateBase&&) noexcept = default;

	explicit DelegateBase(const std::function<RetType(Args...)>& InFunction)
	{
		bind_std_function(InFunction);
	}

	explicit DelegateBase(std::function<RetType(Args...)>&& InFunction)
	{
		bind_std_function(std::move(InFunction));
	}

	template <auto Callable>
		requires std::is_invocable_r_v<RetType, decltype(Callable), Args...>
	void bind_static()
	{
		Super::template initFromStaticBinding<Callable>();
	}

	template <auto Method, typename ClassType>
		requires std::is_member_function_pointer_v<decltype(Method)>
	void bind_raw(ClassType* Instance)
	{
		if (!Instance)
		{
			throw std::invalid_argument("delegate::bind_raw requires a non-null instance");
		}
		Super::template initFromRawBinding<Method>(Instance);
	}

	template <auto Method, typename ClassType>
		requires std::is_member_function_pointer_v<decltype(Method)>
	void bind_raw(const ClassType* Instance)
	{
		if (!Instance)
		{
			throw std::invalid_argument("delegate::bind_raw requires a non-null instance");
		}
		Super::template initFromConstRawBinding<Method>(Instance);
	}

	/** 
	 * @brief Binds a member function while strongly owning its shared object. 
	 * @usage `callback.bind_shared<&Receiver::Handle>(receiver);` 
	 */
	template <auto Method, typename ClassType>
		requires std::is_member_function_pointer_v<decltype(Method)>
	void bind_shared(std::shared_ptr<ClassType> InstancePtr)
	{
		if (!InstancePtr)
		{
			throw std::invalid_argument("delegate::bind_shared requires a non-null instance");
		}
		Super::initFromCallable(
			[InstancePtr = std::move(InstancePtr)](Args... InArgs) -> RetType {
				return (InstancePtr.get()->*Method)(std::forward<Args>(InArgs)...);
			}
		);
	}

	/** 
	 * @brief Binds a weak member function and validates lifetime at invoke time. 
	 * @usage `callback.bind_weak<&Receiver::Handle>(weak_receiver);` 
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
				return (Shared.get()->*Method)(std::forward<Args>(InArgs)...);
			}
		);
	}

	/** 
	 * @brief Binds a copyable callable object. 
	 * @usage `callback.bind_lambda([factor = 2](int value) { return value * factor; });` 
	 */
	template <typename Callable>
		requires std::is_invocable_r_v<RetType, std::decay_t<Callable>&, Args...>
			&& std::copy_constructible<std::decay_t<Callable>>
	void bind_lambda(Callable&& InCallable)
	{
		Super::initFromCallable(std::forward<Callable>(InCallable));
	}

	/** 
	 * @brief Binds std::function by copy for interoperability. 
	 * @usage `callback.bind_std_function(fn);` 
	 */
	void bind_std_function(const std::function<RetType(Args...)>& InFunction)
	{
		Super::initFromCallable(InFunction);
	}

	/** 
	 * @brief Binds std::function by move for interoperability. 
	 * @usage `callback.bind_std_function(std::move(fn));` 
	 */
	void bind_std_function(std::function<RetType(Args...)>&& InFunction)
	{
		Super::initFromCallable(std::move(InFunction));
	}

	/** 
	 * @brief Exports this delegate as std::function.
	 * @usage `std::function<int(int)> fn = callback.as_std_function();` 
	 */
	[[nodiscard]] std::function<RetType(Args...)> as_std_function() const&
	{
		DelegateBase Copy = *this;
		return [Copy = std::move(Copy)](Args... InArgs) mutable -> RetType {
			return Copy.execute(std::forward<Args>(InArgs)...);
		};
	}

	/** 
	 * @brief Moves this delegate into std::move_only_function. 
	 * @usage `auto fn = std::move(callback).as_std_move_only_function();` 
	 */
	[[nodiscard]] std::move_only_function<RetType(Args...)> as_std_move_only_function() &&
	{
		DelegateBase Moved = std::move(*this);
		return [Moved = std::move(Moved)](Args... InArgs) mutable -> RetType {
			return Moved.execute(std::forward<Args>(InArgs)...);
		};
	}
};

/**
 * @brief Move-only high-performance delegate with configurable SBO and direct static/raw trampolines.
 * @usage
 *   - core::MoveOnlyDelegateBase<int, true, int> callback;
 *   - callback.bind_lambda([value = std::make_unique<int>(1)](int input) { return *value + input; });
 */
template <typename RetType, bool EnableSBO, typename... Args>
class MoveOnlyDelegateBase : public DelegateStorage<false, EnableSBO, RetType, Args...>
{
	using Super = DelegateStorage<false, EnableSBO, RetType, Args...>;

public:
	using Super::bound;
	using Super::execute;
	using Super::reset;

	MoveOnlyDelegateBase() noexcept = default;
	MoveOnlyDelegateBase(const MoveOnlyDelegateBase&) = delete;
	MoveOnlyDelegateBase& operator=(const MoveOnlyDelegateBase&) = delete;
	MoveOnlyDelegateBase(MoveOnlyDelegateBase&&) noexcept = default;
	MoveOnlyDelegateBase& operator=(MoveOnlyDelegateBase&&) noexcept = default;

	explicit MoveOnlyDelegateBase(std::move_only_function<RetType(Args...)>&& InFunction)
	{
		bind_std_move_only_function(std::move(InFunction));
	}

	template <auto Callable>
		requires std::is_invocable_r_v<RetType, decltype(Callable), Args...>
	void bind_static()
	{
		Super::template initFromStaticBinding<Callable>();
	}

	template <auto Method, typename ClassType>
		requires std::is_member_function_pointer_v<decltype(Method)>
	void bind_raw(ClassType* Instance)
	{
		if (!Instance)
		{
			throw std::invalid_argument("move_only_delegate::bind_raw requires a non-null instance");
		}
		Super::template initFromRawBinding<Method>(Instance);
	}

	/** 
	 * @brief Binds a const member function and const raw instance. 
	 * @usage `callback.bind_raw<&Receiver::Read>(&receiver);` 
	 */
	template <auto Method, typename ClassType>
		requires std::is_member_function_pointer_v<decltype(Method)>
	void bind_raw(const ClassType* Instance)
	{
		if (!Instance)
		{
			throw std::invalid_argument("move_only_delegate::bind_raw requires a non-null instance");
		}
		Super::template initFromConstRawBinding<Method>(Instance);
	}

	/** 
	 * @brief Binds a member function while strongly owning its shared object. 
	 * @usage `callback.bind_shared<&Receiver::Handle>(receiver);` 
	 */
	template <auto Method, typename ClassType>
		requires std::is_member_function_pointer_v<decltype(Method)>
	void bind_shared(std::shared_ptr<ClassType> InstancePtr)
	{
		if (!InstancePtr)
		{
			throw std::invalid_argument("move_only_delegate::bind_shared requires a non-null instance");
		}
		Super::initFromCallable(
			[InstancePtr = std::move(InstancePtr)](Args... InArgs) -> RetType {
				return (InstancePtr.get()->*Method)(std::forward<Args>(InArgs)...);
			}
		);
	}

	/** 
	 * @brief Binds a weak member function and validates lifetime at invoke time. 
	 * @usage `callback.bind_weak<&Receiver::Handle>(weak_receiver);` 
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
				return (Shared.get()->*Method)(std::forward<Args>(InArgs)...);
			}
		);
	}

	/** 
	 * @brief Binds a move-constructible callable object. 
	 * @usage `callback.bind_lambda([value = std::make_unique<int>(1)](int input) { return *value + input; });` 
	 */
	template <typename Callable>
		requires std::is_invocable_r_v<RetType, std::decay_t<Callable>&, Args...>
			&& std::move_constructible<std::decay_t<Callable>>
	void bind_lambda(Callable&& InCallable)
	{
		Super::initFromCallable(std::forward<Callable>(InCallable));
	}

	/** 
	 * @brief Binds std::move_only_function for interoperability. 
	 * @usage `callback.bind_std_move_only_function(std::move(fn));` 
	 */
	void bind_std_move_only_function(std::move_only_function<RetType(Args...)>&& InFunction)
	{
		Super::initFromCallable(std::move(InFunction));
	}

	/** 
	 * @brief Moves this delegate into std::move_only_function. 
	 * @usage `auto fn = std::move(callback).as_std_move_only_function();` 
	 */
	[[nodiscard]] std::move_only_function<RetType(Args...)> as_std_move_only_function() &&
	{
		MoveOnlyDelegateBase Moved = std::move(*this);
		return [Moved = std::move(Moved)](Args... InArgs) mutable -> RetType {
			return Moved.execute(std::forward<Args>(InArgs)...);
		};
	}
};

} // namespace details

template <typename RetType, typename... Args>
using delegate = details::DelegateBase<RetType, false, Args...>;

template <typename RetType, typename... Args>
using move_only_delegate = details::MoveOnlyDelegateBase<RetType, false, Args...>;

template <typename Ty>
class multicast_delegate;

/**
 * @brief Ordered multicast delegate with generation-safe reusable handles and mutation-safe broadcasting.
 * @usage
 *   - core::multicast_delegate<void(int)> callbacks;
 *   - const auto handle = callbacks.add_lambda([](int) {});
 *   - callbacks.broadcast(42);
 *   - callbacks.remove(handle);
 */
template <typename RetType, typename... Args>
class multicast_delegate<RetType(Args...)>
{
	using DelegateType = delegate<RetType, Args...>;
	static constexpr std::uint32_t InvalidSlot = std::numeric_limits<std::uint32_t>::max();
	static constexpr bool SupportsFanoutArguments = (
		...
		&& (!std::is_rvalue_reference_v<Args>
			&& (std::is_reference_v<Args> || std::copy_constructible<Args>))
	);

	struct Slot
	{
		DelegateType Callback;
		std::uint32_t Generation = 1;
		std::uint32_t NextFree = InvalidSlot;
		bool Active = false;
	};

	struct OrderedSlot
	{
		std::uint32_t Index = InvalidSlot;
		std::uint32_t Generation = 0;
	};

public:
	multicast_delegate() = default;
	multicast_delegate(const multicast_delegate&) = default;
	multicast_delegate(multicast_delegate&&) = default;
	multicast_delegate& operator=(const multicast_delegate&) = default;
	multicast_delegate& operator=(multicast_delegate&&) = default;

	DelegateHandle add(DelegateType&& InDelegate)
	{
		const std::uint32_t SlotIndex = acquireSlot();
		Slot& Entry = Slots[SlotIndex];
		try
		{
			Order.push_back(OrderedSlot{SlotIndex, Entry.Generation});
		}
		catch (...)
		{
			releaseUnboundSlot(SlotIndex);
			throw;
		}

		Entry.Callback = std::move(InDelegate);
		Entry.Active = true;
		++ActiveCount;
		return makeHandle(SlotIndex, Entry.Generation);
	}

	DelegateHandle add(const DelegateType& InDelegate)
	{
		DelegateType Copy(InDelegate);
		return add(std::move(Copy));
	}

	template <auto Method, typename ClassType>
	DelegateHandle add_raw(ClassType* Instance)
	{
		DelegateType Callback;
		Callback.template bind_raw<Method>(Instance);
		return add(std::move(Callback));
	}

	template <auto Method, typename ClassType>
	DelegateHandle add_raw(const ClassType* Instance)
	{
		DelegateType Callback;
		Callback.template bind_raw<Method>(Instance);
		return add(std::move(Callback));
	}

	/** 
	 * @brief Adds a static/free function binding. 
	 * @usage `callbacks.add_static<&OnValue>();` 
	 */
	template <auto Callable>
	DelegateHandle add_static()
	{
		DelegateType Callback;
		Callback.template bind_static<Callable>();
		return add(std::move(Callback));
	}

	/** 
	 * @brief Adds a shared_ptr-owned member-function binding. 
	 * @usage `callbacks.add_shared<&Receiver::Handle>(receiver);` 
	 */
	template <auto Method, typename ClassType>
	DelegateHandle add_shared(std::shared_ptr<ClassType> InstancePtr)
	{
		DelegateType Callback;
		Callback.template bind_shared<Method>(std::move(InstancePtr));
		return add(std::move(Callback));
	}

	/** 
	 * @brief Adds a weak_ptr member-function binding. 
	 * @usage `callbacks.add_weak<&Receiver::Handle>(weak_receiver);` 
	 */
	template <auto Method, typename ClassType>
	DelegateHandle add_weak(const std::weak_ptr<ClassType>& InstanceWeakPtr)
	{
		DelegateType Callback;
		Callback.template bind_weak<Method>(InstanceWeakPtr);
		return add(std::move(Callback));
	}

	/** 
	 * @brief Adds a copyable lambda/callable binding. 
	 * @usage `callbacks.add_lambda([](int value) { return value + 1; });` 
	 */
	template <typename Callable>
	DelegateHandle add_lambda(Callable&& InCallable)
	{
		DelegateType Callback;
		Callback.bind_lambda(std::forward<Callable>(InCallable));
		return add(std::move(Callback));
	}

	/** 
	 * @brief Marks a callback removed in amortized O(1) time. 
	 * @usage `callbacks.remove(handle);` 
	 */
	bool remove(const DelegateHandle& Handle)
	{
		if (!contains(Handle))
		{
			return false;
		}

		const std::uint32_t SlotIndex = slotIndex(Handle);
		retireSlot(SlotIndex);

		if (BroadcastDepth == 0)
		{
			finalizeRetiredSlots();
		}
		return true;
	}

	/** 
	 * @brief Checks whether a handle refers to an active callback. 
	 * @usage `if (callbacks.contains(handle)) callbacks.remove(handle);` 
	 */
	[[nodiscard]] bool contains(const DelegateHandle& Handle) const noexcept
	{
		return Handle.isValid()
			&& isActive(slotIndex(Handle), generation(Handle));
	}

	void clear()
	{
		for (const OrderedSlot& Entry : Order)
		{
			if (isActive(Entry.Index, Entry.Generation))
			{
				retireSlot(Entry.Index);
			}
		}
		if (BroadcastDepth == 0)
		{
			finalizeRetiredSlots();
		}
	}

	[[nodiscard]] bool bound() const noexcept
	{
		return ActiveCount != 0;
	}

	[[nodiscard]] std::size_t size() const noexcept
	{
		return ActiveCount;
	}

	/** 
	 * @brief Broadcasts to callbacks in registration order and permits deferred add/remove. Value arguments are copied per callback; rvalue-reference signatures are unsupported. 
	 * @usage `callbacks.broadcast(42);` 
	 */
	void broadcast(Args... InArgs) const
		requires std::is_void_v<RetType> && SupportsFanoutArguments
	{
		BroadcastScope Scope(*this);
		const std::size_t InitialOrderSize = Order.size();
		for (std::size_t OrderIndex = 0; OrderIndex < InitialOrderSize; ++OrderIndex)
		{
			const OrderedSlot Entry = Order[OrderIndex];
			if (isActive(Entry.Index, Entry.Generation))
			{
				Slots[Entry.Index].Callback.execute(InArgs...);
			}
		}
	}

	/** 
	 * @brief Invokes callbacks in registration order and collects non-void results. Value arguments are copied per callback; rvalue-reference signatures are unsupported. 
	 * @usage `auto values = callbacks.collect(42);` 
	 */
	[[nodiscard]] std::vector<RetType> collect(Args... InArgs) const
		requires (!std::is_void_v<RetType>) && SupportsFanoutArguments
	{
		BroadcastScope Scope(*this);
		std::vector<RetType> Results;
		Results.reserve(ActiveCount);
		const std::size_t InitialOrderSize = Order.size();
		for (std::size_t OrderIndex = 0; OrderIndex < InitialOrderSize; ++OrderIndex)
		{
			const OrderedSlot Entry = Order[OrderIndex];
			if (isActive(Entry.Index, Entry.Generation))
			{
				Results.push_back(Slots[Entry.Index].Callback.execute(InArgs...));
			}
		}
		return Results;
	}

private:
	class BroadcastScope
	{
	public:
		explicit BroadcastScope(const multicast_delegate& Owner) noexcept
			: OwnerRef(Owner)
		{
			++OwnerRef.BroadcastDepth;
		}

		~BroadcastScope() noexcept
		{
			if (--OwnerRef.BroadcastDepth == 0)
			{
				OwnerRef.finalizeRetiredSlots();
			}
		}

	private:
		const multicast_delegate& OwnerRef;
	};

	[[nodiscard]] static constexpr DelegateHandle makeHandle(std::uint32_t SlotIndex, std::uint32_t Generation) noexcept
	{
		return DelegateHandle{
			(static_cast<std::uint64_t>(Generation) << 32) | (static_cast<std::uint64_t>(SlotIndex) + 1)
		};
	}

	[[nodiscard]] static constexpr std::uint32_t slotIndex(const DelegateHandle& Handle) noexcept
	{
		return static_cast<std::uint32_t>(Handle.Id) - 1;
	}

	[[nodiscard]] static constexpr std::uint32_t generation(const DelegateHandle& Handle) noexcept
	{
		return static_cast<std::uint32_t>(Handle.Id >> 32);
	}

	[[nodiscard]] bool isActive(std::uint32_t SlotIndex, std::uint32_t Generation) const noexcept
	{
		return SlotIndex < Slots.size()
			&& Slots[SlotIndex].Active
			&& Slots[SlotIndex].Generation == Generation;
	}

	void retireSlot(std::uint32_t SlotIndex) noexcept
	{
		Slot& Entry = Slots[SlotIndex];
		Entry.Active = false;
		Entry.Generation = nextGeneration(Entry.Generation);
		Entry.NextFree = RetiredHead;
		RetiredHead = SlotIndex;
		--ActiveCount;
		++TombstoneCount;
	}

	void releaseUnboundSlot(std::uint32_t SlotIndex) noexcept
	{
		Slot& Entry = Slots[SlotIndex];
		Entry.NextFree = FreeHead;
		FreeHead = SlotIndex;
	}

	void releaseRetiredSlots() const noexcept
	{
		while (RetiredHead != InvalidSlot)
		{
			const std::uint32_t SlotIndex = RetiredHead;
			Slot& Entry = Slots[SlotIndex];
			RetiredHead = Entry.NextFree;
			Entry.Callback.reset();
			Entry.NextFree = FreeHead;
			FreeHead = SlotIndex;
		}
	}

	[[nodiscard]] bool shouldCompactOrder() const noexcept
	{
		return ActiveCount == 0
			|| (TombstoneCount >= 16 && TombstoneCount * 2 >= Order.size());
	}

	void finalizeRetiredSlots() const noexcept
	{
		releaseRetiredSlots();
		if (shouldCompactOrder())
		{
			compactOrder();
		}
	}

	[[nodiscard]] std::uint32_t acquireSlot()
	{
		if (FreeHead != InvalidSlot)
		{
			const std::uint32_t SlotIndex = FreeHead;
			FreeHead = Slots[SlotIndex].NextFree;
			Slots[SlotIndex].NextFree = InvalidSlot;
			return SlotIndex;
		}

		if (Slots.size() >= std::numeric_limits<std::uint32_t>::max())
		{
			throw std::length_error("multicast_delegate slot capacity exhausted");
		}

		Slots.emplace_back();
		return static_cast<std::uint32_t>(Slots.size() - 1);
	}

	static constexpr std::uint32_t nextGeneration(std::uint32_t Current) noexcept
	{
		++Current;
		return Current == 0 ? 1 : Current;
	}

	void compactOrder() const noexcept
	{
		Order.erase(
			std::remove_if(
				Order.begin(),
				Order.end(),
				[this](const OrderedSlot& Entry) {
					return !isActive(Entry.Index, Entry.Generation);
				}
			),
			Order.end()
		);
		TombstoneCount = 0;
	}

private:
	mutable std::deque<Slot> Slots;
	mutable std::vector<OrderedSlot> Order;
	mutable std::uint32_t FreeHead = InvalidSlot;
	mutable std::uint32_t RetiredHead = InvalidSlot;
	mutable std::size_t ActiveCount = 0;
	mutable std::size_t BroadcastDepth = 0;
	mutable std::size_t TombstoneCount = 0;
};
} // namespace core
