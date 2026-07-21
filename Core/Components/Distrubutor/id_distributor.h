#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <bit>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "../../common.h"
namespace core::components
{
constexpr uint64_t InvalidDestributedId = 0;

class IdDestributor
{
	using DestributedIdType = uint64_t;

public:
  	static IdDestributor &Self()
	{
		static IdDestributor instance;
		return instance;
	}

	DestributedIdType acquireId();
	
	/**
	 * @brief
	 *	占用一个实际的 Id; 其不同于申请, 如果一个对象从磁盘中反序列化出来, 其可能本身就
     *  记录了一个 Id, 此时需要通过该API申请占用该 Id. 此时如果该 Id 已经被占用, 则会
	 *  返回一个新的 Id.
	 * @param Id The ID to occupy.
	 * @return true if the ID was successfully occupied, false if it was already taken.
	 */
	DestributedIdType occupyId(DestributedIdType Id);
	void releaseId(DestributedIdType Id) noexcept;
private:
  	IdDestributor();
  	~IdDestributor() = default;
   	IdDestributor(const IdDestributor&) = delete;
    IdDestributor(IdDestributor&&) = delete;

	static constexpr std::size_t MinStripeCount = 128;
	static constexpr std::size_t InitialActiveCapacityPerStripe = 128;
	static constexpr std::size_t InitialFreeCapacityPerStripe = 64;

	static std::uint64_t mix64(std::uint64_t X) noexcept 
	{
        // SplitMix64 finalizer.
        X += 0X9e3779b97f4a7c15ull;
        X = (X ^ (X >> 30)) * 0Xbf58476d1ce4e5b9ull;
        X = (X ^ (X >> 27)) * 0X94d049bb133111ebull;
        return X ^ (X >> 31);
    }

	static std::size_t normalizeCapacity(std::size_t Capacity) noexcept 
	{
		Capacity = std::max<std::size_t>(Capacity, 16);
        return std::bit_ceil(Capacity);
	}

	enum class SlotState : std::uint8_t 
	{
		Empty,
		Occupied,
		Tombstone
	};

	struct ActiveSetImpl
	{
		std::size_t Capacity = 0;
		std::size_t Size = 0;
		std::size_t TombstoneCount = 0;

		std::unique_ptr<DestributedIdType[]> Keys;
		std::unique_ptr<SlotState[]> States;
		
		bool insert(DestributedIdType Id);
		bool erase(DestributedIdType Id);
		bool contains(DestributedIdType Id) const noexcept;
		void reserve(std::size_t NewCapacity);

	private:
		void ensureInsertCapacity();
		void rehash(std::size_t NewCapacity);
		void insertDuringRehash(DestributedIdType Id) noexcept;
	};

	struct alignas(CacheLineSize) ActiveStripe
	{
		std::mutex Mtx;
		ActiveSetImpl Set;
	};
	
	struct alignas(CacheLineSize) FreeStripe
	{
		std::mutex Mtx;
		std::vector<DestributedIdType> Ids;
	};

private:
	std::size_t stripeIndex(DestributedIdType Id) const noexcept
	{
		return static_cast<std::size_t>(mix64(Id)) & (StripeCount - 1);
	}

	std::size_t cursorStripeIndex(std::size_t Cursor) const noexcept
	{
		return Cursor & (StripeCount - 1);
	}

	bool activeInsert(DestributedIdType Id);
	bool activeErase(DestributedIdType Id) noexcept;
	bool activeContains(DestributedIdType Id) const noexcept;

	DestributedIdType popFree() noexcept;
	bool pushFree(DestributedIdType Id) noexcept;
	DestributedIdType acquireFreshId();
private:
	std::size_t StripeCount = 0;
	std::unique_ptr<ActiveStripe[]> ActiveStripes;
	std::unique_ptr<FreeStripe[]> FreeStripes;

	std::atomic<DestributedIdType> NextId {1};
	std::atomic<std::size_t> FreeCursor {0};

};

}
