#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <mutex>
#include <new>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../memory_common.h"

namespace core::mem
{

inline constexpr std::size_t MemoryTrackerUnknownSize = std::numeric_limits<std::size_t>::max();

enum class HookAllocationOp : std::uint8_t
{
	New,
	Delete,
	NewArray,
	DeleteArray,
	Malloc,
	Free,
	Calloc,
	Realloc,
	AlignedNew,
	AlignedDelete,
	Unknown,
};

enum class MemoryIssueType : std::uint8_t
{
	DoubleAllocation,
	DoubleFree,
	InvalidFree,
	MismatchedFreeFunction,
	SizeMismatch,
	AlignmentMismatch,
	GuardCorruption,
	UseAfterFree,
	ValidationError,
	AllocationFailure,
	ReallocFailure,
};

struct MemoryPayloadPreview
{
	std::size_t CapturedBytes = 0;
	std::size_t OriginalBytes = 0;
	std::string Hex;
};

struct TraceRecord
{
	std::string Name;
	std::string Source;
	std::uint64_t TimestampUs = 0;
	std::uint32_t ThreadId = 0;
	std::uintptr_t Address = 0;
	std::size_t Size = 0;
	std::size_t Alignment = 0;
	std::uint64_t AllocationId = 0;
	std::uint32_t ResourceId = 0;
};

struct MemoryIssueRecord
{
	MemoryIssueType Type = MemoryIssueType::InvalidFree;
	HookAllocationOp Operation = HookAllocationOp::Unknown;
	std::uintptr_t Address = 0;
	std::size_t Size = 0;
	std::size_t Alignment = 0;
	std::uint64_t TimestampNs = 0;
	std::uint32_t ThreadId = 0;
	std::string Message;
	MemoryPayloadPreview Payload;
};

struct LiveAllocationRecord
{
	HookAllocationOp Operation = HookAllocationOp::Unknown;
	std::uintptr_t Address = 0;
	std::size_t Size = 0;
	std::size_t Alignment = 0;
	std::uint64_t AllocationId = 0;
	std::uint64_t TimestampNs = 0;
	std::uint32_t ThreadId = 0;
	std::uint32_t FrameIndex = 0;
	std::uint32_t ResourceId = 0;
	bool FromEngineEvent = false;
	bool FromThreadCache = false;
	bool FromCentralPool = false;
	bool FromOS = false;
	MemoryPayloadPreview LastPayload;
};

struct MemoryTrackerStatistics
{
	std::uint64_t EngineEventCount = 0;
	std::uint64_t HookEventCount = 0;

	std::uint64_t AllocateCount = 0;
	std::uint64_t DeallocateCount = 0;
	std::uint64_t ThreadCacheHitCount = 0;
	std::uint64_t ThreadCacheMissCount = 0;
	std::uint64_t CentralFetchCount = 0;
	std::uint64_t CentralReturnCount = 0;
	std::uint64_t OsAllocateCount = 0;
	std::uint64_t OsReleaseCount = 0;
	std::uint64_t GuardCorruptionCount = 0;
	std::uint64_t UseAfterFreeCount = 0;
	std::uint64_t ValidationErrorCount = 0;

	std::uint64_t HookAllocationCount = 0;
	std::uint64_t HookFreeCount = 0;
	std::uint64_t HookReallocCount = 0;
	std::uint64_t HookNullFreeCount = 0;
	std::uint64_t HookAllocationFailureCount = 0;

	std::uint64_t HookNewCount = 0;
	std::uint64_t HookDeleteCount = 0;
	std::uint64_t HookNewArrayCount = 0;
	std::uint64_t HookDeleteArrayCount = 0;
	std::uint64_t HookMallocCount = 0;
	std::uint64_t HookFreeFunctionCount = 0;
	std::uint64_t HookCallocCount = 0;
	std::uint64_t HookAlignedNewCount = 0;
	std::uint64_t HookAlignedDeleteCount = 0;

	std::uint64_t DoubleAllocationCount = 0;
	std::uint64_t DoubleFreeCount = 0;
	std::uint64_t InvalidFreeCount = 0;
	std::uint64_t MismatchedFreeFunctionCount = 0;
	std::uint64_t SizeMismatchCount = 0;
	std::uint64_t AlignmentMismatchCount = 0;
	std::uint64_t IssueCount = 0;

	std::size_t TotalAllocatedBytes = 0;
	std::size_t TotalFreedBytes = 0;
	std::size_t CurrentLiveBytes = 0;
	std::size_t PeakLiveBytes = 0;
	std::size_t CurrentLiveBlocks = 0;
	std::size_t PeakLiveBlocks = 0;
};

struct MemoryTrackerReport
{
	MemoryTrackerStatistics Statistics;
	std::vector<TraceRecord> Timeline;
	std::vector<MemoryIssueRecord> Issues;
	std::vector<LiveAllocationRecord> LiveAllocations;
};

class IMemoryTrackerReportBackend
{
public:
	virtual ~IMemoryTrackerReportBackend() = default;

	[[nodiscard]] virtual bool write(
		const MemoryTrackerReport& Report,
		const std::filesystem::path& Path) const = 0;
};

class JsonMemoryTrackerReportBackend final : public IMemoryTrackerReportBackend
{
public:
	[[nodiscard]] bool write(
		const MemoryTrackerReport& Report,
		const std::filesystem::path& Path) const override
	{
		std::ofstream Output(Path, std::ios::binary | std::ios::trunc);
		if (!Output.is_open())
		{
			return false;
		}

		Output << "{\n";
		writeStatistics(Report.Statistics, Output);
		Output << ",\n";
		writeIssues(Report.Issues, Output);
		Output << ",\n";
		writeLiveAllocations(Report.LiveAllocations, Output);
		Output << ",\n";
		writeTimeline(Report.Timeline, Output);
		Output << "\n}\n";
		return Output.good();
	}

private:
	[[nodiscard]] static std::string escapeJson(const std::string& InText)
	{
		std::string Escaped;
		Escaped.reserve(InText.size() + 16);
		for (char Character : InText)
		{
			switch (Character)
			{
			case '"':
				Escaped += "\\\"";
				break;
			case '\\':
				Escaped += "\\\\";
				break;
			case '\b':
				Escaped += "\\b";
				break;
			case '\f':
				Escaped += "\\f";
				break;
			case '\n':
				Escaped += "\\n";
				break;
			case '\r':
				Escaped += "\\r";
				break;
			case '\t':
				Escaped += "\\t";
				break;
			default:
				Escaped += Character;
				break;
			}
		}
		return Escaped;
	}

