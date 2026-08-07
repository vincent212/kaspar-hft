<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# CMake Build System for Actors Library

Builds the in-process actors library (`libactors.a`) and its unit tests, using a modern CMake
build system with automatic dependency tracking.

## Quick Start

```bash
# Build everything (Release mode)
./build.sh

# Build with debug symbols
./build.sh --debug

# Clean rebuild
./build.sh --clean
```

## Manual CMake Usage

```bash
# Configure
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . --parallel $(nproc)

# Run tests (if Google Test is installed)
ctest --output-on-failure
```

## Build Outputs

After building, you'll find:

- `build/libactors.a` - Core actors library (static)
- `build/run_tests` - Unit tests (if Google Test found)

## CMake Benefits Over Old Makefile

1. **Automatic dependency tracking** - Changing a header file rebuilds affected sources
2. **Parallel builds** - Faster compilation with `--parallel`
3. **Cross-platform support** - Works on Linux and macOS
4. **Better IDE integration** - Generates `compile_commands.json` for clangd/LSP
5. **Out-of-source builds** - Keeps source tree clean
6. **Proper test integration** - `ctest` for running tests

## Dependency Locations

The CMake script looks for dependencies in OS-specific paths:

**Linux:**
- Boost: `/usr/local/boost188`
- Google Test: `/usr/local`

**macOS:**
- Boost: `/opt/homebrew/opt/boost`
- Google Test: `/opt/homebrew/opt/googletest`

## Migrating from Old Makefile

The old Makefile still works, but CMake is recommended for new development:

| Old Makefile | New CMake |
|--------------|-----------|
| `make` | `./build.sh` |
| `make clean` | `./build.sh --clean` |
| `make debug` | `./build.sh --debug` |
| `make test` | `ctest` |

## Troubleshooting

**CMake can't find dependencies:**
- Check that paths in `CMakeLists.txt` match your system
- Update `BOOST_ROOT` if needed
- Run `cmake .. -DCMAKE_BUILD_TYPE=Release` to see which dependencies failed

**Build fails with missing headers:**
- Make sure all dependencies are installed
- Check that `../../chutil/include` exists
