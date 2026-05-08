<!--
    Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
    Licensed under the MIT License. See LICENSE file in the project root.
-->

# CMake Build System for Actors Library

This directory contains the actors library and coordinator/registry binaries, now using a modern CMake build system with automatic dependency tracking.

## Quick Start

```bash
# Build everything (Release mode)
./build.sh

# Build with debug symbols
./build.sh --debug

# Clean rebuild
./build.sh --clean

# Install binaries to bin/ directory
./build.sh --install
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
- `build/coordinator_actor` - Coordinator binary
- `build/global_registry` - Global registry binary
- `build/run_tests` - Unit tests (if Google Test found)
- `build/run_coord_tests` - Coordination tests
- `build/run_registry_tests` - Registry tests

## Running the Coordinator

```bash
cd /home/vm/m2_kaspr/actors/cpp
./build/coordinator_actor --port 7755 --monitor-port 5557 --paused
```

Options:
- `--port <N>` - Coordination socket port (default: 7755)
- `--monitor-port <N>` - MQ0 monitoring port (default: 5557)
- `--paused` - Start in paused mode (requires DEBUG_CONTINUE to start)
- `--debug` - Enable debug logging
- `--trace-file <path>` - Enable tracing to file

## Running the Registry

```bash
cd /home/vm/m2_kaspr/actors/cpp
./build/global_registry --port 7756 --monitor-port 5558
```

Options:
- `--port <N>` - Registry socket port (default: 7756)
- `--monitor-port <N>` - MQ0 monitoring port (default: 5558)
- `--debug` - Enable debug logging

## CMake Benefits Over Old Makefile

1. **Automatic dependency tracking** - Changing a header file rebuilds affected sources
2. **Parallel builds** - Faster compilation with `--parallel`
3. **Cross-platform support** - Works on Linux and macOS
4. **Better IDE integration** - Generates `compile_commands.json` for clangd/LSP
5. **Out-of-source builds** - Keeps source tree clean
6. **Proper test integration** - `ctest` for running tests
7. **Cleaner dependency management** - Finds Boost, ZMQ, etc. automatically

## Dependency Locations

The CMake script looks for dependencies in OS-specific paths:

**Linux:**
- Boost: `/usr/local/boost188`
- ZeroMQ: `/usr/local`
- cppzmq: `/usr/local`
- nlohmann-json: `/usr/local`
- Google Test: `/usr/local`

**macOS:**
- Boost: `/opt/homebrew/opt/boost`
- ZeroMQ: `/opt/homebrew/opt/zeromq`
- cppzmq: `/opt/homebrew/opt/cppzmq`
- nlohmann-json: `/opt/homebrew/opt/nlohmann-json`
- Google Test: `/opt/homebrew/opt/googletest`

## Migrating from Old Makefile

The old Makefile still works, but CMake is recommended for new development:

| Old Makefile | New CMake |
|--------------|-----------|
| `make coordination-actor` | `./build.sh` |
| `make registry` | `./build.sh` |
| `make clean` | `./build.sh --clean` |
| `make debug` | `./build.sh --debug` |
| `make test-coordination` | `ctest` |

## Troubleshooting

**CMake can't find dependencies:**
- Check that paths in `CMakeLists.txt` match your system
- Update `BOOST_ROOT`, `ZMQ_ROOT`, etc. if needed
- Run `cmake .. -DCMAKE_BUILD_TYPE=Release` to see which dependencies failed

**Build fails with missing headers:**
- Make sure all dependencies are installed
- Check that `../../chutil/include` exists

**Binaries crash with ZMQ errors:**
- Rebuild after updating source files with `./build.sh --clean`
- Check that coordinator/registry are using matching library versions

## Integration with Launcher

The launcher (`actors/launcher/launcher.py`) looks for binaries in these locations (in order):

1. `actors/cpp/bin/coordinator_actor` (permanent location - use `./build.sh --install`)
2. `actors/cpp/coordinator_actor` (build root)
3. `actors/cpp/build/coordinator_actor` (CMake build dir)

To make binaries available to launcher:

```bash
./build.sh --install
```

This copies binaries to `actors/cpp/bin/` which is the first search location.
