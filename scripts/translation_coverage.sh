#!/usr/bin/env bash
# ---
# translation_coverage.sh - audit non-English .ini files against english.ini
#
# Outputs, for every translation file under translations/:
#   · coverage summary  ("spanish.ini: 217/464 keys, 46.8% - 247 missing")
#   · per-language stub at translations/_missing/<lang>.ini with untranslated
#     keys prefilled with their English value, each prefixed with a
#     "; TODO: translate" comment so contributors can grep/fill trivially.
#
# The stubs are regenerated every run - they are NOT meant to be committed.
# Contributors copy the lines they translate into their real <lang>.ini and
# delete the corresponding TODO entry from the stub.
#
# Usage:
#   scripts/translation_coverage.sh              # audit + write stubs
#   scripts/translation_coverage.sh --summary    # audit only, no stubs
# ---

set -e

ROOT="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
TRANS="$ROOT/translations"
ENGLISH="$TRANS/english.ini"
STUB_DIR="$TRANS/_missing"

if [[ ! -f "$ENGLISH" ]]; then
    echo "error: $ENGLISH not found" >&2
    exit 1
fi

WRITE_STUBS=1
if [[ "${1:-}" == "--summary" ]]; then
    WRITE_STUBS=0
fi

# Extract KEY from a line like `key=value` (ignores comments and section
# headers).  Keys are the part before the first `=`.
extract_keys() {
    awk -F= '
        /^[[:space:]]*;/ { next }         # skip comments
        /^[[:space:]]*\[/ { next }        # skip section headers
        /^[[:space:]]*$/ { next }         # skip blank lines
        /=/ {
            key = $1
            sub(/^[[:space:]]+/, "", key)
            sub(/[[:space:]]+$/, "", key)
            if (key != "") print key
        }
    ' "$1"
}

# Emit a stub line for KEY with the English value, wrapped with a TODO
# comment so the contributor knows what to translate.
emit_stub_line() {
    local key="$1"
    local en
    # Grab the English value verbatim (everything after the first `=`).
    en=$(awk -F= -v k="$key" '
        /^[[:space:]]*;/ { next }
        /^[[:space:]]*\[/ { next }
        {
            kk = $1
            sub(/^[[:space:]]+/, "", kk)
            sub(/[[:space:]]+$/, "", kk)
            if (kk == k) {
                # everything after the first '='
                idx = index($0, "=")
                print substr($0, idx + 1)
                exit
            }
        }' "$ENGLISH")
    printf '; TODO: translate\n%s=%s\n\n' "$key" "$en"
}

if [[ $WRITE_STUBS -eq 1 ]]; then
    rm -rf "$STUB_DIR"
    mkdir -p "$STUB_DIR"
fi

# Build the canonical key list from english.ini once.
EN_KEYS_FILE="$(mktemp)"
trap 'rm -f "$EN_KEYS_FILE"' EXIT
extract_keys "$ENGLISH" | sort -u > "$EN_KEYS_FILE"
EN_TOTAL=$(wc -l < "$EN_KEYS_FILE")

printf '\n%-28s %10s %8s %11s\n' "Language" "Coverage" "Percent" "Missing"
printf '%-28s %10s %8s %11s\n' "---" "---" "---" "---"

for f in "$TRANS"/*.ini; do
    [[ "$(basename "$f")" == "english.ini" ]] && continue
    lang=$(basename "$f" .ini)

    lang_keys=$(mktemp)
    extract_keys "$f" | sort -u > "$lang_keys"

    # Present = keys in this language that are also in English (protects
    # against stale keys).  Missing = English keys not present here.
    present=$(comm -12 "$EN_KEYS_FILE" "$lang_keys" | wc -l)
    missing=$(( EN_TOTAL - present ))
    pct=$(awk -v p="$present" -v t="$EN_TOTAL" 'BEGIN{ printf "%.1f", (t>0? p*100/t : 0) }')

    printf '%-28s %10s %7s%%  %10d\n' "$lang" "$present/$EN_TOTAL" "$pct" "$missing"

    if [[ $WRITE_STUBS -eq 1 && $missing -gt 0 ]]; then
        stub="$STUB_DIR/$lang.ini"
        {
            printf '; Auto-generated stub: keys missing from translations/%s.ini\n' "$lang"
            printf '; Fill in the translations, then copy the lines into that file.\n'
            printf '; Generated: %s\n\n' "$(date -Iseconds)"
        } > "$stub"
        comm -23 "$EN_KEYS_FILE" "$lang_keys" | while read -r key; do
            emit_stub_line "$key" >> "$stub"
        done
    fi

    rm -f "$lang_keys"
done

echo
if [[ $WRITE_STUBS -eq 1 ]]; then
    echo "Stubs written to: $STUB_DIR/"
    echo "(Not meant to be committed - regenerate any time with this script.)"
else
    echo "Run without --summary to regenerate stubs under $STUB_DIR/"
fi
