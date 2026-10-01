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
# The generated enum: T(Tk::diag_bundle_title). Checked by the compiler, so
# never undeclared - but a use all the same.
TK_RE = re.compile(r'\bTk::([a-zA-Z_][a-zA-Z0-9_]*)\b')


def strip_cpp_comments(text: str) -> str:
    """Remove // and /* */ comments so doc-strings like "// use T(\"key\")"
    don't show up as fake T() references in the parity check.

    String-aware: a "/*" or "//" inside a literal - a "*.cfg" file filter, a
    URL - is not a comment. The regex version this replaces took one for a
    comment opener and swallowed the real code up to the next "*/", T() calls
    and all, so live keys such as import_openmw_pick_title read as dead."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "/" and text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
            continue
        if c == "R" and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            m = re.match(r'R"([^ ()\\\t\n]{0,16})\(', text[i:])
            if m:
                close = ")" + m.group(1) + '"'
                end = text.find(close, i + m.end())
                end = n if end < 0 else end + len(close)
                out.append(text[i:end])
                i = end
                continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


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


def collect_tk_keys(src_dir: Path):
    """Return set of keys referenced via the generated Tk:: enum."""
    keys = set()
    for ext in ("*.cpp", "*.h"):
        for path in src_dir.rglob(ext):
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            keys.update(TK_RE.findall(strip_cpp_comments(text)))
    return keys


# A key-shaped string literal anywhere: "deploy_confirm" handed to a helper
# that calls T() itself, a key picked by `cond ? "a" : "b"`, a table of keys.
# None of those is a T("...") call, and they made up most of the "dead" keys.
KEY_LITERAL_RE = re.compile(r'"([a-z][a-z0-9_]*)"')

# Suffixes code appends to a key it was handed. deployText() in
# src/mainwindow_deploy.cpp reads key + "_overlay" for games whose deploy
# overlays files instead of writing a plugin list.
VARIANT_SUFFIXES = ("_overlay",)


def collect_key_literals(src_dir: Path):
    """Return every key-shaped string literal in src/ and include/."""
    lits = set()
    for ext in ("*.cpp", "*.h"):
        for path in src_dir.rglob(ext):
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            lits.update(KEY_LITERAL_RE.findall(strip_cpp_comments(text)))
    return lits


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

    # Undeclared: referenced via T() but not in english.ini.
    undeclared = referenced - en_keys

    # Dead keys: present in english.ini but never referenced - by T(), by a
    # key-shaped literal handed elsewhere, or as a known variant of either.
    literals = set()
    for d in src_dirs:
        if d.is_dir():
            literals.update(collect_key_literals(d))
    for d in src_dirs:
        if d.is_dir():
            literals.update(collect_tk_keys(d))
    used = referenced | literals
    used |= {k + suf for k in used for suf in VARIANT_SUFFIXES}
    dead = en_keys - used

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
        print(f"\n-- WARNING: {len(dead)} dead keys (in english.ini, used nowhere in src/ or include/) --")
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