	[[nodiscard]] static std::string pointerToString(const std::uintptr_t Address)
	{
		std::ostringstream Stream;
		Stream << "0x" << std::hex << std::uppercase << Address;
		return Stream.str();
	}

	[[nodiscard]] static const char* hookOpName(const HookAllocationOp Op)
	{
		switch (Op)
		{
		case HookAllocationOp::New:
			return "new";
		case HookAllocationOp::Delete:
			return "delete";
		case HookAllocationOp::NewArray:
			return "new[]";
		case HookAllocationOp::DeleteArray:
			return "delete[]";
		case HookAllocationOp::Malloc:
			return "malloc";
		case HookAllocationOp::Free:
			return "free";
		case HookAllocationOp::Calloc:
			return "calloc";
		case HookAllocationOp::Realloc:
			return "realloc";
		case HookAllocationOp::AlignedNew:
			return "aligned_new";
		case HookAllocationOp::AlignedDelete:
			return "aligned_delete";
		default:
			return "unknown";
		}
	}

	[[nodiscard]] static const char* issueTypeName(const MemoryIssueType Type)
	{
		switch (Type)
		{
		case MemoryIssueType::DoubleAllocation:
			return "double_allocation";
		case MemoryIssueType::DoubleFree:
			return "double_free";
		case MemoryIssueType::InvalidFree:
			return "invalid_free";
		case MemoryIssueType::MismatchedFreeFunction:
			return "mismatched_free_function";
		case MemoryIssueType::SizeMismatch:
			return "size_mismatch";
		case MemoryIssueType::AlignmentMismatch:
			return "alignment_mismatch";
		case MemoryIssueType::GuardCorruption:
			return "guard_corruption";
		case MemoryIssueType::UseAfterFree:
			return "use_after_free";
		case MemoryIssueType::ValidationError:
			return "validation_error";
		case MemoryIssueType::AllocationFailure:
			return "allocation_failure";
		case MemoryIssueType::ReallocFailure:
			return "realloc_failure";
		default:
			return "unknown";
		}
	}

	static void writeStatistics(const MemoryTrackerStatistics& Stats, std::ofstream& Output)
	{
		Output << "  \"statistics\": {\n";
		Output << "    \"engineEventCount\": " << Stats.EngineEventCount << ",\n";
		Output << "    \"hookEventCount\": " << Stats.HookEventCount << ",\n";
		Output << "    \"allocateCount\": " << Stats.AllocateCount << ",\n";
		Output << "    \"deallocateCount\": " << Stats.DeallocateCount << ",\n";
		Output << "    \"threadCacheHitCount\": " << Stats.ThreadCacheHitCount << ",\n";
		Output << "    \"threadCacheMissCount\": " << Stats.ThreadCacheMissCount << ",\n";
		Output << "    \"centralFetchCount\": " << Stats.CentralFetchCount << ",\n";
		Output << "    \"centralReturnCount\": " << Stats.CentralReturnCount << ",\n";
		Output << "    \"osAllocateCount\": " << Stats.OsAllocateCount << ",\n";
		Output << "    \"osReleaseCount\": " << Stats.OsReleaseCount << ",\n";
		Output << "    \"guardCorruptionCount\": " << Stats.GuardCorruptionCount << ",\n";
		Output << "    \"useAfterFreeCount\": " << Stats.UseAfterFreeCount << ",\n";
		Output << "    \"validationErrorCount\": " << Stats.ValidationErrorCount << ",\n";
		Output << "    \"hookAllocationCount\": " << Stats.HookAllocationCount << ",\n";
		Output << "    \"hookFreeCount\": " << Stats.HookFreeCount << ",\n";
		Output << "    \"hookReallocCount\": " << Stats.HookReallocCount << ",\n";
		Output << "    \"hookNullFreeCount\": " << Stats.HookNullFreeCount << ",\n";
		Output << "    \"hookAllocationFailureCount\": " << Stats.HookAllocationFailureCount << ",\n";
		Output << "    \"doubleAllocationCount\": " << Stats.DoubleAllocationCount << ",\n";
		Output << "    \"doubleFreeCount\": " << Stats.DoubleFreeCount << ",\n";
		Output << "    \"invalidFreeCount\": " << Stats.InvalidFreeCount << ",\n";
		Output << "    \"mismatchedFreeFunctionCount\": " << Stats.MismatchedFreeFunctionCount << ",\n";
		Output << "    \"sizeMismatchCount\": " << Stats.SizeMismatchCount << ",\n";
		Output << "    \"alignmentMismatchCount\": " << Stats.AlignmentMismatchCount << ",\n";
		Output << "    \"issueCount\": " << Stats.IssueCount << ",\n";
		Output << "    \"totalAllocatedBytes\": " << Stats.TotalAllocatedBytes << ",\n";
		Output << "    \"totalFreedBytes\": " << Stats.TotalFreedBytes << ",\n";
		Output << "    \"currentLiveBytes\": " << Stats.CurrentLiveBytes << ",\n";
		Output << "    \"peakLiveBytes\": " << Stats.PeakLiveBytes << ",\n";
		Output << "    \"currentLiveBlocks\": " << Stats.CurrentLiveBlocks << ",\n";
		Output << "    \"peakLiveBlocks\": " << Stats.PeakLiveBlocks << "\n";
		Output << "  }";
	}

	static void writeIssues(const std::vector<MemoryIssueRecord>& Issues, std::ofstream& Output)
	{
		Output << "  \"issues\": [\n";
		for (std::size_t Index = 0; Index < Issues.size(); ++Index)
		{
			const MemoryIssueRecord& Issue = Issues[Index];
			Output << "    {\"type\":\"" << issueTypeName(Issue.Type)
				<< "\",\"operation\":\"" << hookOpName(Issue.Operation)
				<< "\",\"address\":\"" << pointerToString(Issue.Address)
				<< "\",\"size\":" << Issue.Size
				<< ",\"alignment\":" << Issue.Alignment
				<< ",\"timestampNs\":" << Issue.TimestampNs
				<< ",\"threadId\":" << Issue.ThreadId
				<< ",\"message\":\"" << escapeJson(Issue.Message)
				<< "\",\"payload\":{\"capturedBytes\":" << Issue.Payload.CapturedBytes
				<< ",\"originalBytes\":" << Issue.Payload.OriginalBytes
				<< ",\"hex\":\"" << escapeJson(Issue.Payload.Hex) << "\"}}";
			if (Index + 1 < Issues.size())
			{
				Output << ",";
			}
			Output << "\n";
		}
		Output << "  ]";
	}

