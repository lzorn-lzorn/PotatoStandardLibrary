
#pragma once

#include <cstdint>
#include <concepts>
#include <type_traits>
#include <utility>
#include <memory>

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
			
		}
	}

private:
	alignas(std::max_align_t) char StorageBuffer[DelegateSmallStorageSize];

	IvokerType          Invoker         = nullptr;
	DestructorType      Destructor      = nullptr;
	MoveConstructorType MoveConstructor = nullptr;
	CopyConstructorType CopyConstructor = nullptr;
};


} // namespace details
} // namespace core