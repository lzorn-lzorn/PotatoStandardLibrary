#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <ostream>
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
	 *     - Event  const MemoryEvent&  Event emitted by allocator pipeline.
	 * @usage
	 *     - Bind this observer when leak-tracking is required.
	 * @return
	 *     - void
	 */
	void onMemoryEvent(const MemoryEvent& Event)
	{
		if (Event.Type == MemoryEventType::Allocate)
		{
			LeakRecord Record;
			Record.Size = Event.Size;
			Record.Alignment = Event.Alignment;
			Record.AllocationId = Event.AllocationId;
			Record.Timestamp = Event.Timestamp;
			Record.FrameIndex = Event.FrameIndex;
			Record.ResourceId = Event.ResourceId;
			Record.ThreadId = Event.ThreadId;

			std::lock_guard Lock(Mutex);
			LiveAllocations[Event.UserPtr] = Record;
			return;
		}

		if (Event.Type == MemoryEventType::Deallocate)
		{
			std::lock_guard Lock(Mutex);
			LiveAllocations.erase(Event.UserPtr);
		}
	}

	/**
	 * @brief Check whether there are remaining live allocations.
	 * @param
	 *     - none
	 * @usage
	 *     - Useful before shutdown assertion.
	 * @return
	 *     - bool  true if no leak Record exists.
	 */
	[[nodiscard]] bool isEmpty() const
	{
		std::lock_guard Lock(Mutex);
		return LiveAllocations.empty();
	}

	/**
	 * @brief Get number of tracked live allocations.
	 * @param
	 *     - none
	 * @usage
	 *     - Use in leak report summary output.
	 * @return
	 *     - std::size_t  Live allocation Record count.
	 */
	[[nodiscard]] std::size_t getLeakCount() const
	{
		std::lock_guard Lock(Mutex);
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
		std::lock_guard Lock(Mutex);
		std::vector<std::pair<void*, LeakRecord>> Leaks;
		Leaks.reserve(LiveAllocations.size());
		for (const auto& [ptr, Record] : LiveAllocations)
		{
			Leaks.emplace_back(ptr, Record);
		}
		return Leaks;
	}

	/**
	 * @brief Print current unresolved leaks into output stream.
	 * @param
	 *     - Output  std::ostream&  Target stream for textual leak report.
	 * @usage
	 *     - Call explicitly on shutdown boundary instead of relying on destructors.
	 * @return
	 *     - bool  true when leaks are present and printed.
	 */
	[[nodiscard]] bool reportLeaks(std::ostream& Output) const
	{
		const auto Leaks = getLeakSnapshot();
		if (Leaks.empty())
		{
			Output << "[MemoryLeakDetector] no live allocations.\n";
			return false;
		}

		Output << "[MemoryLeakDetector] unresolved allocations=" << Leaks.size() << "\n";
		for (const auto& [Ptr, Record] : Leaks)
		{
			Output << "  ptr=" << Ptr
				<< " size=" << Record.Size
				<< " align=" << Record.Alignment
				<< " allocId=" << Record.AllocationId
				<< " frame=" << Record.FrameIndex
				<< " resource=" << Record.ResourceId
				<< "\n";
		}

		return true;
	}

private:
	mutable std::mutex Mutex;
	std::unordered_map<void*, LeakRecord> LiveAllocations;
};

} // namespace core::mem
