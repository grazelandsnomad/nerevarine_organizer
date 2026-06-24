#!/usr/bin/env bash
#
# sanitize.sh - run the unit suite under AddressSanitizer + UBSanitizer.
#
# Mirrors the Linux sanitizer CI job in .github/workflows/sanitizers.yml.
# Produces a side build under build-san/ so it doesn't interfere with the
# release build dir.  Unit-tests only -- the integration suite hits
# Nexus / a real desktop session and is not part of the gate.
#
# Catches the bug class the dangling-placeholder rewrite was prone to
# before the QUuid InstallToken work landed: use-after-free,
# null-deref, signed overflow, stack-use-after-return.
#
# `-fno-sanitize-recover=all` flips UBSan from "print and continue" to
# "abort on first finding" so a violation actually fails the build,
# not just dumps a one-liner that gets buried in the test log.

set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")/.."

BUILD_DIR=build-san

# -O1 strikes the usual balance: optimization is required for some
# UBSan checks to fire at all, while -O3 obscures the line numbers in
# the sanitizer reports we'd actually need to triage failures.
SAN_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g -O1"

if [[ ! -f $BUILD_DIR/CMakeCache.txt ]]; then
    cmake -S . -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=gcc \
        -DCMAKE_CXX_COMPILER=g++ \
        -DCMAKE_CXX_FLAGS="$SAN_FLAGS" \
        -DCMAKE_C_FLAGS="$SAN_FLAGS" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
fi

cmake --build "$BUILD_DIR" -j"$(nproc)"

# detect_stack_use_after_return is the cheap-but-high-signal check the
# stranded-placeholder bug would have tripped on; keep it on for CI.
# strict_string_checks catches unterminated buffers passed to libc.
# abort_on_error makes the test runner see non-zero exits when a
# violation hits, not just a stderr blob.
ASAN_OPTIONS="halt_on_error=1:abort_on_error=1:strict_string_checks=1:detect_stack_use_after_return=1:detect_leaks=1" \
UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
ctest --test-dir "$BUILD_DIR" -L unit --output-on-failure -j"$(nproc)"
