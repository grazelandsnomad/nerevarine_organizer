#!/usr/bin/env bash
# ---
# release-smoke.sh - headless launch sanity-check for a Release build.
#
# Catches the class of regression that unit tests can't: a binary that
# builds clean and passes ctest but, on first launch, spams Qt warnings,
# can't find :/assets/ resources, or tries to load a translation that
# isn't in the bundle.  "QObject::connect: No such slot" and "Cannot open
# resource: :/foo.qrc" used to slip into releases because nobody launched
# the Release binary before tagging.
#
# v1 is log-scrape only.  A visual-snapshot-vs-golden pass is its own
# project (baselines drift across Qt point releases, font packages, and
# HiDPI settings) - the framework here leaves room for it: bolt on a
# `--snapshot` flag later that uses Qt's grabWindow + pixel-diff.
#
# Usage:
#   release-smoke.sh                         # build + run + scrape
#   release-smoke.sh --no-build              # reuse whatever's in bin/Release_Linux/
#   release-smoke.sh --duration 5            # let the app run N seconds before SIGTERM
#   release-smoke.sh --verbose               # show the raw captured log at the end
#   release-smoke.sh help
#
# Exit codes:
#   0  clean - no errors surfaced
#   1  bad arguments / help requested as error path
#   2  build failed
#   3  binary missing / non-executable
#   4  launch produced errors (the caller cares about this one)
# ---

set -eu

ROOT="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
BINARY="$ROOT/bin/Release_Linux/nerevarine_organizer"
APP_LOG="$ROOT/bin/Release_Linux/log.txt"

DURATION=3
DO_BUILD=1
VERBOSE=0

usage() {
    sed -n '/^# Usage:/,/^# Exit codes:/p' "$0" \
        | sed -e '$d' -e 's/^# \{0,1\}//'
    exit "${1:-1}"
}

while (( $# )); do
    case "$1" in
        --no-build) DO_BUILD=0 ;;
        --duration) shift; DURATION="${1:-}"
            [[ "$DURATION" =~ ^[0-9]+$ ]] || { echo "error: --duration expects integer" >&2; exit 1; }
            ;;
        --verbose)  VERBOSE=1 ;;
        help|-h|--help) usage 0 ;;
        *)          echo "error: unknown argument: $1" >&2; usage ;;
    esac
    shift
done

# -- Build ---
if (( DO_BUILD )); then
    echo "-- Building Release ---"
    cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 \
        || { echo "error: cmake configure failed" >&2; exit 2; }
    if ! cmake --build "$ROOT/build" -j"$(nproc)"; then
        echo "error: cmake build failed" >&2
        exit 2
    fi
    # Mirror to bin/Release_Linux so the headless launch below hits the
    # same layout the end user will.  Same steps as build.sh.
    mkdir -p "$ROOT/bin/Release_Linux/translations"
    cp -f  "$ROOT/build/nerevarine_organizer" "$BINARY"
    cp -f  "$ROOT/build/nerevarine_prefs.ini" "$ROOT/bin/Release_Linux/nerevarine_prefs.ini"
    cp -rf "$ROOT/build/translations/."       "$ROOT/bin/Release_Linux/translations/"
fi

[[ -x "$BINARY" ]] || { echo "error: binary not executable: $BINARY" >&2; exit 3; }

# -- Headless launch ---
#
# Reset the app log so we only scrape THIS run's output - otherwise a
# prior run's stale FATAL poisons the check forever.
: > "$APP_LOG"

# Capture stderr (where Qt writes warnings) independently of stdout, which
# is mostly empty for this app.  `-platform offscreen` keeps us out of
# the X/Wayland display entirely - no window pops up, no compositor is
# required, CI can run this.
STDOUT_LOG=$(mktemp)
STDERR_LOG=$(mktemp)
cleanup_tmp() { rm -f "$STDOUT_LOG" "$STDERR_LOG"; }
trap cleanup_tmp EXIT

echo "-- Launching headless (duration: ${DURATION}s) ---"
# `timeout -k 2 N` sends SIGTERM after N seconds and SIGKILL 2s later if
# the app ignored it.  Exit code 124 = SIGTERM fired, which is our normal
# healthy outcome - we WANT the app to run to the duration and be killed,
# not to crash on its own.
set +e
timeout -k 2 "$DURATION" "$BINARY" -platform offscreen \
    >"$STDOUT_LOG" 2>"$STDERR_LOG"