	static void writeLiveAllocations(
		const std::vector<LiveAllocationRecord>& LiveAllocations,
		std::ofstream& Output)
	{
		Output << "  \"liveAllocations\": [\n";
		for (std::size_t Index = 0; Index < LiveAllocations.size(); ++Index)
		{
			const LiveAllocationRecord& Record = LiveAllocations[Index];
			Output << "    {\"operation\":\"" << hookOpName(Record.Operation)
				<< "\",\"address\":\"" << pointerToString(Record.Address)
				<< "\",\"size\":" << Record.Size
				<< ",\"alignment\":" << Record.Alignment
				<< ",\"allocationId\":" << Record.AllocationId
				<< ",\"timestampNs\":" << Record.TimestampNs
				<< ",\"threadId\":" << Record.ThreadId
				<< ",\"frameIndex\":" << Record.FrameIndex
				<< ",\"resourceId\":" << Record.ResourceId
				<< ",\"fromEngineEvent\":" << (Record.FromEngineEvent ? "true" : "false")
				<< ",\"fromThreadCache\":" << (Record.FromThreadCache ? "true" : "false")
				<< ",\"fromCentralPool\":" << (Record.FromCentralPool ? "true" : "false")
				<< ",\"fromOS\":" << (Record.FromOS ? "true" : "false")
				<< ",\"payload\":{\"capturedBytes\":" << Record.LastPayload.CapturedBytes
				<< ",\"originalBytes\":" << Record.LastPayload.OriginalBytes
				<< ",\"hex\":\"" << escapeJson(Record.LastPayload.Hex) << "\"}}";
			if (Index + 1 < LiveAllocations.size())
			{
				Output << ",";
			}
			Output << "\n";
		}
		Output << "  ]";
	}

	static void writeTimeline(const std::vector<TraceRecord>& Timeline, std::ofstream& Output)
	{
		Output << "  \"timeline\": [\n";
		for (std::size_t Index = 0; Index < Timeline.size(); ++Index)
		{
			const TraceRecord& Record = Timeline[Index];
			Output << "    {\"name\":\"" << escapeJson(Record.Name)
				<< "\",\"source\":\"" << escapeJson(Record.Source)
				<< "\",\"timestampUs\":" << Record.TimestampUs
				<< ",\"threadId\":" << Record.ThreadId
				<< ",\"address\":\"" << pointerToString(Record.Address)
				<< "\",\"size\":" << Record.Size
				<< ",\"alignment\":" << Record.Alignment
				<< ",\"allocationId\":" << Record.AllocationId
				<< ",\"resourceId\":" << Record.ResourceId << "}";
			if (Index + 1 < Timeline.size())
			{
				Output << ",";
			}
			Output << "\n";
		}
		Output << "  ]";
	}
};

class MemoryTracker
{
public:
	/**
	 * @brief Convert memory events into tracker records and statistics.
	 * @param
	 *     - Event  const MemoryEvent&  Event emitted by allocator pipeline.
	 * @usage
	 *     - Bind one tracker instance to AllocatorFacade event bus.
	 * @return
	 *     - void
	 */
	void onMemoryEvent(const MemoryEvent& Event)
	{
		global().recordEngineEvent(Event);
	}

	/**
	 * @brief Notify tracker about an allocation captured by hook wrappers.
	 * @param
	 *     - Operation  HookAllocationOp  Hook operation kind.
	 *     - Ptr        void*             Returned allocation pointer.
	 *     - Size       std::size_t       Requested size.
	 *     - Alignment  std::size_t       Requested alignment or unknown marker.
	 *     - Caller     void*             Caller return address if available.
	 * @usage
	 *     - Called by new/malloc hook wrappers.
	 * @return
	 *     - void
	 */
	static void onHookAllocation(
		const HookAllocationOp Operation,
		void* Ptr,
		const std::size_t Size,
		const std::size_t Alignment,
		void* Caller)
	{
		global().recordHookAllocation(Operation, Ptr, Size, Alignment, Caller);
	}

	/**
	 * @brief Notify tracker about a deallocation captured by hook wrappers.
	 * @param
	 *     - Operation  HookAllocationOp  Hook operation kind.
	 *     - Ptr        void*             Pointer passed to free/delete.
	 *     - Size       std::size_t       Optional size (or unknown marker).
	 *     - Alignment  std::size_t       Optional alignment (or unknown marker).
	 *     - Caller     void*             Caller return address if available.
	 * @usage
	 *     - Called by new/malloc hook wrappers before real free.
	 * @return
	 *     - void
	 */
	static void onHookFree(
		const HookAllocationOp Operation,
		void* Ptr,
		const std::size_t Size,
		const std::size_t Alignment,
		void* Caller)
	{
		global().recordHookFree(Operation, Ptr, Size, Alignment, Caller);
	}

	/**
	 * @brief Notify tracker about realloc behavior captured by hook wrappers.
	 * @param
	 *     - OldPtr     void*        Old pointer passed to realloc.
	 *     - NewPtr     void*        Returned pointer from realloc.
	 *     - NewSize    std::size_t  Requested new size.
	 *     - Caller     void*        Caller return address if available.
	 * @usage
	 *     - Called by realloc hook wrapper after real realloc returns.
	 * @return
	 *     - void
	 */
	static void onHookReallocate(
		void* OldPtr,
		void* NewPtr,
		const std::size_t NewSize,
		void* Caller)
	{
		global().recordHookReallocate(OldPtr, NewPtr, NewSize, Caller);
	}

	/**
	 * @brief Notify tracker about allocation failure captured by hook wrappers.
	 * @param
	 *     - Operation  HookAllocationOp  Hook operation kind.
	 *     - Size       std::size_t       Requested size.
	 *     - Alignment  std::size_t       Requested alignment or unknown marker.
	 *     - Caller     void*             Caller return address if available.
	 * @usage
	 *     - Called when hook wrapper receives null from allocator function.
	 * @return
	 *     - void
	 */
	static void onHookAllocationFailure(
		const HookAllocationOp Operation,
		const std::size_t Size,
		const std::size_t Alignment,
		void* Caller)
	{
		global().recordHookAllocationFailure(Operation, Size, Alignment, Caller);
	}

	[[nodiscard]] static MemoryTracker& global()
	{
		struct StorageBlock
		{
			alignas(MemoryTracker) std::byte Data[sizeof(MemoryTracker)];
		};

		static StorageBlock Storage;
		static MemoryTracker* Instance = []() -> MemoryTracker* {
			struct BootstrapSuppressionScope
			{
				BootstrapSuppressionScope()
				{
					++hookSuppressionDepth();
				}

				~BootstrapSuppressionScope()
				{
					--hookSuppressionDepth();
				}
			} Scope;

			(void)Scope;
			return new (Storage.Data) MemoryTracker();
		}();
		return *Instance;
	}

