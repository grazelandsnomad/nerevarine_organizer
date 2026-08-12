#!/usr/bin/env bash
# ---
# prune-backups.sh - list, diff, and prune safefs::snapshotBackup files.
#
# The app writes ".bak.YYYYMMDD-HHMMSS" siblings next to every file it
# snapshots (see src/safe_fs.cpp).  snapshotBackup auto-rotates to keep=5
# in the common path, but ad-hoc `cp` during debugging, older builds, or a
# crashed run bypassing the rotation can leave piles.  This script covers
# those cases.
#
# Backups are matched by the exact pattern:
#   <live-file-basename>.bak.YYYYMMDD-HHMMSS
# - 8 digits + "-" + 6 digits, nothing else.  That's deliberately narrow so
# stray files named "something.bak.final" or "x.bak.tmp" never get nuked.
#
# Usage:
#   prune-backups.sh list <dir>
#       group every *.bak.YYYYMMDD-HHMMSS file under <dir> by the live
#       filename it shadows, show timestamp + size per snapshot.
#
#   prune-backups.sh diff <live-file>
#       diff <live-file> against its most recent snapshot in the same
#       directory.  Exits 0 whether diff finds changes or not - mirrors
#       `git diff --no-exit-code`, since "different" is the expected case.
#
#   prune-backups.sh prune <dir> [--keep N] [--apply]
#       keep the newest N snapshots per live file (default N=5, matching
#       safefs::snapshotBackup's default), delete older ones.
#       DRY-RUN by default - shows what WOULD be deleted.  Pass --apply
#       to actually unlink.  Non-backup files in <dir> are ignored.
#
#   prune-backups.sh help
#
# Exit codes:
#   0  success (including "dry-run, nothing applied")
#   1  bad arguments / unknown subcommand
#   2  path doesn't exist
#   3  prune target wasn't a directory / no backups found where expected
# ---

set -eu

# -- Pattern + helpers ---

# Globbing pattern for the backup suffix.  Exactly 8 digits, a dash, then
# exactly 6 digits.  Used both in find -iregex-equivalent and bash globs.
BAK_REGEX='.*\.bak\.[0-9]{8}-[0-9]{6}$'
BAK_GLOB='.bak.????????-??????'

# Print with an "--" prefix for dry-run simulated-delete lines so grep from
# shell scripts can distinguish real output.
say_dry()  { echo "[dry-run] $*"; }
say_real() { echo "          $*"; }
die()      { echo "error: $*" >&2; exit "${2:-1}"; }

usage() {
    # Extract the usage block from the header comment so there's one source
    # of truth.  Stop at "# Exit codes:" so inter-subcommand blank-comment
    # separators don't truncate the help output early.
    sed -n '/^# Usage:/,/^# Exit codes:/p' "$0" \
        | sed -e '$d' -e 's/^# \{0,1\}//'
    exit "${1:-1}"
}

# Split a backup filename into "live-basename" + "timestamp".
# Input:  modlist_morrowind.txt.bak.20260418-224800
# Output: modlist_morrowind.txt|20260418-224800
split_backup_name() {
    local full="$1"
    # Strip everything up to and including ".bak." - what's left is the stamp.
    local stamp="${full##*.bak.}"
    # The live basename is everything BEFORE ".bak.<stamp>".
    local base="${full%.bak.$stamp}"
    echo "$base|$stamp"
}

# Human-readable size (uses numfmt if available, else raw bytes).
human_size() {
    local bytes="$1"
    if command -v numfmt >/dev/null 2>&1; then
        numfmt --to=iec --suffix=B --format="%8.1f" "$bytes" 2>/dev/null || echo "${bytes}B"
    else
        echo "${bytes}B"
    fi
}

# Print the age of a file relative to now as "Nd Nh Nm ago" or similar.
# Works off `stat -c %Y` (Linux).  Degrades to raw mtime on non-GNU stat.
file_age() {
    local file="$1"
    local mtime
    mtime=$(stat -c %Y "$file" 2>/dev/null) || { stat -f %m "$file" 2>/dev/null || echo ?; return; }
    local now diff
    now=$(date +%s)
    diff=$(( now - mtime ))
    if   (( diff < 60 ));      then echo "${diff}s ago"
    elif (( diff < 3600 ));    then echo "$(( diff / 60 ))m ago"
    elif (( diff < 86400 ));   then echo "$(( diff / 3600 ))h ago"
    else                            echo "$(( diff / 86400 ))d ago"
    fi
}

# Enumerate *.bak.YYYYMMDD-HHMMSS files directly under <dir> (NOT recursive -
# snapshotBackup always writes next to the live file, so recursing would
# pick up unrelated things).  Null-delimited so filenames with spaces survive.
list_backup_files() {
    local dir="$1"
    # -maxdepth 1 keeps the scan surgical.  -regextype posix-extended makes
    # the digit quantifiers work reliably across findutils versions.
    find "$dir" -maxdepth 1 -type f -regextype posix-extended -regex "$BAK_REGEX" -print0
}

# -- Subcommand: list ---

