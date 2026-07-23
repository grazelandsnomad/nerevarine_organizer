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

# One temp root for everything this run needs.  Bash traps are not additive:
# a second `trap ... EXIT` for the per-TU logs below would silently replace
# this one and leak the preflight dir.
tmproot=$(mktemp -d)
trap 'rm -rf "$tmproot"' EXIT
preflight=$tmproot/preflight
tidy_logs=$tmproot/logs
mkdir -p "$preflight" "$tidy_logs"
cat > "$preflight/probe.cpp" <<'PROBE'
#include <expected>
std::expected<int, int> probe() { return 1; }
PROBE
echo "clang-tidy.sh: using $CLANGXX and $CLANG_TIDY"

# Both binaries have to be checked, not just the compiler. They are versioned
# independently and clang-tidy carries its OWN copy of the frontend, so a run
# with a new clang++ and a stale clang-tidy looks correctly upgraded and fails
# identically. (That is exactly how the first attempt at this fix went: the
# distro splits clang-tidy-19 into its own package, so installing
# clang-tools-19 upgraded nothing.)
probe_ok() {
    case $1 in
    compiler) "$CLANGXX" -std=c++23 -fsyntax-only "$preflight/probe.cpp" 2>/dev/null ;;
    # Two traps here, both of which silently turned this check into a no-op
    # while it looked correct:
    #   --checks is mandatory. The probe sits outside the repo, so no
    #   .clang-tidy is found and clang-tidy exits with "no checks enabled"
    #   WITHOUT compiling anything, and there is no error to find.
    #   The output must be captured, not piped to grep. clang-tidy exits
    #   non-zero when it reports the error, and under `set -o pipefail` that
    #   status wins the pipeline, so `! tidy | grep -q` reports success exactly
    #   when the problem IS present.
    tidy)
        local out
        out=$("$CLANG_TIDY" --quiet --checks='-*,bugprone-assert-side-effect' \
                  "$preflight/probe.cpp" -- -std=c++23 2>&1 || true)
        case $out in
            *"no template named 'expected'"*) return 1 ;;
            *)                                return 0 ;;
        esac
        ;;
    esac
}

if ! probe_ok compiler || ! probe_ok tidy; then
    failed=$(probe_ok compiler && echo "$CLANG_TIDY" || echo "$CLANGXX")
    cat >&2 <<EOF
clang-tidy.sh: $failed cannot handle std::expected, so every TU that includes
safe_fs.h would fail with a misleading "no template named 'expected'".

libstdc++ gates <expected> on BOTH __cplusplus >= 202100L and
__cpp_concepts >= 202002L (bits/version.h). This compiler reports:
  __cplusplus     = $(probe_macro __cplusplus)
  __cpp_concepts  = $(probe_macro __cpp_concepts)
Clang did not raise __cpp_concepts to 202002L until 19. Install clang 19 or
newer (Ubuntu: clang-19 AND clang-tidy-19, which are separate packages), or
point CLANGXX / CLANG_TIDY at one.
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

# clang-tidy analyses a file once per compile_commands.json entry, and CMake
# emits one entry per (source, target) pair.  47 of our 88 sources are
# compiled into a test target as well as the app - 148 entries - so most of
# them are analysed two to four times for no new findings (clang-tidy even
# collapses the duplicate diagnostics before printing).  It is just run time.
#
# So point -p at a filtered database holding one entry per source.  The app
# target's entry specifically, not merely the first one seen: each test target
# carries its own include dirs and not all of them add <build>/generated, so a
# test entry can fail to compile a TU that the app entry handles.  Every
# src/*.cpp is in CMakeLists' SOURCES, so an app entry always exists.
#
# Written beside the real database rather than over it - CMake owns that file,
# regenerates it on reconfigure, and .clangd reads it for the editor.
TIDY_DB=$BUILD_DIR/tidy-db
mkdir -p "$TIDY_DB"
if command -v python3 >/dev/null 2>&1; then
    python3 - "$BUILD_DIR/compile_commands.json" "$TIDY_DB/compile_commands.json" <<'FILTER'
