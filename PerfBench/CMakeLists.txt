add_executable(
    MemoryAllocatorBenchmark
    ${CMAKE_CURRENT_SOURCE_DIR}/MemoryAllocatorBenchmark.cpp
)

target_link_libraries(
    MemoryAllocatorBenchmark
    PRIVATE
        Core::Core
        benchmark::benchmark_main
)

target_compile_features(MemoryAllocatorBenchmark PRIVATE cxx_std_23)

set_target_properties(
    MemoryAllocatorBenchmark
    PROPERTIES
        FOLDER "Benchmarks"
)