cmd_list() {
    local dir="${1:-}"
    [[ -n "$dir" ]] || usage
    [[ -e "$dir" ]] || die "path does not exist: $dir" 2
    [[ -d "$dir" ]] || die "not a directory: $dir" 3

    echo "Backups under: $dir"
    echo

    # Collect into associative arrays keyed by live basename.
    declare -A group_count group_size
    declare -A group_files

    while IFS= read -r -d '' path; do
        local fname="${path##*/}"
        local parts base
        parts=$(split_backup_name "$fname")
        base="${parts%%|*}"

        local size
        size=$(stat -c %s "$path" 2>/dev/null || echo 0)

        group_count["$base"]=$(( ${group_count["$base"]:-0} + 1 ))
        group_size["$base"]=$(( ${group_size["$base"]:-0} + size ))
        # Append path + newline to the running string for this group.
        group_files["$base"]="${group_files["$base"]:-}${path}"$'\n'
    done < <(list_backup_files "$dir")

    if [[ ${#group_count[@]} -eq 0 ]]; then
        echo "(no snapshot backups found)"
        return 0
    fi

    # Print each group sorted by base name, with each group's snapshots
    # sorted newest-first (timestamp is lexicographically sortable).
    local base
    for base in $(printf '%s\n' "${!group_count[@]}" | sort); do
        printf '  %s  (%d snapshot(s), total %s)\n' \
            "$base" "${group_count[$base]}" "$(human_size "${group_size[$base]}")"

        # reverse sort newest first
        local path
        while IFS= read -r path; do
            [[ -z "$path" ]] && continue
            local sz age
            sz=$(stat -c %s "$path" 2>/dev/null || echo 0)
            age=$(file_age "$path")
            printf '      %s   %s   %s\n' \
                "${path##*.bak.}" \
                "$(human_size "$sz")" \
                "$age"
        done < <(printf '%s' "${group_files[$base]}" | sort -r)
        echo
    done
}

# -- Subcommand: diff ---

cmd_diff() {
    local live="${1:-}"
    [[ -n "$live" ]] || usage
    [[ -e "$live" ]] || die "live file does not exist: $live" 2
    [[ -f "$live" ]] || die "not a regular file: $live" 3

    local dir base newest
    dir="$(dirname "$live")"
    base="$(basename "$live")"

    # Newest matching backup in the same directory.
    newest=$(list_backup_files "$dir" \
             | xargs -0 -I{} sh -c 'echo "{}"' \
             | grep -F "/$base.bak." \
             | sort -r \
             | head -n 1 || true)

    if [[ -z "$newest" ]]; then
        die "no backups found for $live in $dir" 3
    fi

    echo "diff: $newest"
    echo "vs  : $live"
    echo
    # diff returns 1 when files differ - we don't want set -e to kill us.
    diff -u "$newest" "$live" || true
}

# -- Subcommand: prune ---

cmd_prune() {
    local dir="" keep=5 apply=0
    while (( $# )); do
        case "$1" in
            --keep)   shift; keep="${1:-}"; [[ "$keep" =~ ^[0-9]+$ ]] || die "--keep expects an integer" ;;
            --apply)  apply=1 ;;
            -h|--help) usage 0 ;;
            -*)       die "unknown flag: $1" ;;
            *)
                [[ -z "$dir" ]] || die "only one directory argument permitted"
                dir="$1"
                ;;
        esac
        shift
    done

    [[ -n "$dir" ]] || usage
    [[ -e "$dir" ]] || die "path does not exist: $dir" 2
    [[ -d "$dir" ]] || die "not a directory: $dir" 3

    echo "Pruning backups under: $dir"
    echo "Keep per live file:    $keep"
    if (( apply )); then echo "Mode: APPLY (real delete)"
    else                  echo "Mode: DRY-RUN (pass --apply to actually delete)"; fi
    echo

    declare -A group_files
    while IFS= read -r -d '' path; do
        local parts base
        parts=$(split_backup_name "${path##*/}")
        base="${parts%%|*}"
        group_files["$base"]="${group_files["$base"]:-}${path}"$'\n'
    done < <(list_backup_files "$dir")

    if [[ ${#group_files[@]} -eq 0 ]]; then
        echo "(no snapshot backups found)"
        return 0
    fi

    local total_deleted=0 total_bytes=0

    local base
    for base in $(printf '%s\n' "${!group_files[@]}" | sort); do
        # Sort newest first, drop the first $keep, delete the rest.
        local sorted_paths
        sorted_paths=$(printf '%s' "${group_files[$base]}" | sort -r)

        local line_count
        line_count=$(printf '%s\n' "$sorted_paths" | grep -c .)
        if (( line_count <= keep )); then
            printf '  %s: %d snapshot(s), within keep window - skipping\n' \
                "$base" "$line_count"
            continue
        fi

        printf '  %s: %d snapshot(s), dropping %d\n' \
            "$base" "$line_count" "$(( line_count - keep ))"

        local n=0 path
        while IFS= read -r path; do
            [[ -z "$path" ]] && continue
            n=$(( n + 1 ))
            if (( n <= keep )); then
                printf '      keep: %s\n' "${path##*.bak.}"
            else
                local sz
                sz=$(stat -c %s "$path" 2>/dev/null || echo 0)
                total_bytes=$(( total_bytes + sz ))
                total_deleted=$(( total_deleted + 1 ))
                if (( apply )); then
                    if rm -- "$path"; then
                        say_real "delete: ${path##*.bak.}   ($(human_size "$sz"))"
                    else
                        say_real "FAILED delete: $path" >&2
                    fi
                else
                    say_dry  "delete: ${path##*.bak.}   ($(human_size "$sz"))"
                fi
            fi
        done <<< "$sorted_paths"
    done

    echo
    if (( apply )); then
        printf 'Deleted %d snapshot(s), freed %s\n' \
            "$total_deleted" "$(human_size "$total_bytes")"
    else
        printf 'Would delete %d snapshot(s), freeing %s  (pass --apply)\n' \
            "$total_deleted" "$(human_size "$total_bytes")"
    fi
}

# -- Dispatch ---

cmd="${1:-help}"
shift || true
case "$cmd" in
    list)    cmd_list  "$@" ;;
    diff)    cmd_diff  "$@" ;;
    prune)   cmd_prune "$@" ;;
    help|-h|--help) usage 0 ;;
    *)       echo "unknown subcommand: $cmd" >&2; usage ;;
esac
