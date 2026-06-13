# PotatoStandardLibrary

PotatoStandardLibrary is the standard library layer for PotatoEngine. The current repository is organized as a small modular CMake workspace:

- The root project controls global options and whether tests are enabled.
- `PotatoStandard/` defines the library target `PotatoStandard::PotatoStandard`.
- `Test/` contains standalone test executables that are auto-registered with CTest.

## Repository layout

```text
PotatoStandardLibrary/
|- CMakeLists.txt
|- PotatoStandard/
|  |- CMakelists.txt
|  `- Include/
|     `- PotatoStandard/
|        `- Version.hpp
`- Test/
   |- CMakeLists.txt
   `- version_smoke_test.cpp
```

## Module responsibilities

### Root module

The root [CMakeLists.txt](CMakeLists.txt) is responsible for:

- declaring the top-level project;
- exposing `POTATO_STANDARD_BUILD_TESTS` so tests can be enabled or disabled cleanly;
- adding the `PotatoStandard` module;
- enabling `CTest` and the `Test` module when testing is turned on.

### Library module

The [PotatoStandard/CMakelists.txt](PotatoStandard/CMakelists.txt) file is the library module entry point. It:

- discovers public headers under `PotatoStandard/Include/`;
- discovers optional implementation sources under `PotatoStandard/Source/`;
- creates either a normal library target or an interface-only library when no source files exist;
- exports the namespaced target `PotatoStandard::PotatoStandard`.

This means the library can start as header-only and later grow into a compiled module without changing how downstream code links to it.

### Test module

The [Test/CMakeLists.txt](Test/CMakeLists.txt) file is intentionally simple. Every `.cc`, `.cpp`, or `.cxx` file directly under `Test/` becomes:

- one executable target;
- one `ctest` entry with the same generated target name.

This keeps test registration modular: adding one new file adds one new test without editing the test CMake file.

## Detailed modular configuration flow

### 1. Prerequisites

On Windows, make sure these tools are available:

- CMake 3.10 or newer;
- a C++20-capable MSVC toolchain or another supported compiler;
- an existing configured developer shell when invoking command-line builds.

This repository already contains a `build/` directory generated for Visual Studio/MSVC, so you can usually reconfigure and build in place.

### 2. Configure the project

From the repository root:

```powershell
cmake -S . -B build -DPOTATO_STANDARD_BUILD_TESTS=ON -DBUILD_TESTING=ON
```

What this does:

- `-S .` points CMake at the root module;
- `-B build` writes or refreshes the generated build system in `build/`;
- `-DPOTATO_STANDARD_BUILD_TESTS=ON` keeps the test module enabled;
- `-DBUILD_TESTING=ON` allows `CTest` to register test executables.

If you want to configure the library without tests:

```powershell
cmake -S . -B build -DPOTATO_STANDARD_BUILD_TESTS=OFF
```

### 2.1. Configure static analysis with CMake first

This repository now prefers CMake-native analysis metadata instead of editor-specific workspace settings.

- `CMakePresets.json` defines a checked-in configure/build/test workflow for both the existing Visual Studio generator and a `Ninja` generator.
- The root [CMakeLists.txt](CMakeLists.txt) enables `CMAKE_EXPORT_COMPILE_COMMANDS`, which allows generators that support it to emit `compile_commands.json` directly.
- `.clangd` and `compile_flags.txt` remain only as a fallback when the active editor cannot consume CMake metadata and no compile database is available.

Recommended order:

1. Configure the project through a CMake preset.
2. Let your editor bind to that configured CMake build tree.
3. Use clangd fallback files only when a compile database cannot be produced in the current environment.

To configure with the checked-in Visual Studio preset:

```powershell
cmake --preset vs2026-debug
```

If `ninja.exe` is installed and available on `PATH`, you can generate a compile database more reliably with:

```powershell
cmake --preset ninja-debug
```

In the current environment, `Ninja` is listed by CMake but the executable is not installed on `PATH`, so the Visual Studio preset is the default usable option.

### 3. Add or update a library module

Public API should go under `PotatoStandard/Include/`. For example, the current smoke-testable API lives in [PotatoStandard/Include/PotatoStandard/Version.hpp](PotatoStandard/Include/PotatoStandard/Version.hpp).

If you later add compiled sources, place them under `PotatoStandard/Source/`. The existing library CMake file already discovers them automatically.

### 4. Add a test module

To add a new test, create a new `.cpp`, `.cc`, or `.cxx` file under `Test/`. No additional CMake registration is required.

Example:

```text
Test/my_feature_test.cpp
```

After re-running configuration, that file becomes a separate executable and a separate `ctest` case.

### 5. Build the project or a single test

Build everything:

```powershell
cmake --build build --config Debug
```

Build only the smoke test target:

```powershell
cmake --build build --config Debug --target version_smoke_test_cpp
```

The exact target name is derived from the test file path by replacing `/`, `\`, and `.` with `_`.

### 6. Run tests through CTest

Run all tests:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Run only the smoke test:

```powershell
ctest --test-dir build -C Debug -R version_smoke_test_cpp --output-on-failure
```

### 7. Install the library

If you want to verify installation/export behavior:

```powershell
cmake --install build --config Debug
```

The install step exports `PotatoStandard::PotatoStandard` for downstream consumers.

## Current smoke test

The current smoke test validates the minimal public API in [PotatoStandard/Include/PotatoStandard/Version.hpp](PotatoStandard/Include/PotatoStandard/Version.hpp) by checking:

- the exported library semantic version constants;
- the combined `VersionValue()` helper.

The corresponding test source is [Test/version_smoke_test.cpp](Test/version_smoke_test.cpp).

## Recommended workflow for future modules

1. Add public headers under `PotatoStandard/Include/` first.
2. Add compiled sources under `PotatoStandard/Source/` only when the module needs implementation files.
3. Add one test file per feature slice under `Test/`.
4. Re-run `cmake --preset vs2026-debug` after adding files, or use `ninja-debug` when `ninja.exe` is available.
5. Build only the affected target when possible.
6. Run the matching `ctest` case before broadening scope.