	static void setHookEnabled(const bool Enabled)
	{
		global().HookEnabled.store(Enabled, std::memory_order_release);
	}

	[[nodiscard]] static bool isHookEnabled()
	{
		return global().HookEnabled.load(std::memory_order_acquire);
	}

	[[nodiscard]] static bool isTrackingSuppressedForCurrentThread() noexcept
	{
		return hookSuppressionDepth() > 0;
	}

	static void setPayloadPreviewBytes(const std::size_t Bytes)
	{
		std::lock_guard Lock(global().Mutex);
		global().PayloadPreviewBytes = Bytes;
	}

	[[nodiscard]] static std::size_t getPayloadPreviewBytes()
	{
		std::lock_guard Lock(global().Mutex);
		return global().PayloadPreviewBytes;
	}

	static void setFreedHistoryLimit(const std::size_t Limit)
	{
		std::lock_guard Lock(global().Mutex);
		global().FreedHistoryLimit = std::max<std::size_t>(1, Limit);
		global().trimFreedHistoryLocked();
	}

	[[nodiscard]] static std::size_t getFreedHistoryLimit()
	{
		std::lock_guard Lock(global().Mutex);
		return global().FreedHistoryLimit;
	}

	/**
	 * @brief Flush the full tracker report by using JSON backend.
	 * @param
	 *     - InPath  const std::filesystem::path&  Output path.
	 * @usage
	 *     - Use when a persistent report file is required after test/runtime.
	 * @return
	 *     - bool  true when report is written successfully.
	 */
	[[nodiscard]] bool flushToFile(const std::filesystem::path& InPath) const
	{
		JsonMemoryTrackerReportBackend Backend;
		return flushReport(InPath, Backend);
	}

	/**
	 * @brief Flush tracker report through custom report backend.
	 * @param
	 *     - InPath    const std::filesystem::path&  Output path.
	 *     - Backend   const IMemoryTrackerReportBackend&  Backend implementation.
	 * @usage
	 *     - Use to extend report format (json/yml/etc.) while sharing core snapshot.
	 * @return
	 *     - bool  true when backend successfully writes report.
	 */
	[[nodiscard]] bool flushReport(
		const std::filesystem::path& InPath,
		const IMemoryTrackerReportBackend& Backend) const
	{
		return Backend.write(getReport(), InPath);
	}

	/**
	 * @brief Get immutable report snapshot.
	 * @param
	 *     - none
	 * @usage
	 *     - Read detailed statistics, issues and live allocations in-memory.
	 * @return
	 *     - MemoryTrackerReport  Snapshot copy.
	 */
	[[nodiscard]] MemoryTrackerReport getReport() const
	{
		HookSuppressionScope Scope;
		std::lock_guard Lock(global().Mutex);
		MemoryTrackerReport Report;
		Report.Statistics = global().Statistics;
		Report.Timeline = global().Timeline;
		Report.Issues = global().Issues;
		Report.LiveAllocations = global().buildLiveAllocationSnapshotLocked();
		return Report;
	}

	[[nodiscard]] MemoryTrackerStatistics getStatistics() const
	{
		HookSuppressionScope Scope;
		std::lock_guard Lock(global().Mutex);
		return global().Statistics;
	}

	/**
	 * @brief Query current buffered event count.
	 * @param
	 *     - none
	 * @usage
	 *     - Keep compatibility with existing tests checking tracker activity.
	 * @return
	 *     - std::size_t  Number of timeline records.
	 */
	[[nodiscard]] std::size_t getEventCount() const
	{
		HookSuppressionScope Scope;
		std::lock_guard Lock(global().Mutex);
		return global().Timeline.size();
	}

	void clear()
	{
		HookSuppressionScope Scope;
		std::lock_guard Lock(global().Mutex);
		global().Timeline.clear();
		global().Issues.clear();
		global().ActiveAllocations.clear();
		global().FreedAllocations.clear();
		global().FreedOrder.clear();
		global().Statistics = {};
		global().NextFreedSequence = 0;
	}

private:
	struct ActiveAllocation
	{
		HookAllocationOp Operation = HookAllocationOp::Unknown;
		std::size_t Size = 0;
		std::size_t Alignment = MemoryTrackerUnknownSize;
		std::uint64_t AllocationId = 0;
		std::uint64_t TimestampNs = 0;
		std::uint32_t ThreadId = 0;
		std::uint32_t FrameIndex = 0;
		std::uint32_t ResourceId = 0;
		bool FromEngineEvent = false;
		bool FromThreadCache = false;
		bool FromCentralPool = false;
		bool FromOS = false;
		MemoryPayloadPreview LastPayload;
	};

	struct FreedAllocation
	{
		std::uint64_t Sequence = 0;
		HookAllocationOp FreedBy = HookAllocationOp::Unknown;
		std::size_t Size = 0;
		std::size_t Alignment = MemoryTrackerUnknownSize;
		std::uint64_t TimestampNs = 0;
		std::uint32_t ThreadId = 0;
		MemoryPayloadPreview Payload;
	};

	struct FreedOrderEntry
	{
		void* Ptr = nullptr;
		std::uint64_t Sequence = 0;
	};

	[[nodiscard]] static int& hookSuppressionDepth() noexcept
	{
		static thread_local int Depth = 0;
		return Depth;
	}

	class HookSuppressionScope
	{
	public:
		HookSuppressionScope()
		{
			++hookSuppressionDepth();
		}

		~HookSuppressionScope()
		{
			--hookSuppressionDepth();
		}
	};

	MemoryTracker() = default;

	static void updatePeak(std::size_t& Peak, const std::size_t Current)
	{
		if (Current > Peak)
		{
			Peak = Current;
		}
	}

	[[nodiscard]] static std::uint32_t threadIdFrom(const std::thread::id& Id)
	{
		return static_cast<std::uint32_t>(std::hash<std::thread::id>{}(Id));
	}

	[[nodiscard]] static std::uint32_t currentThreadId()
	{
		return threadIdFrom(std::this_thread::get_id());
	}

	[[nodiscard]] static const char* hookOpName(const HookAllocationOp Op)
	{
		switch (Op)
		{
		case HookAllocationOp::New:
			return "new";
		case HookAllocationOp::Delete:
			return "delete";
		case HookAllocationOp::NewArray:
			return "new[]";
		case HookAllocationOp::DeleteArray:
			return "delete[]";
		case HookAllocationOp::Malloc:
			return "malloc";
		case HookAllocationOp::Free:
			return "free";
		case HookAllocationOp::Calloc:
			return "calloc";
		case HookAllocationOp::Realloc:
			return "realloc";
		case HookAllocationOp::AlignedNew:
			return "aligned_new";
		case HookAllocationOp::AlignedDelete:
			return "aligned_delete";
		default:
			return "unknown";
		}
	}

