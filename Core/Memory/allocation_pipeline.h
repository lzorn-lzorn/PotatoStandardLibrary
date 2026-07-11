
#pragma once

#include "memory_common.h"
namespace core::mem
{

template <CAllocationStage ... Stages>
class AllocationPipeline
{
public:
	/**
	 * @brief Execute allocation pipeline stages in declaration order.
	 * @param
	 *     - context  AllocationContext&  Shared stage context for one allocation request.
	 * @usage
	 *     - Use for composing static stage chains without virtual dispatch.
	 * @return
	 *     - void
	 */
	static void allocate(AllocationContext& context)
	{
		(Stages::allocate(context), ...);
	}

	/**
	 * @brief Execute deallocation pipeline stages in declaration order.
	 * @param
	 *     - context  AllocationContext&  Shared stage context for one deallocation request.
	 * @usage
	 *     - Keep this order symmetrical with allocate() when adding new stages.
	 * @return
	 *     - void
	 */
	static void deallocate(AllocationContext& context)
	{
		(Stages::deallocate(context), ...);
	}
};

} // namespace core::mem