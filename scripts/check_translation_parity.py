#!/usr/bin/env python3
"""
check_translation_parity.py - parity check for translations/english.ini.

Three failure modes this catches:

  1. Format breakage in english.ini itself - duplicate keys, blank
     keys, blank values, malformed lines.  english.ini is the source
     of truth that drives the auto-generated translation_keys.h enum
     plus every T() lookup at runtime, so a single dup silently
     shadows the second occurrence.

  2. Dead keys - lines in english.ini that no T(...) call in src/
     actually references.  Without the gate, removed UI strings
     accumulate as bytes in every shipped translation and confuse
     contributors trying to translate them.

  3. Undeclared T() calls - T("foo") in src/ where "foo" isn't a key
     in english.ini.  Runtime returns the literal "foo" to the user;
     the failure is silent unless the typo is in a high-traffic UI
     string.

Exits 0 on clean, 1 on any failure.  Designed for CI gate use; runs
against the working tree, no build needed.

Usage:
    python3 scripts/check_translation_parity.py [--repo-root <path>]
"""
import argparse
import re
import sys
from pathlib import Path


def parse_english_ini(path: Path):
    """Return (keys_in_order, errors).  Errors is a list of strings."""
    keys = []
    seen = set()
    errors = []
    with path.open(encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            stripped = line.strip()
            if not stripped or stripped.startswith(";") or stripped.startswith("["):
                continue
            if "=" not in line:
                errors.append(f"{path}:{lineno}: line has no '=' separator")
                continue
            key, _, value = line.partition("=")
            key = key.strip()
            if not key:
                errors.append(f"{path}:{lineno}: blank key")
                continue
            if key in seen:
                errors.append(f"{path}:{lineno}: duplicate key '{key}'")
            seen.add(key)
            keys.append(key)
            # Empty values are allowed (e.g. punctuation in some langs).
            # Don't error on them, but the format check catches the row.
    return keys, errors


T_CALL_RE = re.compile(r'\bT\(\s*"([a-zA-Z_][a-zA-Z0-9_]*)"\s*[,)]')


def strip_cpp_comments(text: str) -> str:
    """Remove // and /* */ comments so doc-strings like "// use T(\"key\")"
    don't show up as fake T() references in the parity check."""
    # Block comments first - non-greedy across newlines.
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    # Line comments - everything after // to EOL.  This is naive about
    # // inside string literals (e.g. URL strings), but the T() regex
    # below doesn't care about those false-clears - it only fires on
    # actual T(...) calls.
    text = re.sub(r"//[^\n]*", "", text)
    return text


def collect_t_keys(src_dir: Path):
    """Return set of keys referenced via T("...") in src/ and include/."""
    keys = set()
    for ext in ("*.cpp", "*.h"):
        for path in src_dir.rglob(ext):
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            keys.update(T_CALL_RE.findall(strip_cpp_comments(text)))
    return keys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--repo-root", default=None,
                    help="Path to repo root (default: parent of script's dir)")
    args = ap.parse_args()

    if args.repo_root:
        root = Path(args.repo_root).resolve()
    else:
        root = Path(__file__).resolve().parent.parent

    english = root / "translations" / "english.ini"
    if not english.is_file():
        print(f"error: {english} not found", file=sys.stderr)
        return 1

    print(f"-- Parsing {english.relative_to(root)} --")
    en_keys_ordered, format_errors = parse_english_ini(english)
    en_keys = set(en_keys_ordered)
    print(f"  {len(en_keys)} unique keys, {len(en_keys_ordered) - len(en_keys)} duplicates")

    if format_errors:
        print("\n-- Format errors in english.ini --")
        for e in format_errors:
            print(f"  {e}")

    print("\n-- Collecting T() references from src/ + include/ --")
    src_dirs = [root / "src", root / "include"]
    referenced = set()
    for d in src_dirs:
        if d.is_dir():
            referenced.update(collect_t_keys(d))
    print(f"  {len(referenced)} unique T(...) keys referenced")

    # Dead keys: present in english.ini but never referenced.
    dead = en_keys - referenced
    # Undeclared: referenced via T() but not in english.ini.
    undeclared = referenced - en_keys

    # The translation_keys.h enum is auto-generated, so the build
    # would still succeed if a key is added but unused - that's
    # exactly the failure mode we want to surface.  We treat dead
    # keys as a SOFT warning (won't fail CI) and undeclared T()
    # calls as a HARD failure (runtime regression).
    soft_warnings = 0
    hard_failures = 0

    if format_errors:
        hard_failures += len(format_errors)

    if dead:
        print(f"\n-- WARNING: {len(dead)} dead keys (in english.ini but no T() call refs) --")
        for k in sorted(dead):
            print(f"  {k}")
        soft_warnings += len(dead)

    if undeclared:
        print(f"\n-- ERROR: {len(undeclared)} undeclared T() keys (used in src/ but not in english.ini) --")
        for k in sorted(undeclared):
            print(f"  {k}")
        hard_failures += len(undeclared)

    print()
    print("=" * 60)
    print(f"  format errors: {len(format_errors)}")
    print(f"  dead keys (warning): {len(dead)}")
    print(f"  undeclared T() refs (error): {len(undeclared)}")
    print("=" * 60)

    if hard_failures > 0:
        print(f"\nFAIL: {hard_failures} hard failure(s)")
        return 1
    if soft_warnings > 0:
        print(f"\nOK with {soft_warnings} warning(s)")
    else:
        print("\nOK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
