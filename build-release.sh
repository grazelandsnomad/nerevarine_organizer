#!/usr/bin/env bash
# Build and package a release tarball matching the layout:
#   Nerevarine_Organizer-<version>-x86_64/
#     nerevarine_organizer
#     nerevarine_organizer.desktop
#     nerevarine_prefs.ini
#     cystal_full_0.png
#     translations/
#       english.ini

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
BUILD_DIR="$REPO_ROOT/build"

G='\033[0;32m'; R='\033[0m'
step() { echo -e "${G}-- $* ${R}"; }

# -- 1. Read version from CMakeLists.txt ---
VERSION=$(grep -m1 'project(NerevarineOrganizer VERSION' "$REPO_ROOT/CMakeLists.txt" \
          | grep -oP '\d+\.\d+\.\d+')
DIRNAME="Nerevarine_Organizer-${VERSION}-x86_64"
TARBALL="$REPO_ROOT/${DIRNAME}.tar.gz"

step "Version: $VERSION"

# -- 2. Build ---
step "Configuring CMake"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release

step "Compiling"
cmake --build "$BUILD_DIR" -j"$(nproc)"

# -- 3. Assemble package directory ---
step "Assembling $DIRNAME"
PKG="$REPO_ROOT/$DIRNAME"
rm -rf "$PKG"
mkdir -p "$PKG/translations"

cp "$BUILD_DIR/nerevarine_organizer"            "$PKG/nerevarine_organizer"
cp "$REPO_ROOT/nerevarine_organizer.desktop"    "$PKG/nerevarine_organizer.desktop"
cp "$BUILD_DIR/nerevarine_prefs.ini"            "$PKG/nerevarine_prefs.ini"
cp "$REPO_ROOT/assets/icons/cystal_full_0.png"  "$PKG/cystal_full_0.png"
cp "$BUILD_DIR/translations/english.ini"        "$PKG/translations/english.ini"

# -- 4. Archive ---
step "Creating $TARBALL"
(cd "$REPO_ROOT" && tar -czf "$TARBALL" "$DIRNAME")
rm -rf "$PKG"

echo -e "${G}✓ Done: $TARBALL${R}"
ls -lh "$TARBALL"
