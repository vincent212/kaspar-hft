#!/bin/bash
# Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
#
# Licensed under the MIT License. See LICENSE file in the project root.

# CMake build script for actors library and coordinator

set -e  # Exit on error

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${SCRIPT_DIR}/build"

# Parse arguments
BUILD_TYPE="Release"
CLEAN=false
VERBOSE=false
INSTALL=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -d|--debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -i|--install)
            INSTALL=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -d, --debug     Build with debug symbols (default: Release)"
            echo "  -c, --clean     Clean build directory before building"
            echo "  -v, --verbose   Verbose build output"
            echo "  -i, --install   Install binaries to bin/ directory"
            echo "  -h, --help      Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                # Release build"
            echo "  $0 --debug        # Debug build"
            echo "  $0 --clean        # Clean rebuild"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Clean if requested
if [ "$CLEAN" = true ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Configure with CMake
echo "Configuring CMake (${BUILD_TYPE})..."
cd "$BUILD_DIR"
cmake .. -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# Build
echo "Building..."
if [ "$VERBOSE" = true ]; then
    cmake --build . --parallel $(nproc)
else
    cmake --build . --parallel $(nproc) 2>&1 | grep -v "Building" || true
fi

echo ""
echo "Build complete!"
echo "Build directory: $BUILD_DIR"
echo "Binaries:"
echo "  - coordinator_actor: $BUILD_DIR/coordinator_actor"
echo "  - global_registry: $BUILD_DIR/global_registry"
echo "  - libactors.a: $BUILD_DIR/libactors.a"

# Install to bin/ if requested
if [ "$INSTALL" = true ]; then
    echo ""
    echo "Installing to bin/..."
    mkdir -p "${SCRIPT_DIR}/bin"
    cp -v "$BUILD_DIR/coordinator_actor" "${SCRIPT_DIR}/bin/"
    cp -v "$BUILD_DIR/global_registry" "${SCRIPT_DIR}/bin/"
    echo "Installed to: ${SCRIPT_DIR}/bin/"
fi

echo ""
echo "To run coordinator:"
echo "  cd $SCRIPT_DIR && ./build/coordinator_actor --port 7755 --monitor-port 5557"
echo ""
echo "To run registry:"
echo "  cd $SCRIPT_DIR && ./build/global_registry --port 7756 --monitor-port 5558"
