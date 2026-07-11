#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../memory_common.h"

namespace core::mem
{

struct LeakRecord
{
	std::size_t Size = 0;
	std::size_t Alignment = 0;
	std::uint64_t AllocationId = 0;
	std::uint64_t Timestamp = 0;
	std::uint32_t FrameIndex = 0;
	std::uint32_t ResourceId = 0;
	std::thread::id ThreadId = std::this_thread::get_id();
};

class MemoryLeakDetectorObserver
{
public:
	/**
	 * @brief Consume allocator events and maintain live-allocation map.
	 * @param
	 *     - event  const MemoryEvent&  Event emitted by allocator pipeline.
	 * @usage
	 *     - Bind this observer when leak-tracking is required.
	 * @return
	 *     - void
	 */
	void onMemoryEvent(const MemoryEvent& event)
	{
		if (event.Type == MemoryEventType::Allocate)
		{
			LeakRecord record;
			record.Size = event.Size;
			record.Alignment = event.Alignment;
			record.AllocationId = event.AllocationId;
			record.Timestamp = event.Timestamp;
			record.FrameIndex = event.FrameIndex;
			record.ResourceId = event.ResourceId;
			record.ThreadId = event.ThreadId;

			std::lock_guard lock(Mutex);
			LiveAllocations[event.UserPtr] = record;
			return;
		}

		if (event.Type == MemoryEventType::Deallocate)
		{
			std::lock_guard lock(Mutex);
			LiveAllocations.erase(event.UserPtr);
		}
	}

	/**
	 * @brief Check whether there are remaining live allocations.
	 * @param
	 *     - none
	 * @usage
	 *     - Useful before shutdown assertion.
	 * @return
	 *     - bool  true if no leak record exists.
	 */
	[[nodiscard]] bool isEmpty() const
	{
		std::lock_guard lock(Mutex);
		return LiveAllocations.empty();
	}

	/**
	 * @brief Get number of tracked live allocations.
	 * @param
	 *     - none
	 * @usage
	 *     - Use in leak report summary output.
	 * @return
	 *     - std::size_t  Live allocation record count.
	 */
	[[nodiscard]] std::size_t getLeakCount() const
	{
		std::lock_guard lock(Mutex);
		return LiveAllocations.size();
	}

	/**
	 * @brief Build a stable leak snapshot for reporting.
	 * @param
	 *     - none
	 * @usage
	 *     - Call at frame end or process shutdown to print unresolved allocations.
	 * @return
	 *     - std::vector<std::pair<void*, LeakRecord>>  Copy of current leak records.
	 */
	[[nodiscard]] std::vector<std::pair<void*, LeakRecord>> getLeakSnapshot() const
	{
		std::lock_guard lock(Mutex);
		std::vector<std::pair<void*, LeakRecord>> leaks;
		leaks.reserve(LiveAllocations.size());
		for (const auto& [ptr, record] : LiveAllocations)
		{
			leaks.emplace_back(ptr, record);
		}
		return leaks;
	}

private:
	mutable std::mutex Mutex;
	std::unordered_map<void*, LeakRecord> LiveAllocations;
};

} // namespace core::mem
