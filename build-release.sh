#!/usr/bin/env bash
# Build + package the PLAIN (non-AppImage) Linux release into dist/ as a zip.
#
#   dist/Nerevarine_Organizer-<version>-x86_64.zip
#     Nerevarine_Organizer-<version>-x86_64/
#       nerevarine_organizer            (the binary)
#       nerevarine_organizer.desktop
#       nerevarine_prefs.ini            (shipped defaults)
#       cystal_full_0.png               (icon)
#       translations/*.ini
#       README.md, LICENSE
#
# SAFETY: this packages an explicit ALLOW-LIST of files into a fresh staging
# dir - it never copies build/ or bin/ wholesale, and a final scan ABORTS if
# any personal state (modlist_*, loadorder_*, *.conf, log.txt, forbidden_*,
# backups) slipped into the payload.  Those files live next to the dev binary
# in bin/ and build/ and must NEVER reach a user.
#
# Usage:
#   ./build-release.sh                 # configure + build + package
#   ./build-release.sh --no-build      # package whatever's already in build/
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
BUILD_DIR="$REPO_ROOT/build"
DIST_DIR="$REPO_ROOT/dist"
DO_BUILD=1
[[ "${1:-}" == "--no-build" ]] && DO_BUILD=0

# Zip a directory (1st arg) into a zip file (2nd arg), staging-dir-relative so
# the archive contains the top folder, not absolute paths.  Prefers the `zip`
# CLI; falls back to Python's stdlib zipfile (always present) when absent.
make_zip() {
    local srcdir="$1" out="$2" parent base
    parent="$(dirname "$srcdir")"; base="$(basename "$srcdir")"
    rm -f "$out"
    if command -v zip >/dev/null 2>&1; then
        ( cd "$parent" && zip -rq "$out" "$base" )
    else
        python3 - "$parent" "$base" "$out" <<'PY'
import os, sys, zipfile
parent, base, out = sys.argv[1], sys.argv[2], sys.argv[3]
root = os.path.join(parent, base)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for dp, _, files in os.walk(root):
        for f in files:
            full = os.path.join(dp, f)
            z.write(full, os.path.relpath(full, parent))
PY
    fi
}

G='\033[0;32m'; Y='\033[0;33m'; R='\033[0m'
step() { echo -e "${G}>>> $*${R}"; }
die()  { echo -e "${R}$*${R}" >&2; exit 1; }

# Version from CMakeLists.txt
VERSION=$(grep -m1 'project(NerevarineOrganizer VERSION' "$REPO_ROOT/CMakeLists.txt" \
          | grep -oP '\d+\.\d+(?:\.\d+)?')
[[ -n "$VERSION" ]] || die "could not read version from CMakeLists.txt"
DIRNAME="Nerevarine_Organizer-${VERSION}-x86_64"
PKG="$REPO_ROOT/$DIRNAME"            # transient staging dir
ZIP="$DIST_DIR/${DIRNAME}.zip"
step "Version: $VERSION"

# Build
if (( DO_BUILD )); then
    step "Configuring CMake (Release)"
    cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    step "Compiling"
    cmake --build "$BUILD_DIR" -j"$(nproc)"
fi
[[ -x "$BUILD_DIR/nerevarine_organizer" ]] || die "binary missing: build first (drop --no-build)"

# Assemble from an explicit allow-list (never copy build/ or bin/ wholesale)
step "Assembling $DIRNAME"
rm -rf "$PKG"
mkdir -p "$PKG/translations"

# binary + shipped defaults
cp "$BUILD_DIR/nerevarine_organizer"            "$PKG/nerevarine_organizer"
cp "$REPO_ROOT/nerevarine_organizer.desktop"    "$PKG/nerevarine_organizer.desktop"
cp "$REPO_ROOT/nerevarine_prefs.ini"            "$PKG/nerevarine_prefs.ini"
cp "$REPO_ROOT/assets/icons/cystal_full_0.png"  "$PKG/cystal_full_0.png"
cp "$REPO_ROOT/README.md"                       "$PKG/README.md"
cp "$REPO_ROOT/LICENSE"                          "$PKG/LICENSE"

# ALL shipped translations, straight from the source tree (not build/, which
# may contain a user-run language file or stray state).
shopt -s nullglob
for ini in "$REPO_ROOT/translations/"*.ini; do
    cp "$ini" "$PKG/translations/"
done
shopt -u nullglob
[[ -f "$PKG/translations/english.ini" ]] || die "english.ini missing from package"

# bail if any personal/state file ended up in the payload
step "Scanning payload for personal state (must be none)"
LEAKS=$(find "$PKG" -type f \( \
        -iname 'modlist_*'      -o \
        -iname 'loadorder_*'    -o \
        -iname 'forbidden_*'    -o \
        -iname '*.conf'         -o \
        -iname 'log.txt'        -o \
        -iname '*.bak'          -o \
        -iname '*.bak.*'        -o \
        -iname '*.good.*'       -o \
        -iname '*.premanualedit' -o \
        -iname '*.zip' \) 2>/dev/null || true)
if [[ -n "$LEAKS" ]]; then
    echo -e "${R}personal/state files found in the release payload:${R}" >&2
    echo "$LEAKS" | sed 's/^/    /' >&2
    rm -rf "$PKG"
    die "refusing to ship a tarball containing user state"
fi
echo "  clean - no modlist/state files in payload."

# Zip into dist/
step "Packaging $ZIP"
mkdir -p "$DIST_DIR"
make_zip "$PKG" "$ZIP"
rm -rf "$PKG"

echo -e "${G}Done${R}"
ls -lh "$ZIP"
echo
echo "  Contents:"
( cd "$DIST_DIR" && python3 -c "import zipfile,sys; [print('    '+n) for n in zipfile.ZipFile(sys.argv[1]).namelist()]" "$ZIP" )
