
#pragma once

#include <cstdint>
#include <concepts>
#include <cstring>
#include <type_traits>
#include <utility>
#include <memory>
#include <exception>
#include <functional>

namespace core
{
constexpr std::uint32_t DelegateSmallStorageSize = 64;
struct DelegateHandle
{

	uint64_t Id = 0;

	[[nodiscard]] bool isValid() const noexcept
	{
		return Id != 0;
	}

	void reset() noexcept
	{
		Id = 0;
	}

	bool operator==(const DelegateHandle& Other) const noexcept
	{
		return Id == Other.Id;
	}
};

namespace details
{
template <bool IsCopyable, typename RetType, typename... Args>
class Storage
{
public:
	using IvokerType = RetType(*)(void*, Args...);
	using DestructorType = void(*)(void*);
	using MoveConstructorType = void(*)(void*, void*);
	using CopyConstructorType = void(*)(void*, const void*);

	Storage() noexcept = default;
	~Storage() 
	{

	}
	
	RetType execute(Args... args) const
	{
		if (!Invoker)
		{
			throw std::bad_function_call();
		}
		return Invoker(getInnerPtr(), std::forward<Args>(args)...);
	}
private:
	template <typename Callable>
	void initFromCallable(Callable&& InCallable)
	{
		using DecayedType = std::decay_t<Callable>;
		static_assert(std::is_invocable_r_v<RetType, DecayedType, Args...>,
                          "Callable must be invocable with given Args... and return Ret");
		
		constexpr size_t InlineStorageSize = DelegateSmallStorageSize;
		constexpr size_t InlineAlignment = alignof(std::max_align_t);

		if constexpr (sizeof(DecayedType) <= InlineStorageSize 
			&& alignof(DecayedType) <= InlineAlignment)
		{
			std::construct_at(
				reinterpret_cast<DecayedType*>(&StorageBuffer), 
				std::forward<Callable>(InCallable)
			);
			IsHeapAllocated = false;
			Invoker = +[](void* StoragePtr, Args... args) -> RetType {
				return std::invoke(
					*static_cast<DecayedType*>(StoragePtr), 
					std::forward<Args>(args)...
				);
			};

			Destructor = +[](void* StoragePtr) {
				std::destroy_at(static_cast<DecayedType*>(StoragePtr));
			};

			MoveConstructor = +[](void* Dest, void* Src) {
				std::construct_at(
					static_cast<DecayedType*>(Dest), 
					std::move(*static_cast<DecayedType*>(Src))
				);
				std::destroy_at(static_cast<DecayedType*>(Src));
			};

			if constexpr (IsCopyable)
			{
				CopyConstructor = +[](void* Dest, const void* Src) {
					std::construct_at(
						static_cast<DecayedType*>(Dest), 
						*static_cast<const DecayedType*>(Src)
					);
				};
			}
		}
		else
		{
			DecayedType* FromHeap = std::allocator<DecayedType>{}.allocate(1);
			std::construct_at(FromHeap, std::forward<Callable>(InCallable));

			*reinterpret_cast<DecayedType**>(&StorageBuffer) = FromHeap;
			IsHeapAllocated = true;
			Invoker = +[](void* StoragePtr, Args... args) -> RetType {
				return std::invoke(
					*static_cast<DecayedType*>(StoragePtr), 
					std::forward<Args>(args)...
				);
			};

			Destructor = +[](void* StoragePtr) {
				std::destroy_at(static_cast<DecayedType*>(StoragePtr));
				std::allocator<DecayedType>{}.deallocate(static_cast<DecayedType*>(StoragePtr), 1);
			};

			MoveConstructor = +[](void* Dest, void* Src) {
				std::construct_at(
					static_cast<DecayedType*>(Dest), 
					std::move(*static_cast<DecayedType*>(Src))
				);
				std::destroy_at(static_cast<DecayedType*>(Src));
				std::allocator<DecayedType>{}.deallocate(static_cast<DecayedType*>(Src), 1);
			};

			if constexpr (IsCopyable)
			{
				CopyConstructor = +[](void* Dest, const void* Src) {
					std::construct_at(
						static_cast<DecayedType*>(Dest), 
						*static_cast<const DecayedType*>(Src)
					);
				};
			}
		}
	}

private:
	void* getInnerPtr() noexcept { return IsHeapAllocated ? *reinterpret_cast<void**>(&StorageBuffer) : &StorageBuffer; }

	const void* getInnerPtr() const noexcept { return IsHeapAllocated ? *reinterpret_cast<const void* const*>(&StorageBuffer) : &StorageBuffer; }

	void destroy() noexcept
	{
		if (Destructor && Invoker)
		{
			Destructor(getInnerPtr());
			Invoker = nullptr;
		}
	}

	void moveFrom(Storage&& Other) noexcept
	{
		if (Other.Invoker)
		{
			MoveConstructor(getInnerPtr(), Other.getInnerPtr());
			Invoker = Other.Invoker;
			Destructor = Other.Destructor;
			MoveConstructor = Other.MoveConstructor;
			CopyConstructor = Other.CopyConstructor;
			IsHeapAllocated = Other.IsHeapAllocated;

			if constexpr (IsCopyable)
			{
				CopyConstructor = Other.CopyConstructor;
			}

			if (Other.Invoker)
			{
				MoveConstructor(getInnerPtr(), Other.getInnerPtr());
				Other.Invoker = nullptr;
			}
		}
	}

	void copyFrom(const Storage& Other) noexcept
	{
		CopyConstructor(getInnerPtr(), Other.getInnerPtr());
		Invoker = Other.Invoker;
		Destructor = Other.Destructor;
		MoveConstructor = Other.MoveConstructor;
		CopyConstructor = Other.CopyConstructor;
		IsHeapAllocated = Other.IsHeapAllocated;
		if (Other.Invoker && CopyConstructor)
		{
			CopyConstructor(getInnerPtr(), Other.getInnerPtr());
		}
		else if (Other.Invoker && !CopyConstructor)
		{
			std::memcpy(getInnerPtr(), Other.getInnerPtr(), sizeof(StorageBuffer));
		}
	}
private:
	alignas(std::max_align_t) char StorageBuffer[DelegateSmallStorageSize] {};

	bool                IsHeapAllocated = false;
	IvokerType          Invoker         = nullptr;
	DestructorType      Destructor      = nullptr;
	MoveConstructorType MoveConstructor = nullptr;
	CopyConstructorType CopyConstructor = nullptr;
};


} // namespace details
} // namespace core