	[[nodiscard]] static HookAllocationOp expectedFreeOperation(const HookAllocationOp Operation)
	{
		switch (Operation)
		{
		case HookAllocationOp::New:
			return HookAllocationOp::Delete;
		case HookAllocationOp::NewArray:
			return HookAllocationOp::DeleteArray;
		case HookAllocationOp::AlignedNew:
			return HookAllocationOp::AlignedDelete;
		case HookAllocationOp::Malloc:
		case HookAllocationOp::Calloc:
		case HookAllocationOp::Realloc:
			return HookAllocationOp::Free;
		default:
			return HookAllocationOp::Unknown;
		}
	}

	[[nodiscard]] static const char* eventTypeName(const MemoryEventType Type)
	{
		switch (Type)
		{
		case MemoryEventType::Allocate:
			return "Allocate";
		case MemoryEventType::Deallocate:
			return "Deallocate";
		case MemoryEventType::ThreadCacheHit:
			return "ThreadCacheHit";
		case MemoryEventType::ThreadCacheMiss:
			return "ThreadCacheMiss";
		case MemoryEventType::CentralFetch:
			return "CentralFetch";
		case MemoryEventType::CentralReturn:
			return "CentralReturn";
		case MemoryEventType::GuardCorruption:
			return "GuardCorruption";
		case MemoryEventType::UseAfterFree:
			return "UseAfterFree";
		case MemoryEventType::ValidationError:
			return "ValidationError";
		case MemoryEventType::Reserve:
			return "Reserve";
		case MemoryEventType::Commit:
			return "Commit";
		case MemoryEventType::Decommit:
			return "Decommit";
		case MemoryEventType::Release:
			return "Release";
		case MemoryEventType::PageAllocate:
			return "PageAllocate";
		case MemoryEventType::PageFree:
			return "PageFree";
		case MemoryEventType::Reallocate:
			return "Reallocate";
		default:
			return "Unknown";
		}
	}

	[[nodiscard]] MemoryPayloadPreview capturePayloadPreview(const void* Ptr, const std::size_t Size) const
	{
		MemoryPayloadPreview Preview;
		Preview.OriginalBytes = Size;
		if (!Ptr || Size == 0 || PayloadPreviewBytes == 0)
		{
			return Preview;
		}

		const std::size_t CaptureBytes = std::min(Size, PayloadPreviewBytes);
		Preview.CapturedBytes = CaptureBytes;
		const auto* Bytes = static_cast<const std::uint8_t*>(Ptr);

		std::ostringstream Stream;
		Stream << std::hex << std::setfill('0');
		for (std::size_t Index = 0; Index < CaptureBytes; ++Index)
		{
			Stream << std::setw(2) << static_cast<unsigned>(Bytes[Index]);
			if (Index + 1 < CaptureBytes)
			{
				Stream << ' ';
			}
		}

		Preview.Hex = Stream.str();
		return Preview;
	}

	void appendTimelineLocked(
		const std::string& Name,
		const std::string& Source,
		const std::uint64_t TimestampNs,
		const std::uint32_t ThreadId,
		const std::uintptr_t Address,
		const std::size_t Size,
		const std::size_t Alignment,
		const std::uint64_t AllocationId,
		const std::uint32_t ResourceId)
	{
		TraceRecord Record;
		Record.Name = Name;
		Record.Source = Source;
		Record.TimestampUs = TimestampNs / 1000;
		Record.ThreadId = ThreadId;
		Record.Address = Address;
		Record.Size = Size;
		Record.Alignment = Alignment;
		Record.AllocationId = AllocationId;
		Record.ResourceId = ResourceId;
		Timeline.push_back(std::move(Record));
	}

	void addIssueLocked(
		const MemoryIssueType Type,
		const HookAllocationOp Operation,
		const std::uintptr_t Address,
		const std::size_t Size,
		const std::size_t Alignment,
		const std::string& Message,
		const MemoryPayloadPreview& Payload,
		const std::uint64_t TimestampNs,
		const std::uint32_t ThreadId)
	{
		MemoryIssueRecord Issue;
		Issue.Type = Type;
		Issue.Operation = Operation;
		Issue.Address = Address;
		Issue.Size = Size;
		Issue.Alignment = Alignment;
		Issue.TimestampNs = TimestampNs;
		Issue.ThreadId = ThreadId;
		Issue.Message = Message;
		Issue.Payload = Payload;
		Issues.push_back(std::move(Issue));
		++Statistics.IssueCount;
	}

	void rememberFreedAllocationLocked(void* Ptr, const FreedAllocation& Record)
	{
		FreedAllocations[Ptr] = Record;
		FreedOrder.push_back(FreedOrderEntry{Ptr, Record.Sequence});
		trimFreedHistoryLocked();
	}

	void trimFreedHistoryLocked()
	{
		while (FreedOrder.size() > FreedHistoryLimit)
		{
			const FreedOrderEntry Entry = FreedOrder.front();
			FreedOrder.pop_front();

			auto Iter = FreedAllocations.find(Entry.Ptr);
			if (Iter != FreedAllocations.end() && Iter->second.Sequence == Entry.Sequence)
			{
				FreedAllocations.erase(Iter);
			}
		}
	}

	[[nodiscard]] std::vector<LiveAllocationRecord> buildLiveAllocationSnapshotLocked() const
	{
		std::vector<LiveAllocationRecord> Snapshot;
		Snapshot.reserve(ActiveAllocations.size());

		for (const auto& [Ptr, Allocation] : ActiveAllocations)
		{
			LiveAllocationRecord Record;
			Record.Operation = Allocation.Operation;
			Record.Address = reinterpret_cast<std::uintptr_t>(Ptr);
			Record.Size = Allocation.Size;
			Record.Alignment = Allocation.Alignment;
			Record.AllocationId = Allocation.AllocationId;
			Record.TimestampNs = Allocation.TimestampNs;
			Record.ThreadId = Allocation.ThreadId;
			Record.FrameIndex = Allocation.FrameIndex;
			Record.ResourceId = Allocation.ResourceId;
			Record.FromEngineEvent = Allocation.FromEngineEvent;
			Record.FromThreadCache = Allocation.FromThreadCache;
			Record.FromCentralPool = Allocation.FromCentralPool;
			Record.FromOS = Allocation.FromOS;
			Record.LastPayload = Allocation.LastPayload;
			Snapshot.push_back(std::move(Record));
		}

		return Snapshot;
	}