import json, sys

source, dest = sys.argv[1], sys.argv[2]
best = {}
for entry in json.load(open(source)):
    # CMake's "output" is <build>/[tests/]CMakeFiles/<target>.dir/<path>.o
    is_app = "/CMakeFiles/nerevarine_organizer.dir/" in entry.get("output", "")
    keep, seen = best.get(entry["file"], (False, None))
    if seen is None or (is_app and not keep):
        best[entry["file"]] = (is_app, entry)
json.dump([entry for _, entry in best.values()], open(dest, "w"))
FILTER
else
    # Not fatal: the gate still reports the same findings, just slower.
    echo "clang-tidy.sh: no python3, so every compile_commands.json entry is" \
         "analysed and the shared TUs are walked more than once" >&2
    cp "$BUILD_DIR/compile_commands.json" "$TIDY_DB/compile_commands.json"
fi

# Walk every project source.  -P"$(nproc)" gives one process per CPU (-P0
# would be unlimited); -I{} prevents word-splitting on paths with spaces.
# We don't use
# run-clang-tidy because its prefix-matching of source files trips on
# our tests/ subdir layout.
#
# No --warnings-as-errors here on purpose: passing it on the command line
# *overrides* .clang-tidy's `WarningsAsErrors: '*,-misc-include-cleaner'`
# rather than merging with it, which re-promotes the misc-include-cleaner
# backlog to an error and fails the gate on the first TU.  Letting the
# config file govern keeps bugprone-*/clang-analyzer-* fatal and
# include-cleaner advisory, which is what .clang-tidy documents.
#
# Each TU's output goes to its own file rather than to a stdout shared with
# the other N-1 running processes.  Writing straight through under `xargs -P`
# interleaves the diagnostics line by line, and --quiet drops the per-file
# "N warnings generated" markers that would otherwise delimit them, so a
# failing gate used to be tens of thousands of unattributable lines behind
# xargs' exit 123 ("some invocation failed") and nothing else.
tidy_one() {
    local src=$1 out rc=0
    out=$("$CLANG_TIDY" -p "$TIDY_DB" --quiet "$src" 2>&1) || rc=$?
    # clang-tidy prints "[1/1] (2/4) Processing file ..." to stderr for every
    # compile-database entry a source has, and --quiet does not cover it.  The
    # filtered database above leaves one entry per source so this should now
    # be silent, but it still fires on the no-python3 fallback path, and it
    # made three otherwise-clean TUs look like they had findings.  Drop it, so
    # that "this TU produced a block" means "it produced diagnostics".
    out=$(printf '%s\n' "$out" | grep -vE \
        '^\[[0-9]+/[0-9]+\] \([0-9]+/[0-9]+\) Processing file .*\.$' || true)
    # `if`, not `[[ ... ]] && printf`: as the last command in a function the
    # && form returns 1 whenever the condition is false, which would report
    # every clean TU as a failure.
    if [[ $rc -ne 0 ]]; then
        # One short path appended to an O_APPEND file is atomic under
        # PIPE_BUF, so the parallel writers here need no lock.
        printf '%s\n' "$src" >> "$tidy_logs/failed"
    fi
    if [[ -n $out ]]; then
        { printf '===== %s =====\n' "$src"; printf '%s\n' "$out"; } \
            > "$tidy_logs/$(printf '%s' "$src" | tr -c 'A-Za-z0-9._-' '_').out"
    fi
}
# `bash -c` starts a fresh shell, which does not inherit set -euo pipefail:
# tidy_one must not depend on it.
export -f tidy_one
export CLANG_TIDY TIDY_DB tidy_logs

total=$(find src -name "*.cpp" | wc -l)
walk_rc=0
find src -name "*.cpp" -print0 \
    | xargs -0 -P"$(nproc)" -I{} bash -c 'tidy_one "$@"' _ {} || walk_rc=$?

