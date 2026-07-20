#pragma once

#include <atomic>
#include <unordered_set>
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

	DestributedIdType acquireId()
	{
		std::atomic<DestributedIdType> CurrentId {1};
		while(Ids.contains(CurrentId))
		{
			++CurrentId;
		}
		Ids.insert(CurrentId);
		return CurrentId;
	}

	/**
	 * @brief
	 *	占用一个实际的 Id; 其不同于申请, 如果一个对象从磁盘中反序列化出来, 其可能本身就
     *  记录了一个 Id, 此时需要通过该API申请占用该 Id. 此时如果该 Id 已经被占用, 则会
	 *  返回一个新的 Id.
	 * @param Id The ID to occupy.
	 * @return true if the ID was successfully occupied, false if it was already taken.
	 */
	DestributedIdType occupyId(DestributedIdType Id)
	{
		if (Ids.contains(Id))
		{
			return acquireId();
		}
		Ids.insert(Id);
		return Id;
	}

	void releaseId(DestributedIdType Id) noexcept
	{
		if (Ids.contains(Id)) [[likely]]
		{
			Ids.erase(Id);
		}
	}

private:
  	IdDestributor() = default;
  	~IdDestributor() = default;
   	IdDestributor(const IdDestributor&) = delete;
    IdDestributor(IdDestributor&&) = delete;
private:
	std::unordered_set<DestributedIdType> Ids;
    std::atomic<DestributedIdType> CurrentId {1};
};

}
