
#include "id_distributor.h"
#include <bit>
#include <limits>
#include <mutex>

namespace core::components
{
IdDestributor::IdDestributor()
{
	const std::size_t HC = std::thread::hardware_concurrency();
	const std::size_t DesiredStripeCount = std::max<std::size_t>(MinStripeCount, HC*4);

	StripeCount = std::bit_ceil(DesiredStripeCount);
	ActiveStripes = std::make_unique<ActiveStripe[]>(StripeCount);
	FreeStripes = std::make_unique<FreeStripe[]>(StripeCount);

	for (std::size_t i = 0; i < StripeCount; ++i)
	{
		ActiveStripes[i].Set.reserve(InitialActiveCapacityPerStripe);
		FreeStripes[i].Ids.reserve(InitialFreeCapacityPerStripe);
	}
}


void IdDestributor::ActiveSetImpl::reserve(std::size_t RequestedCapacity)
{
	if (RequestedCapacity <= Capacity) [[unlikely]]
	{
		return ;
	}

	rehash(normalizeCapacity(RequestedCapacity));
}

void IdDestributor::ActiveSetImpl::ensureInsertCapacity()
{
	if (Capacity == 0) 
	{
		rehash(16);
		return ;
	}

	const std::size_t Used = Size + TombstoneCount + 1;
	if (Used * 10 > Capacity * 7)
	{
		rehash(normalizeCapacity(Capacity * 2));
		return ;
	}

	if (TombstoneCount > 32 && TombstoneCount > Size)
	{
		rehash(Capacity);
	}
}

bool IdDestributor::ActiveSetImpl::insert(DestributedIdType Id)
{
	if (Id == InvalidDestributedId) [[unlikely]]
	{
		return false;
	}

	ensureInsertCapacity();

	const std::size_t Mask = Capacity - 1;
	const std::size_t Start = static_cast<std::size_t>(mix64(Id)) & Mask;

	std::size_t FirstTombstone = std::numeric_limits<std::size_t>::max();

	for (std::size_t Probe = 0; Probe < Capacity; ++Probe)
	{
		const std::size_t Index = (Start + Probe) & Mask;

		switch (States[Index])
		{
			case SlotState::Empty: {
				const std::size_t Target = 
					FirstTombstone != std::numeric_limits<std::size_t>::max() ? 
					FirstTombstone : Index;

				Keys[Target] = Id;
				if (States[Target] == SlotState::Tombstone)
				{
					--TombstoneCount;
				}
				
				States[Target] = SlotState::Occupied;
				++Size;
				return true;
			}
			case SlotState::Occupied : 
			{
				if (Keys[Index] == Id)
				{
					return false;
				}
				break;
			}
			case SlotState::Tombstone: 
			{
				if (FirstTombstone == std::numeric_limits<std::size_t>::max())
				{
					FirstTombstone = Index;
				}
				break;
			}
		}
	}

	// Unreachable
	rehash(Capacity * 2);
	return insert(Id);
}

bool IdDestributor::ActiveSetImpl::erase(DestributedIdType Id)
{
	if (Id == InvalidDestributedId || Capacity == 0) [[unlikely]]
	{
		return false;
	}

	const std::size_t Mask = Capacity - 1;
	const std::size_t Start = static_cast<std::size_t>(mix64(Id)) & Mask;

	for (std::size_t Probe = 0; Probe < Capacity; ++Probe)
	{
		const std::size_t Index = (Start + Probe) & Mask;

		switch (States[Index])
		{
			case SlotState::Empty:
				return false;
			case SlotState::Occupied : 
			{
				if (Keys[Index] == Id)
				{
					Keys[Index] = InvalidDestributedId;
					States[Index] = SlotState::Tombstone;
					--Size;
					++TombstoneCount;
					return true;
				}
				break;
			}
			case SlotState::Tombstone: 
				break;
		}
	}

	return false;
}

bool IdDestributor::ActiveSetImpl::contains(DestributedIdType Id) const noexcept
{
	if (Id == InvalidDestributedId || Capacity == 0) [[unlikely]]
	{
		return false;
	}

	const std::size_t Mask = Capacity - 1;
	const std::size_t Start = static_cast<std::size_t>(mix64(Id)) & Mask;

	for (std::size_t Probe = 0; Probe < Capacity; ++Probe)
	{
		const std::size_t Index = (Start + Probe) & Mask;

		switch (States[Index])
		{
			case SlotState::Empty:
				return false;
			case SlotState::Occupied : 
			{
				if (Keys[Index] == Id)
				{
					return true;
				}
				break;
			}
			case SlotState::Tombstone: 
				break;
		}
	}

	return false;
}

void IdDestributor::ActiveSetImpl::rehash(std::size_t NewCapacity)
{
	const std::size_t OldCapacity = Capacity;
	const std::size_t OldSize = Size;

	NewCapacity = normalizeCapacity(std::max<std::size_t>(NewCapacity, OldSize * 2 + 16));

	auto NewKeys = std::make_unique<DestributedIdType[]>(NewCapacity);
	auto NewStates = std::make_unique<SlotState[]>(NewCapacity);

	for (size_t i = 0; i < NewCapacity; ++i)
	{
		NewKeys[i] = InvalidDestributedId;
		NewStates[i] = SlotState::Empty;
	}

	auto OldKeys = std::move(Keys);
	auto OldStates = std::move(States);

	Keys = std::move(NewKeys);
	States = std::move(NewStates);
	Capacity = NewCapacity;
	Size = 0;
	TombstoneCount = 0;

	for (std::size_t i = 0; i < OldCapacity; ++i)
	{
		if (OldStates[i] == SlotState::Occupied)
		{
			insertDuringRehash(OldKeys[i]);
		}
	}
}

void IdDestributor::ActiveSetImpl::insertDuringRehash(DestributedIdType Id) noexcept
{
	const std::size_t Mask = Capacity - 1;
	const std::size_t Start = static_cast<std::size_t>(mix64(Id)) & Mask;

	for (std::size_t Probe = 0; Probe < Capacity; ++Probe)
	{
		const std::size_t Index = (Start + Probe) & Mask;

		if (States[Index] == SlotState::Empty)
		{
			Keys[Index] = Id;
			States[Index] = SlotState::Occupied;
			++Size;
			return ;
		}
	}

	// Unreachable
	// std::terminate();

}

bool IdDestributor::activeInsert(DestributedIdType Id)
{
	ActiveStripe& Stripe = ActiveStripes[stripeIndex(Id)];
	std::lock_guard<std::mutex> Lock(Stripe.Mtx);
	return Stripe.Set.insert(Id);
}

bool IdDestributor::activeErase(DestributedIdType Id) noexcept
{
	try
	{
		ActiveStripe& Stripe = ActiveStripes[stripeIndex(Id)];
		std::lock_guard<std::mutex> Lock(Stripe.Mtx);
		return Stripe.Set.erase(Id);
	}
	catch (...)
	{
		// std::lock_guard<std::mutex> Lock(Stripe.Mtx); 实际上有可能抛出 system_error
		// 但是该函数是 noexcept 的, 所以这里捕获异常并返回 false. 如果外界在构建时禁用异常,
		// 此时会直接崩溃 std::terminate() 
		return false;
	}
}

bool IdDestributor::activeContains(DestributedIdType Id) const noexcept
{
	try
	{
		ActiveStripe& Stripe = ActiveStripes[stripeIndex(Id)];
		std::lock_guard<std::mutex> Lock(Stripe.Mtx);
		return Stripe.Set.contains(Id);
	}
	catch (...)
	{
		// std::lock_guard<std::mutex> Lock(Stripe.Mtx); 实际上有可能抛出 system_error
		// 但是该函数是 noexcept 的, 所以这里捕获异常并返回 false. 如果外界在构建时禁用异常,
		// 此时会直接崩溃 std::terminate() 
		return false;
	}
}

IdDestributor::DestributedIdType IdDestributor::popFree() noexcept
{
	const std::size_t Start = FreeCursor.fetch_add(1, std::memory_order_relaxed);

	for (std::size_t I = 0; I < StripeCount; ++I)
	{
		FreeStripe& Stripe = FreeStripes[cursorStripeIndex(Start + I)];

		try {
			std::lock_guard<std::mutex> Lock(Stripe.Mtx);
			
			if (!Stripe.Ids.empty())
			{
				DestributedIdType Id = Stripe.Ids.back();
				Stripe.Ids.pop_back();
				return Id;
			}
		} catch (...) {
			// ignore
			
		}
	}
	return InvalidDestributedId;
}

bool IdDestributor::pushFree(DestributedIdType Id) noexcept
{
	if (Id == InvalidDestributedId) [[unlikely]]
	{
		return false;
	}

	const std::size_t Stripe = stripeIndex(Id);
	FreeStripe& StripeRef = FreeStripes[Stripe];

	try {
		std::lock_guard<std::mutex> Lock(StripeRef.Mtx);
		StripeRef.Ids.push_back(Id);
		return true;
	} catch (...) {
		return false;
	}
}

IdDestributor::DestributedIdType IdDestributor::acquireFreshId()
{
	while(true)
	{
		const DestributedIdType Id = NextId.fetch_add(1, std::memory_order_relaxed);
		if (Id == InvalidDestributedId) [[unlikely]]
		{
			continue;
		}

		if (activeInsert(Id))
		{
			return Id;
		}
	}
}

IdDestributor::DestributedIdType IdDestributor::acquireId()
{
	while(true)
	{
		DestributedIdType Id = popFree();
		if (Id == InvalidDestributedId)
		{
			return acquireFreshId();
		}

		if (activeInsert(Id))
		{
			return Id;
		}
	}
	// The recycled ID may have been occupied explicitly between releaseId() 
	// erasing it from activeSet and pushing it into FreeList.
	// In that case this stale free-list entry is safely discarded.
}

IdDestributor::DestributedIdType IdDestributor::occupyId(DestributedIdType Id)
{
	if (Id == InvalidDestributedId) [[unlikely]]
	{
		return acquireId();
	}

	if (activeInsert(Id))
	{
		return Id;
	}
	else
	{
		return acquireId();
	}
}

void IdDestributor::releaseId(DestributedIdType Id) noexcept
{
	if (Id == InvalidDestributedId) [[unlikely]]
	{
		return;
	}

	if (!activeErase(Id))
	{
		return;
	}

    // If pushing to the free pool fails, the ID simply won't be reused.
    // Correctness is preserved because it has already been removed from activeSet.
	pushFree(Id);
}


} // namespace core::components 