shopt -s nullglob
tidy_out=("$tidy_logs"/*.out)
shopt -u nullglob

# Replay, one contiguous block per TU.  Clean-but-noisy TUs are printed too:
# misc-include-cleaner is advisory (see .clang-tidy) and its backlog is meant
# to stay visible.  Under Actions each block becomes a collapsed log group.
for out in "${tidy_out[@]}"; do
    src=$(head -n1 "$out"); src=${src#===== }; src=${src% =====}
    if [[ -n ${GITHUB_ACTIONS:-} ]]; then
        printf '::group::%s\n' "$src"
        tail -n +2 "$out"
        printf '::endgroup::\n'
    else
        cat "$out"
    fi
done

# Tally by the [check-name] suffix every clang-tidy diagnostic carries, so a
# failure says which checks fired and not just that something did.  The
# trailing ",-warnings-as-errors" clang-tidy appends to a diagnostic it
# promoted has to come off, or a check is tallied under two different names
# depending on whether WarningsAsErrors covers it.
tally=
if ((${#tidy_out[@]})); then
    tally=$(sed -n \
        's/^.*:[0-9]\+:[0-9]\+: \(warning\|error\): .*\[\([A-Za-z0-9_.,-]\+\)\]$/\2/p' \
        "${tidy_out[@]}" \
        | sed 's/,-warnings-as-errors$//' | sort | uniq -c | sort -rn)
fi

failed=()
if [[ -f $tidy_logs/failed ]]; then
    mapfile -t failed < <(sort -u "$tidy_logs/failed")
fi

printf '\nclang-tidy.sh: %d translation units, %d with errors\n' \
       "$total" "${#failed[@]}"
if [[ -n $tally ]]; then
    printf '%s\n' "$tally"
fi
if ((${#failed[@]})); then
    printf 'failing translation units:\n'
    printf '   %s\n' "${failed[@]}"
fi

# Put the same verdict on the run page.  The step log is 88 collapsed groups
# deep by this point, and "Process completed with exit code 123" on its own is
# what made previous failures of this gate take a push cycle to read.
if [[ -n ${GITHUB_STEP_SUMMARY:-} ]]; then
    {
        printf '## clang-tidy\n\n'
        printf '%d translation units, **%d** with errors.\n\n' \
               "$total" "${#failed[@]}"
        if [[ -n $tally ]]; then
            printf '| count | check |\n|---:|:---|\n'
            printf '%s\n' "$tally" | awk '{printf "| %s | `%s` |\n", $1, $2}'
        fi
        if ((${#failed[@]})); then
            printf '\n### Failing translation units\n\n'
            printf -- '- `%s`\n' "${failed[@]}"
        fi
    } >> "$GITHUB_STEP_SUMMARY"
fi

# Inline annotations for the fatal diagnostics.  GitHub renders only the first
# handful per step, so cap deliberately and say what was dropped - a silent
# truncation would read as "that was all of them".
if [[ -n ${GITHUB_ACTIONS:-} ]] && ((${#tidy_out[@]})); then
    annotated=0
    while IFS=$'\t' read -r file line col msg; do
        annotated=$((annotated + 1))
        ((annotated > 20)) && continue
        printf '::error file=%s,line=%s,col=%s::%s\n' \
               "${file#"$PWD/"}" "$line" "$col" "${msg//\%/%25}"
    done < <(sed -n \
        's/^\(.*\):\([0-9]\+\):\([0-9]\+\): error: \(.*\)$/\1\t\2\t\3\t\4/p' \
        "${tidy_out[@]}" | sort -u)
    if ((annotated > 20)); then
        printf '::notice::%d further clang-tidy errors are not annotated; see the step log\n' \
               "$((annotated - 20))"
    fi
fi

if ((${#failed[@]})); then
    exit 1
fi
# xargs failing for some other reason (bash missing, 127) must not pass
# silently just because no TU recorded a diagnostic.
if [[ $walk_rc -ne 0 ]]; then
    echo "clang-tidy.sh: the TU walk exited $walk_rc with no per-TU failure recorded" >&2
    exit "$walk_rc"
fi
