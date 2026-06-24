#!/usr/bin/env bash
#
# gen_translation_enum.sh - emit a C++ enum class Tk from english.ini.
#
# Generates a header with one enum entry per translation key found in
# `translations/english.ini`, plus a constexpr name-lookup table.  The
# header is consumed by translator.h to provide a `T(Tk::some_key)`
# overload that fails to compile on misspelled keys -- the enum is the
# source of truth for "what keys must every translation file contain".
#
# Idempotent: writes the output only when the contents would change, so
# CMake doesn't trigger unnecessary rebuilds.
#
# Usage:
#   gen_translation_enum.sh <english.ini> <output-header>
#
# Both arguments are required; CMake supplies absolute paths.

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <english.ini> <output-header>" >&2
    exit 64
fi

INPUT="$1"
OUTPUT="$2"

if [[ ! -r "$INPUT" ]]; then
    echo "$0: cannot read input: $INPUT" >&2
    exit 66
fi

# Extract keys: lines matching ^[A-Za-z_][A-Za-z0-9_]*= -- filters out
# `[Section]` headers, `;`-comments, blank lines, and any malformed entries.
# Stable, deduped order: keys appear in the order they're declared in the
# file, but a key seen twice is kept only once (first occurrence wins).
mapfile -t KEYS < <(awk -F= '
    /^[A-Za-z_][A-Za-z0-9_]*=/ {
        if (!(seen[$1]++)) print $1
    }
' "$INPUT")

if [[ ${#KEYS[@]} -eq 0 ]]; then
    echo "$0: no keys found in $INPUT" >&2
    exit 65
fi

TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT

{
    cat <<HEADER
// AUTO-GENERATED from translations/english.ini by
// scripts/gen_translation_enum.sh.  DO NOT EDIT BY HAND.
//
// Strongly-typed translation-key enum.  Use \`T(Tk::some_key)\` instead
// of \`T("some_key")\` so misspelled keys are a compile error rather
// than a runtime fall-through to the literal key string.  The enum is
// regenerated whenever english.ini changes; foreign translations are
// validated at runtime against the same key set via
// \`Translator::missingKeys()\`.

#pragma once

#include <cstddef>

enum class Tk : int {
HEADER

    for key in "${KEYS[@]}"; do
        printf '    %s,\n' "$key"
    done

    cat <<MIDDLE
};

inline constexpr const char *tkName(Tk t) noexcept
{
    // Indexed by Tk's integer value; order MUST match the enum above.
    constexpr const char *kNames[] = {
MIDDLE

    for key in "${KEYS[@]}"; do
        printf '        "%s",\n' "$key"
    done

    cat <<FOOTER
    };
    return kNames[static_cast<std::size_t>(t)];
}

inline constexpr std::size_t kTkCount = ${#KEYS[@]};
FOOTER
} > "$TMP"

# Only overwrite when contents actually changed -- keeps CMake's
# dependency tracking happy and avoids spurious rebuilds.
if [[ -r "$OUTPUT" ]] && cmp -s "$TMP" "$OUTPUT"; then
    exit 0
fi

mkdir -p "$(dirname "$OUTPUT")"
mv "$TMP" "$OUTPUT"
trap - EXIT