	void updateHookOperationCountersLocked(const HookAllocationOp Operation)
	{
		switch (Operation)
		{
		case HookAllocationOp::New:
			++Statistics.HookNewCount;
			break;
		case HookAllocationOp::Delete:
			++Statistics.HookDeleteCount;
			break;
		case HookAllocationOp::NewArray:
			++Statistics.HookNewArrayCount;
			break;
		case HookAllocationOp::DeleteArray:
			++Statistics.HookDeleteArrayCount;
			break;
		case HookAllocationOp::Malloc:
			++Statistics.HookMallocCount;
			break;
		case HookAllocationOp::Free:
			++Statistics.HookFreeFunctionCount;
			break;
		case HookAllocationOp::Calloc:
			++Statistics.HookCallocCount;
			break;
		case HookAllocationOp::AlignedNew:
			++Statistics.HookAlignedNewCount;
			break;
		case HookAllocationOp::AlignedDelete:
			++Statistics.HookAlignedDeleteCount;
			break;
		default:
			break;
		}
	}

	void recordEngineEvent(const MemoryEvent& Event)
	{
		HookSuppressionScope Scope;
		std::lock_guard Lock(Mutex);
		++Statistics.EngineEventCount;

		appendTimelineLocked(
			eventTypeName(Event.Type),
			"engine",
			Event.Timestamp,
			threadIdFrom(Event.ThreadId),
			reinterpret_cast<std::uintptr_t>(Event.UserPtr),
			Event.Size,
			Event.Alignment,
			Event.AllocationId,
			Event.ResourceId);

		switch (Event.Type)
		{
		case MemoryEventType::Allocate:
		{
			++Statistics.AllocateCount;
			Statistics.TotalAllocatedBytes += Event.Size;
			Statistics.CurrentLiveBytes += Event.Size;
			++Statistics.CurrentLiveBlocks;
			updatePeak(Statistics.PeakLiveBytes, Statistics.CurrentLiveBytes);
			updatePeak(Statistics.PeakLiveBlocks, Statistics.CurrentLiveBlocks);

			if (Event.FromOS)
			{
				++Statistics.OsAllocateCount;
			}

			if (Event.UserPtr)
			{
				ActiveAllocation Allocation;
				Allocation.Operation = HookAllocationOp::Unknown;
				Allocation.Size = Event.Size;
				Allocation.Alignment = Event.Alignment;
				Allocation.AllocationId = Event.AllocationId;
				Allocation.TimestampNs = Event.Timestamp;
				Allocation.ThreadId = threadIdFrom(Event.ThreadId);
				Allocation.FrameIndex = Event.FrameIndex;
				Allocation.ResourceId = Event.ResourceId;
				Allocation.FromEngineEvent = true;
				Allocation.FromThreadCache = Event.FromThreadCache;
				Allocation.FromCentralPool = Event.FromCentralPool;
				Allocation.FromOS = Event.FromOS;
				ActiveAllocations[Event.UserPtr] = std::move(Allocation);
				FreedAllocations.erase(Event.UserPtr);
			}
			break;
		}

		case MemoryEventType::Deallocate:
			++Statistics.DeallocateCount;
			Statistics.TotalFreedBytes += Event.Size;
			Statistics.CurrentLiveBytes = Statistics.CurrentLiveBytes >= Event.Size
				? Statistics.CurrentLiveBytes - Event.Size
				: 0;
			if (Statistics.CurrentLiveBlocks > 0)
			{
				--Statistics.CurrentLiveBlocks;
			}
			if (Event.FromOS)
			{
				++Statistics.OsReleaseCount;
			}
			if (Event.UserPtr)
			{
				ActiveAllocations.erase(Event.UserPtr);
			}
			break;

		case MemoryEventType::ThreadCacheHit:
			++Statistics.ThreadCacheHitCount;
			break;

		case MemoryEventType::ThreadCacheMiss:
			++Statistics.ThreadCacheMissCount;
			break;

		case MemoryEventType::CentralFetch:
			++Statistics.CentralFetchCount;
			break;

		case MemoryEventType::CentralReturn:
			++Statistics.CentralReturnCount;
			break;

		case MemoryEventType::GuardCorruption:
			++Statistics.GuardCorruptionCount;
			addIssueLocked(
				MemoryIssueType::GuardCorruption,
				HookAllocationOp::Unknown,
				reinterpret_cast<std::uintptr_t>(Event.UserPtr),
				Event.Size,
				Event.Alignment,
				"Guard bytes were corrupted before deallocation.",
				{},
				Event.Timestamp,
				threadIdFrom(Event.ThreadId));
			break;

		case MemoryEventType::UseAfterFree:
			++Statistics.UseAfterFreeCount;
			addIssueLocked(
				MemoryIssueType::UseAfterFree,
				HookAllocationOp::Unknown,
				reinterpret_cast<std::uintptr_t>(Event.UserPtr),
				Event.Size,
				Event.Alignment,
				"Use-after-free detected from allocator quarantine validation.",
				{},
				Event.Timestamp,
				threadIdFrom(Event.ThreadId));
			break;

		case MemoryEventType::ValidationError:
			++Statistics.ValidationErrorCount;
			addIssueLocked(
				MemoryIssueType::ValidationError,
				HookAllocationOp::Unknown,
				reinterpret_cast<std::uintptr_t>(Event.UserPtr),
				Event.Size,
				Event.Alignment,
				"Allocator validation failed for request/deallocation metadata.",
				{},
				Event.Timestamp,
				threadIdFrom(Event.ThreadId));
			break;

		default:
			break;
		}
	}

