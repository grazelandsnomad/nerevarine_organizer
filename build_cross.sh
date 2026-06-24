#!/bin/sh
# Cross-compilation helper - called automatically by Code::Blocks for the
# "Debug (Windows)" and "Release (Windows)" build targets.
#
# Usage: sh build_cross.sh <platform> <config>
#   platform : windows
#   config   : debug   | release
#
# Prerequisites: see the comments at the top of the relevant toolchain file:
#   cmake/toolchain-windows.cmake  - Windows (MXE)

set -e   # abort immediately on any error

PLATFORM="$1"
CONFIG="$2"

# -- Validate arguments ---
case "$PLATFORM" in
    windows)
        TOOLCHAIN="cmake/toolchain-windows.cmake"
        ;;
    *)
        echo "ERROR: Unknown platform '$PLATFORM'. Expected: windows" >&2
        exit 1
        ;;
esac

case "$CONFIG" in
    debug)   CMAKE_CONFIG="Debug"   ;;
    release) CMAKE_CONFIG="Release" ;;
    *)
        echo "ERROR: Unknown config '$CONFIG'. Expected: debug | release" >&2
        exit 1
        ;;
esac

BUILD_DIR="build_${PLATFORM}_${CONFIG}"

# -- Configure (only if CMakeCache.txt is absent or toolchain changed) ---
echo ""
echo "==================================================================="
echo "  Cross-compiling for ${PLATFORM} / ${CMAKE_CONFIG}"
echo "  Build dir : ${BUILD_DIR}/"
echo "  Toolchain : ${TOOLCHAIN}"
echo "==================================================================="
echo ""

cmake -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DCMAKE_BUILD_TYPE="${CMAKE_CONFIG}"

# -- Build ---
cmake --build "${BUILD_DIR}" --config "${CMAKE_CONFIG}"

echo ""
echo "Build complete.  Binary is in: ${BUILD_DIR}/"
