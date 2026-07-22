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

# Pick the newest versioned clang on PATH, honouring an explicit override.
# libstdc++ gates std::expected on `__cpp_concepts >= 202002L` (see the
# preflight below), which clang only defines from 19 onwards, so "whatever
# clang is default" is not good enough on distros still defaulting to 18.
pick_newest() {
    local base=$1 best="" v
    for v in $(seq 30 -1 19); do
        if command -v "$base-$v" >/dev/null 2>&1; then best="$base-$v"; break; fi
    done
    [[ -z $best ]] && command -v "$base" >/dev/null 2>&1 && best="$base"
    printf '%s' "$best"
}

CLANGXX=${CLANGXX:-$(pick_newest clang++)}
CLANG_TIDY=${CLANG_TIDY:-$(pick_newest clang-tidy)}
if [[ -z $CLANGXX || -z $CLANG_TIDY ]]; then
    echo "clang-tidy.sh: need clang++ and clang-tidy on PATH" >&2
    exit 1
fi

# Preflight: prove the toolchain can actually see std::expected BEFORE spending
# ten minutes to discover it cannot.
#
# libstdc++ guards <expected> with `(__cplusplus >= 202100L) && (__cpp_concepts
# >= 202002L)` (bits/version.h). Clang did not raise __cpp_concepts to 202002L
# until 19, so clang 18 includes <expected> successfully and defines NOTHING in
# it. The result is "no template named 'expected' in namespace 'std'" repeated
# across every TU that transitively includes safe_fs.h - 123 errors over 18 TUs,
# with the real cause nowhere in the output.
probe_macro() {
    "$CLANGXX" -std=c++23 -dM -E -x c++ /dev/null 2>/dev/null \
        | awk -v m="$1" '$2 == m {print $3; found=1} END {if (!found) print "undefined"}'
}

preflight=$(mktemp -d)
trap 'rm -rf "$preflight"' EXIT
cat > "$preflight/probe.cpp" <<'PROBE'
#include <expected>
std::expected<int, int> probe() { return 1; }
PROBE
if ! "$CLANGXX" -std=c++23 -fsyntax-only "$preflight/probe.cpp" 2>/dev/null; then
    cat >&2 <<EOF
clang-tidy.sh: $CLANGXX cannot compile std::expected, so every TU that includes
safe_fs.h would fail with a misleading "no template named 'expected'".

libstdc++ gates <expected> on BOTH __cplusplus >= 202100L and
__cpp_concepts >= 202002L (bits/version.h). This compiler reports:
  __cplusplus     = $(probe_macro __cplusplus)
  __cpp_concepts  = $(probe_macro __cpp_concepts)
Clang did not raise __cpp_concepts to 202002L until 19. Install clang 19 or
newer (Ubuntu: clang-19 clang-tools-19), or point CLANGXX / CLANG_TIDY at one.
EOF
    exit 1
fi

if [[ ! -f $BUILD_DIR/compile_commands.json ]]; then
    # No C compiler pinned: the project is CXX-only, and passing
    # CMAKE_C_COMPILER just earns an "unused variable" warning on every run.
    cmake -S . -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER="$CLANGXX" \
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
#
# No --warnings-as-errors here on purpose: passing it on the command line
# *overrides* .clang-tidy's `WarningsAsErrors: '*,-misc-include-cleaner'`
# rather than merging with it, which re-promotes the misc-include-cleaner
# backlog to an error and fails the gate on the first TU.  Letting the
# config file govern keeps bugprone-*/clang-analyzer-* fatal and
# include-cleaner advisory, which is what .clang-tidy documents.
find src -name "*.cpp" -print0 \
    | xargs -0 -P"$(nproc)" -I{} \
        "$CLANG_TIDY" -p "$BUILD_DIR" --quiet {}