	void recordHookAllocation(
		const HookAllocationOp Operation,
		void* Ptr,
		const std::size_t Size,
		const std::size_t Alignment,
		void* Caller)
	{
		HookSuppressionScope Scope;
		std::lock_guard Lock(Mutex);
		if (!HookEnabled.load(std::memory_order_acquire))
		{
			return;
		}

		const std::uint64_t Now = timestampNs();
		const std::uint32_t Tid = currentThreadId();

		++Statistics.HookEventCount;
		++Statistics.HookAllocationCount;
		updateHookOperationCountersLocked(Operation);

		appendTimelineLocked(
			hookOpName(Operation),
			"hook",
			Now,
			Tid,
			reinterpret_cast<std::uintptr_t>(Ptr),
			Size,
			Alignment,
			0,
			0);

		if (!Ptr)
		{
			++Statistics.HookAllocationFailureCount;
			addIssueLocked(
				MemoryIssueType::AllocationFailure,
				Operation,
				reinterpret_cast<std::uintptr_t>(Caller),
				Size,
				Alignment,
				"Hook allocation returned null pointer.",
				{},
				Now,
				Tid);
			return;
		}

		auto Existing = ActiveAllocations.find(Ptr);
		if (Existing != ActiveAllocations.end())
		{
			++Statistics.DoubleAllocationCount;
			addIssueLocked(
				MemoryIssueType::DoubleAllocation,
				Operation,
				reinterpret_cast<std::uintptr_t>(Ptr),
				Size,
				Alignment,
				"Pointer was allocated again before being released.",
				{},
				Now,
				Tid);

			Statistics.CurrentLiveBytes = Statistics.CurrentLiveBytes >= Existing->second.Size
				? Statistics.CurrentLiveBytes - Existing->second.Size
				: 0;
		}
		else
		{
			++Statistics.CurrentLiveBlocks;
			updatePeak(Statistics.PeakLiveBlocks, Statistics.CurrentLiveBlocks);
		}

		Statistics.TotalAllocatedBytes += Size;
		Statistics.CurrentLiveBytes += Size;
		updatePeak(Statistics.PeakLiveBytes, Statistics.CurrentLiveBytes);

		ActiveAllocation Allocation;
		Allocation.Operation = Operation;
		Allocation.Size = Size;
		Allocation.Alignment = Alignment;
		Allocation.AllocationId = 0;
		Allocation.TimestampNs = Now;
		Allocation.ThreadId = Tid;
		Allocation.FrameIndex = 0;
		Allocation.ResourceId = 0;
		Allocation.FromEngineEvent = false;
		ActiveAllocations[Ptr] = std::move(Allocation);
		FreedAllocations.erase(Ptr);
		(void)Caller;
	}

	void recordHookFree(
		const HookAllocationOp Operation,
		void* Ptr,
		const std::size_t Size,
		const std::size_t Alignment,
		void* Caller)
	{
		HookSuppressionScope Scope;
		std::lock_guard Lock(Mutex);
		if (!HookEnabled.load(std::memory_order_acquire))
		{
			return;
		}

		const std::uint64_t Now = timestampNs();
		const std::uint32_t Tid = currentThreadId();

		++Statistics.HookEventCount;
		++Statistics.HookFreeCount;
		updateHookOperationCountersLocked(Operation);

		appendTimelineLocked(
			hookOpName(Operation),
			"hook",
			Now,
			Tid,
			reinterpret_cast<std::uintptr_t>(Ptr),
			Size,
			Alignment,
			0,
			0);

		recordHookFreeNoLock(Operation, Ptr, Size, Alignment, Caller, Now, Tid);
	}

	void recordHookReallocate(void* OldPtr, void* NewPtr, const std::size_t NewSize, void* Caller)
	{
		HookSuppressionScope Scope;
		std::lock_guard Lock(Mutex);
		if (!HookEnabled.load(std::memory_order_acquire))
		{
			return;
		}

		const std::uint64_t Now = timestampNs();
		const std::uint32_t Tid = currentThreadId();

		++Statistics.HookEventCount;
		++Statistics.HookReallocCount;
		updateHookOperationCountersLocked(HookAllocationOp::Realloc);

		appendTimelineLocked(
			hookOpName(HookAllocationOp::Realloc),
			"hook",
			Now,
			Tid,
			reinterpret_cast<std::uintptr_t>(NewPtr),
			NewSize,
			MemoryTrackerUnknownSize,
			0,
			0);

		if (OldPtr == nullptr)
		{
			if (NewPtr)
			{
				recordHookAllocationNoLock(HookAllocationOp::Realloc, NewPtr, NewSize, MemoryTrackerUnknownSize);
			}
			else
			{
				++Statistics.HookAllocationFailureCount;
				addIssueLocked(
					MemoryIssueType::ReallocFailure,
					HookAllocationOp::Realloc,
					reinterpret_cast<std::uintptr_t>(Caller),
					NewSize,
					MemoryTrackerUnknownSize,
					"realloc(nullptr, size) failed to allocate memory.",
					{},
					Now,
					Tid);
			}
			return;
		}

		if (NewPtr == nullptr)
		{
			++Statistics.HookAllocationFailureCount;
			addIssueLocked(
				MemoryIssueType::ReallocFailure,
				HookAllocationOp::Realloc,
				reinterpret_cast<std::uintptr_t>(OldPtr),
				NewSize,
				MemoryTrackerUnknownSize,
				"realloc failed and old allocation remains active.",
				{},
				Now,
				Tid);
			return;
		}

		if (OldPtr == NewPtr)
		{
			auto Iter = ActiveAllocations.find(OldPtr);
			if (Iter == ActiveAllocations.end())
			{
				++Statistics.InvalidFreeCount;
				addIssueLocked(
					MemoryIssueType::InvalidFree,
					HookAllocationOp::Realloc,
					reinterpret_cast<std::uintptr_t>(OldPtr),
					NewSize,
					MemoryTrackerUnknownSize,
					"realloc in-place was called on unknown pointer.",
					{},
					Now,
					Tid);
				return;
			}

			const std::size_t OldSize = Iter->second.Size;
			Iter->second.Operation = HookAllocationOp::Realloc;
			Iter->second.Size = NewSize;
			Iter->second.TimestampNs = Now;
			Iter->second.ThreadId = Tid;

			if (NewSize >= OldSize)
			{
				const std::size_t Delta = NewSize - OldSize;
				Statistics.TotalAllocatedBytes += Delta;
				Statistics.CurrentLiveBytes += Delta;
				updatePeak(Statistics.PeakLiveBytes, Statistics.CurrentLiveBytes);
			}
			else
			{
				const std::size_t Delta = OldSize - NewSize;
				Statistics.TotalFreedBytes += Delta;
				Statistics.CurrentLiveBytes = Statistics.CurrentLiveBytes >= Delta
					? Statistics.CurrentLiveBytes - Delta
					: 0;
			}

			return;
		}

		recordHookFreeNoLock(
			HookAllocationOp::Free,
			OldPtr,
			MemoryTrackerUnknownSize,
			MemoryTrackerUnknownSize,
			Caller,
			Now,
			Tid);
		recordHookAllocationNoLock(HookAllocationOp::Realloc, NewPtr, NewSize, MemoryTrackerUnknownSize);
	}

	void recordHookAllocationFailure(
		const HookAllocationOp Operation,
		const std::size_t Size,
		const std::size_t Alignment,
		void* Caller)
	{
		HookSuppressionScope Scope;
		std::lock_guard Lock(Mutex);
		if (!HookEnabled.load(std::memory_order_acquire))
		{
			return;
		}

		const std::uint64_t Now = timestampNs();
		const std::uint32_t Tid = currentThreadId();
		++Statistics.HookEventCount;
		++Statistics.HookAllocationFailureCount;
		appendTimelineLocked(
			hookOpName(Operation),
			"hook",
			Now,
			Tid,
			reinterpret_cast<std::uintptr_t>(Caller),
			Size,
			Alignment,
			0,
			0);
		addIssueLocked(
			MemoryIssueType::AllocationFailure,
			Operation,
			reinterpret_cast<std::uintptr_t>(Caller),
			Size,
			Alignment,
			"Hook allocation reported failure.",
			{},
			Now,
			Tid);
	}