RC=$?
set -e

# -- Classify the exit ---
#
# 124 == SIGTERM from timeout, healthy (app ran for the full duration).
# 0   == clean exit, also fine (happens if the app has a cmdline path
#        that exits immediately, e.g. in CI minimal mode).
# 137 == SIGKILL after -k grace, app hung past grace period - report it.
# Anything else is a crash / startup failure.
case "$RC" in
    0|124)
        echo "  launch OK (rc=$RC)"
        ;;
    137)
        echo "  WARNING: app did not honour SIGTERM within grace period (rc=137)"
        ;;
    *)
        echo "  FAIL: launch exited with rc=$RC"
        ;;
esac

# -- Scrape logs ---
#
# Three sources: stderr (live Qt messages), stdout (some println calls),
# and the app's own log.txt (qtMessageHandler dumps there too).  Combine
# them so a message is caught regardless of which sink it landed in.
COMBINED=$(mktemp)
{
    [[ -s "$STDERR_LOG" ]] && { echo "### STDERR ###"; cat "$STDERR_LOG"; echo; }
    [[ -s "$STDOUT_LOG" ]] && { echo "### STDOUT ###"; cat "$STDOUT_LOG"; echo; }
    [[ -s "$APP_LOG"    ]] && { echo "### APP LOG ###"; cat "$APP_LOG"; echo; }
} > "$COMBINED"

cleanup_combined() { rm -f "$COMBINED"; cleanup_tmp; }
trap cleanup_combined EXIT

# Patterns that constitute a smoke failure.  Each is ORed; any hit flags
# the run.  Keep the patterns conservative - false positives defeat the
# whole point of a "quick check" script.
declare -a PATTERN_LABELS=(
    "Qt critical / fatal"
    "missing resource"
    "missing translation key"
    "QObject::connect fail"
    "segfault / abort"
)
declare -a PATTERN_REGEX=(
    '\[(CRIT|FATAL)\]'
    '(Cannot (open|find) resource|QResource.*not found|qrc:[^ ]+ does not exist)'
    '(No language files for the preferred languages found in|translation key missing|T\("[^"]+"\).*missing)'
    'QObject::connect:'
    '(segmentation fault|Aborted|signal handler|backtrace)'
)

ERRORS_FOUND=0
ERROR_SUMMARY=""

for i in "${!PATTERN_LABELS[@]}"; do
    label="${PATTERN_LABELS[$i]}"
    regex="${PATTERN_REGEX[$i]}"
    # grep -c counts matching lines; -E for extended regex; -i case-insens.
    count=$(grep -cEi "$regex" "$COMBINED" || true)
    if (( count > 0 )); then
        ERRORS_FOUND=$(( ERRORS_FOUND + count ))
        ERROR_SUMMARY+=$'\n'"  $label: $count hit(s)"
        # Show the first 3 matching lines so the user has something to
        # grep for - full verbose dump is gated on --verbose.
        while IFS= read -r line; do
            ERROR_SUMMARY+=$'\n'"      → $line"
        done < <(grep -Ei "$regex" "$COMBINED" | head -n 3)
    fi
done

# -- Report ---
echo ""
echo "-- Log scrape ---"
if (( ERRORS_FOUND == 0 )) && [[ "$RC" == "0" || "$RC" == "124" ]]; then
    echo "  clean: no matching log entries, rc within expected range"
    (( VERBOSE )) && { echo; echo "-- Captured output ---"; cat "$COMBINED"; }
    exit 0
fi

echo "  hits found:"
echo "$ERROR_SUMMARY"

if (( VERBOSE )); then
    echo
    echo "-- Full captured output ---"
    cat "$COMBINED"
fi

# Distinguish "process exited badly" from "process ran clean but log had
# warnings" in the exit code the caller sees.  Both fail (exit 4), but a
# CI summary can mine stderr to tell them apart.
if [[ "$RC" != "0" && "$RC" != "124" ]]; then
    echo ""
    echo "NOTE: also: launch rc=$RC (see exit-code table at top of script)"
fi

exit 4
