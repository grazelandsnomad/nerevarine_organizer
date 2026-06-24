#!/usr/bin/env bash
#
# clang-tidy.sh - run the project's clang-tidy gate locally.
#
# Mirrors what .github/workflows/clang-tidy.yml does on CI.  Configures
# a side build under build-tidy/ with clang as the compiler, generates
# compile_commands.json, and walks every src/ TU.  Failures exit non-
# zero so it composes with `&&` in shell pipelines.
#
# Why a side build dir: the main build/ uses GCC and may pull in flags
# (Arch's `-mno-direct-extern-access`) that clang doesn't accept.  Running
# clang-tidy against a clang-configured build dir keeps both compilers
# happy without sed-ing the JSON.
#
# Why we exclude moc / autogen TUs: those are generated; failures there
# are not actionable here.  Headers are filtered via HeaderFilterRegex
# in .clang-tidy itself.

set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")/.."

BUILD_DIR=build-tidy

if [[ ! -f $BUILD_DIR/compile_commands.json ]]; then
    cmake -S . -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi

# AUTOMOC outputs and the generated translation header have to exist
# before clang-tidy parses the TUs that include them.
cmake --build "$BUILD_DIR" --target translation_keys_header
cmake --build "$BUILD_DIR" --target nerevarine_organizer_autogen

# Walk every project source.  -P0 lets xargs use one process per CPU;
# -I{} prevents word-splitting on paths with spaces.  We don't use
# run-clang-tidy because its prefix-matching of source files trips on
# our tests/ subdir layout.
find src -name "*.cpp" -print0 \
    | xargs -0 -P"$(nproc)" -I{} \
        clang-tidy -p "$BUILD_DIR" --quiet --warnings-as-errors='*' {}
