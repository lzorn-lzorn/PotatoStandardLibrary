# PotatoStandardLibrary

PotatoStandardLibrary is the shared foundation library for PotatoEngine.

This repository is now organized for direct subproject consumption:

- Add it via `add_subdirectory(...)`
- Link against `Core::Core`
- Include a single umbrella header: `#include <Core/Core.h>`

## Quick start (as a subproject)

```cmake
# Main project CMakeLists.txt
add_subdirectory(external/PotatoStandardLibrary)

target_link_libraries(MyGame PRIVATE Core::Core)
```

```cpp
#include <Core/Core.h>

int main()
{
    core::dynamic_array<int> values;
    values.push_back(1);
    return 0;
}
```

## Repository layout

```text
PotatoStandardLibrary/
|- CMakelists.txt
|- Core/
|  |- CMakelists.txt
|  |- Core.h                          # unified public umbrella header
|  |
|  |- Foundation/                     # categorized public wrappers
|  |  |- common.h
|  |  |- enum_flag.h
|  |  `- std_interface.h
|  |- Platform/
|  |  `- platform.h
|  |- Diagnostics/
|  |  |- exception_handler.h
|  |  `- logger.h
|  |- Containers/
|  |  |- buffer.h
|  |  |- mpmc_queue.h
|  |  |- observed.h
|  |  `- radix_tree.h
|  |- Math/
|  |  `- math.h
|  |- Text/
|  |  `- string.h
|  |- Time/
|  |  `- time.h
|  `- Memory/
|
|- Test/
|- PerfBench/
`- benchmark/                         # optional third-party benchmark source
```

Note:
- Existing legacy include paths (for example `<Core/buffer.h>`) remain usable.
- New categorized wrappers provide a cleaner module view (for example `<Core/Containers/buffer.h>`).

## CMake options

Top-level options in `CMakelists.txt`:

- `POTATO_STANDARD_BUILD_TESTS`: build `Test/` targets. Default `ON` only when this repo is the top-level project.
- `POTATO_STANDARD_BUILD_BENCHMARKS`: build `PerfBench/` targets. Default `ON` only when top-level.
- `POTATO_STANDARD_USE_BUNDLED_BENCHMARK`: use local `benchmark/` when benchmarks are enabled. Default `ON`.
- `POTATO_STANDARD_ENABLE_INSTALL`: enable `install()` and CMake export rules for `Core::Core`. Default `ON` only when top-level.
- `POTATO_STANDARD_ENABLE_WARNINGS`: enable stricter compile warnings for library build. Default `ON` only when top-level.

Memory compile-time options in `Core/Memory/CMakeLists.txt`:

- `POTATO_MEMORY_DEBUG_GUARDS_MODE`: `AUTO|ON|OFF`
- `POTATO_MEMORY_UAF_DETECTION_MODE`: `AUTO|ON|OFF`
- `POTATO_MEMORY_CAPTURE_STACK`: `ON|OFF`
- `POTATO_MEMORY_QUARANTINE_RELEASE_ONLY_ON_FLUSH`: `ON|OFF`
- `POTATO_MEMORY_LAZY_COMMIT`: `ON|OFF`
- `POTATO_MEMORY_INTERNAL_BENCH_TIMING`: internal allocator timing switch (default `OFF` from root)

## Build and test

```powershell
cmake -S . -B build -DPOTATO_STANDARD_BUILD_TESTS=ON -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## Benchmark build

```powershell
cmake -S . -B build_bench -DPOTATO_STANDARD_BUILD_BENCHMARKS=ON
cmake --build build_bench --config Release --target MemoryAllocatorBenchmark
```

## Design goals

- Work as a clean engine base library under `add_subdirectory`
- Keep public integration simple via `Core::Core` and `<Core/Core.h>`
- Preserve backward compatibility for existing include usage while offering categorized headers
- Keep tests/benchmarks optional and off by default for subproject consumers