	void recordHookAllocationNoLock(
		const HookAllocationOp Operation,
		void* Ptr,
		const std::size_t Size,
		const std::size_t Alignment)
	{
		if (!Ptr)
		{
			++Statistics.HookAllocationFailureCount;
			return;
		}

		auto Existing = ActiveAllocations.find(Ptr);
		if (Existing != ActiveAllocations.end())
		{
			++Statistics.DoubleAllocationCount;
			addIssueLocked(
				MemoryIssueType::DoubleAllocation,
				Operation,
				reinterpret_cast<std::uintptr_t>(Ptr),
				Size,
				Alignment,
				"Pointer was allocated again before being released.",
				{},
				timestampNs(),
				currentThreadId());
			Statistics.CurrentLiveBytes = Statistics.CurrentLiveBytes >= Existing->second.Size
				? Statistics.CurrentLiveBytes - Existing->second.Size
				: 0;
		}
		else
		{
			++Statistics.CurrentLiveBlocks;
			updatePeak(Statistics.PeakLiveBlocks, Statistics.CurrentLiveBlocks);
		}

		Statistics.TotalAllocatedBytes += Size;
		Statistics.CurrentLiveBytes += Size;
		updatePeak(Statistics.PeakLiveBytes, Statistics.CurrentLiveBytes);

		ActiveAllocation Allocation;
		Allocation.Operation = Operation;
		Allocation.Size = Size;
		Allocation.Alignment = Alignment;
		Allocation.AllocationId = 0;
		Allocation.TimestampNs = timestampNs();
		Allocation.ThreadId = currentThreadId();
		Allocation.FromEngineEvent = false;
		ActiveAllocations[Ptr] = std::move(Allocation);
		FreedAllocations.erase(Ptr);
	}

	void recordHookFreeNoLock(
		const HookAllocationOp Operation,
		void* Ptr,
		const std::size_t Size,
		const std::size_t Alignment,
		void* Caller,
		const std::uint64_t TimestampNs,
		const std::uint32_t ThreadId)
	{
		if (!Ptr)
		{
			++Statistics.HookNullFreeCount;
			return;
		}

		auto ActiveIter = ActiveAllocations.find(Ptr);
		if (ActiveIter == ActiveAllocations.end())
		{
			auto FreedIter = FreedAllocations.find(Ptr);
			if (FreedIter != FreedAllocations.end())
			{
				++Statistics.DoubleFreeCount;
				addIssueLocked(
					MemoryIssueType::DoubleFree,
					Operation,
					reinterpret_cast<std::uintptr_t>(Ptr),
					FreedIter->second.Size,
					FreedIter->second.Alignment,
					"Double free detected on a previously released pointer.",
					FreedIter->second.Payload,
					TimestampNs,
					ThreadId);
			}
			else
			{
				++Statistics.InvalidFreeCount;
				addIssueLocked(
					MemoryIssueType::InvalidFree,
					Operation,
					reinterpret_cast<std::uintptr_t>(Ptr),
					Size,
					Alignment,
					"Free/delete called for an unknown pointer.",
					{},
					TimestampNs,
					ThreadId);
			}
			return;
		}

		const ActiveAllocation Active = ActiveIter->second;
		const HookAllocationOp Expected = expectedFreeOperation(Active.Operation);
		if (Expected != HookAllocationOp::Unknown && Expected != Operation)
		{
			++Statistics.MismatchedFreeFunctionCount;
			addIssueLocked(
				MemoryIssueType::MismatchedFreeFunction,
				Operation,
				reinterpret_cast<std::uintptr_t>(Ptr),
				Active.Size,
				Active.Alignment,
				"Mismatched deallocation function for pointer.",
				{},
				TimestampNs,
				ThreadId);
		}

		if (Size != MemoryTrackerUnknownSize && Active.Size != 0 && Size != Active.Size)
		{
			++Statistics.SizeMismatchCount;
			addIssueLocked(
				MemoryIssueType::SizeMismatch,
				Operation,
				reinterpret_cast<std::uintptr_t>(Ptr),
				Size,
				Active.Alignment,
				"Sized delete/free size does not match allocation size.",
				{},
				TimestampNs,
				ThreadId);
		}

		if (Alignment != MemoryTrackerUnknownSize &&
			Active.Alignment != MemoryTrackerUnknownSize &&
			Alignment != Active.Alignment)
		{
			++Statistics.AlignmentMismatchCount;
			addIssueLocked(
				MemoryIssueType::AlignmentMismatch,
				Operation,
				reinterpret_cast<std::uintptr_t>(Ptr),
				Active.Size,
				Alignment,
				"Aligned delete/free alignment does not match allocation alignment.",
				{},
				TimestampNs,
				ThreadId);
		}

		MemoryPayloadPreview Payload = capturePayloadPreview(Ptr, Active.Size);
		Statistics.TotalFreedBytes += Active.Size;
		Statistics.CurrentLiveBytes = Statistics.CurrentLiveBytes >= Active.Size
			? Statistics.CurrentLiveBytes - Active.Size
			: 0;
		if (Statistics.CurrentLiveBlocks > 0)
		{
			--Statistics.CurrentLiveBlocks;
		}

		FreedAllocation Freed;
		Freed.Sequence = ++NextFreedSequence;
		Freed.FreedBy = Operation;
		Freed.Size = Active.Size;
		Freed.Alignment = Active.Alignment;
		Freed.TimestampNs = TimestampNs;
		Freed.ThreadId = ThreadId;
		Freed.Payload = Payload;
		rememberFreedAllocationLocked(Ptr, Freed);

		ActiveAllocations.erase(ActiveIter);
		(void)Caller;
	}

	mutable std::mutex Mutex;
	std::unordered_map<void*, ActiveAllocation> ActiveAllocations;
	std::unordered_map<void*, FreedAllocation> FreedAllocations;
	std::deque<FreedOrderEntry> FreedOrder;
	std::vector<TraceRecord> Timeline;
	std::vector<MemoryIssueRecord> Issues;
	MemoryTrackerStatistics Statistics;

	std::size_t PayloadPreviewBytes = 32;
	std::size_t FreedHistoryLimit = 8192;
	std::uint64_t NextFreedSequence = 0;
	std::atomic<bool> HookEnabled{false};
};

} // namespace core::mem
