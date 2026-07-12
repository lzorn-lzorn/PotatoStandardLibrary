set(
	POTATO_MEMORY_HOOK_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/Observability/new_malloc_tracker.cpp"
	PARENT_SCOPE
)

set(
	POTATO_MEMORY_DEBUG_GUARDS_MODE
	"AUTO"
	CACHE STRING
	"Compile-time policy for debug guards: AUTO, ON, OFF."
)
set_property(
	CACHE POTATO_MEMORY_DEBUG_GUARDS_MODE
	PROPERTY STRINGS AUTO ON OFF
)

set(
	POTATO_MEMORY_UAF_DETECTION_MODE
	"AUTO"
	CACHE STRING
	"Compile-time policy for use-after-free detection: AUTO, ON, OFF."
)
set_property(
	CACHE POTATO_MEMORY_UAF_DETECTION_MODE
	PROPERTY STRINGS AUTO ON OFF
)

option(
	POTATO_MEMORY_CAPTURE_STACK
	"Enable compile-time stack capture in allocator metadata path."
	ON
)

option(
	POTATO_MEMORY_QUARANTINE_RELEASE_ONLY_ON_FLUSH
	"Compile quarantine policy: if ON, release quarantined blocks only on explicit flush."
	ON
)

option(
	POTATO_MEMORY_LAZY_COMMIT
	"Enable lazy decommit on PageAllocator span release at compile time."
	ON
)

option(
	POTATO_MEMORY_INTERNAL_BENCH_TIMING
	"Compile internal allocator timing benchmarks and periodic timing prints."
	ON
)
