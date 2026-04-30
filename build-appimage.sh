#!/usr/bin/env bash
# Build a self-contained AppImage that bundles Qt and all shared libs,
# then patches every ELF inside to declare only glibc >= 2.17 so the
# AppImage runs on Steam Deck (glibc 2.41) and most other distros.
#
# Usage:
#   ./build-appimage.sh
#
# Outputs:
#   NerevarineOrganizer-<version>-x86_64.AppImage  (version from APPIMAGE_VERSION below)
#
# Requirements: cmake, a C++23-capable compiler, Qt6 dev files,
#               imagemagick, python3.
#   linuxdeploy, linuxdeploy-plugin-qt, and appimagetool are downloaded
#   automatically into build-appimage/tools/.

set -euo pipefail

# Stamped into the AppImage filename. Bump in lockstep with the release;
# CI consumes the file via a version-agnostic glob.
APPIMAGE_VERSION="0.3"

REPO_ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
BUILD_DIR="$REPO_ROOT/build-appimage"
APPDIR="$BUILD_DIR/AppDir"
TOOLS_DIR="$BUILD_DIR/tools"

G='\033[0;32m'; R='\033[0m'
step() { echo -e "${G}-- $* ${R}"; }

# -- 1. Build ---
step "Configuring CMake"
# Pass CC/CXX as -DCMAKE_*_COMPILER so a stale CMakeCache.txt from a
# previous run can't pin the compiler.
CMAKE_COMPILER_ARGS=()
[[ -n "${CC:-}"  ]] && CMAKE_COMPILER_ARGS+=("-DCMAKE_C_COMPILER=$CC")
[[ -n "${CXX:-}" ]] && CMAKE_COMPILER_ARGS+=("-DCMAKE_CXX_COMPILER=$CXX")
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" \
    "${CMAKE_COMPILER_ARGS[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

step "Compiling"
cmake --build "$BUILD_DIR" -j"$(nproc)"

# -- 2. Install into AppDir ---
step "Installing into AppDir"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"

cp "$REPO_ROOT/nerevarine_organizer.desktop" "$APPDIR/nerevarine_organizer.desktop"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
convert "$REPO_ROOT/assets/icons/cystal_full_0.png" \
    -resize 256x256! "$APPDIR/nerevarine_organizer.png"

# -- 3. Fetch tools ---
step "Downloading linuxdeploy tools (if needed)"
mkdir -p "$TOOLS_DIR"

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"
APPIMAGETOOL="$TOOLS_DIR/appimagetool-x86_64.AppImage"

_fetch() {
    local dst="$1" url="$2"
    [[ -x "$dst" ]] && return
    curl -Lo "$dst" "$url"
    chmod +x "$dst"
}

_fetch "$LINUXDEPLOY" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
_fetch "$LINUXDEPLOY_QT" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
_fetch "$APPIMAGETOOL" \
    "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"

# -- 4. Extract AppImage tools and patch their bundled strip ---
# linuxdeploy and the qt plugin bundle an old strip that chokes on
# SHT_RELR relocations on modern ELF; replace with the system strip.
_patch_ld() {
    local appimage="$1" dest="$2"
    if [[ ! -d "$dest" ]] || [[ "$appimage" -nt "$dest/AppRun" ]]; then
        rm -rf "$TOOLS_DIR/squashfs-root" "$dest"
        (cd "$TOOLS_DIR" && "$appimage" --appimage-extract >/dev/null 2>&1)
        mv "$TOOLS_DIR/squashfs-root" "$dest"
    fi
    cp "$(command -v strip)" "$dest/usr/bin/strip"
}

LD_EXTRACTED="$TOOLS_DIR/ld-squash"
LD_QT_EXTRACTED="$TOOLS_DIR/ld-qt-squash"
AT_EXTRACTED="$TOOLS_DIR/appimagetool-squash"

_patch_ld "$LINUXDEPLOY"    "$LD_EXTRACTED"
_patch_ld "$LINUXDEPLOY_QT" "$LD_QT_EXTRACTED"

# appimagetool doesn't need strip patching but does need extraction (no FUSE in
# many build environments).
if [[ ! -d "$AT_EXTRACTED" ]] || [[ "$APPIMAGETOOL" -nt "$AT_EXTRACTED/AppRun" ]]; then
    rm -rf "$TOOLS_DIR/squashfs-root" "$AT_EXTRACTED"
    (cd "$TOOLS_DIR" && "$APPIMAGETOOL" --appimage-extract >/dev/null 2>&1)
    mv "$TOOLS_DIR/squashfs-root" "$AT_EXTRACTED"
fi

# -- 5. Populate AppDir (Qt + all deps) - no AppImage yet ---
step "Populating AppDir with Qt and dependencies"

export QMAKE
QMAKE="$(command -v qmake6 || command -v qmake || true)"
if [[ -z "$QMAKE" ]]; then
    for candidate in /usr/lib/qt6/bin/qmake /usr/lib/qt/bin/qmake; do
        [[ -x "$candidate" ]] && { QMAKE="$candidate"; break; }
    done
fi
[[ -z "$QMAKE" ]] && { echo "ERROR: qmake not found."; exit 1; }
echo "  qmake: $QMAKE"

export EXTRA_QT_PLUGINS="platforms;platformthemes;styles;xcbglintegrations"

FAKE_BIN="$TOOLS_DIR/fake-bin"
mkdir -p "$FAKE_BIN"
cat > "$FAKE_BIN/linuxdeploy-plugin-qt-x86_64.AppImage" << WRAPPER
#!/bin/bash
exec "$LD_QT_EXTRACTED/AppRun" "\$@"
WRAPPER
chmod +x "$FAKE_BIN/linuxdeploy-plugin-qt-x86_64.AppImage"
export PATH="$FAKE_BIN:$PATH"

# Run linuxdeploy without --output so it only populates AppDir
"$LD_EXTRACTED/AppRun" \
    --appdir "$APPDIR" \
    --plugin qt \
    --desktop-file "$APPDIR/nerevarine_organizer.desktop" \
    --icon-file    "$APPDIR/nerevarine_organizer.png"

# Bundle Noto Color Emoji so the AppImage renders coloured glyphs even on
# hosts without it installed (Steam Deck, minimal Debian, etc.). main.cpp
# loads it via QFontDatabase::addApplicationFont at startup.
step "Bundling Noto Color Emoji"
EMOJI_FONT=""
for cand in /usr/share/fonts/noto/NotoColorEmoji.ttf \
            /usr/share/fonts/truetype/noto/NotoColorEmoji.ttf \
            /usr/share/fonts/google-noto-emoji-color-fonts/NotoColorEmoji.ttf \
            /usr/share/fonts/NotoColorEmoji.ttf; do
    [[ -f "$cand" ]] && { EMOJI_FONT="$cand"; break; }
done
if [[ -n "$EMOJI_FONT" ]]; then
    mkdir -p "$APPDIR/usr/share/fonts"
    cp "$EMOJI_FONT" "$APPDIR/usr/share/fonts/NotoColorEmoji.ttf"
    echo "  bundled: $EMOJI_FONT"
else
    echo "  WARNING: NotoColorEmoji.ttf not found on host; AppImage will fall" \
         "back to whatever the user's system fontconfig finds."
fi

# -- 6. Clear glibc version requirements (≥ 2.42 → no version constraint) ----
step "Clearing GLIBC_2.42+ symbol version requirements"
python3 "$REPO_ROOT/scripts/patch_glibc.py" \
    --max-minor 41 \
    "$APPDIR"

# -- 6b. Build glibc compat shim and wrap AppRun with LD_PRELOAD ---
# free_aligned_sized / free_sized were added in glibc 2.43 and are referenced
# by libglib-2.0 on bleeding-edge distros. The patcher renames the version
# tag down to 2.41 but the symbol still has to resolve, so ship our own.
step "Building glibc compat shim (free_aligned_sized, free_sized)"
mkdir -p "$APPDIR/usr/lib"
gcc -shared -fPIC -O2 \
    -Wl,--version-script="$REPO_ROOT/compat/glibc_compat.ver" \
    -Wl,-soname,libglibc_compat.so \
    -o "$APPDIR/usr/lib/libglibc_compat.so" \
    "$REPO_ROOT/compat/glibc_compat.c"

# Wrap linuxdeploy's AppRun so LD_PRELOAD picks up the shim before anything
# else loads. Keep the original AppRun intact so its Qt / xdg env plumbing
# still runs.
if [[ -f "$APPDIR/AppRun" && ! -f "$APPDIR/AppRun.orig" ]]; then
    mv "$APPDIR/AppRun" "$APPDIR/AppRun.orig"
fi
cat > "$APPDIR/AppRun" << 'APPRUN'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export LD_PRELOAD="${HERE}/usr/lib/libglibc_compat.so${LD_PRELOAD:+:${LD_PRELOAD}}"
# Prefer wayland, fall back to xcb if the bundled wayland plugin can't
# load. Only set when the env doesn't already pick a platform.
: "${QT_QPA_PLATFORM:=wayland;xcb}"
export QT_QPA_PLATFORM
exec "${HERE}/AppRun.orig" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

# -- 7. Package with appimagetool ---
step "Packaging AppImage"
DEST="$REPO_ROOT/NerevarineOrganizer-${APPIMAGE_VERSION}-x86_64.AppImage"
rm -f "$DEST"
ARCH=x86_64 "$AT_EXTRACTED/AppRun" --no-appstream "$APPDIR" "$DEST"

echo -e "${G}✓ AppImage ready: $DEST${R}"

# Show actual minimum glibc the patched image requires
MAX_VER=$(find "$APPDIR" -type f ! -l -exec sh -c '
    readelf -V "$1" 2>/dev/null | grep -oP "GLIBC_\K[\d.]+"
' _ {} \; 2>/dev/null | sort -V | tail -1 || true)
echo ""
echo "  Bundled Qt + all dependencies - no system Qt required."
[[ -n "$MAX_VER" ]] && echo "  Minimum glibc after patching: $MAX_VER"
echo ""
