#!/usr/bin/env bash
# self-contained AppImage: bundles Qt + libs, patches every ELF down to
# glibc 2.17 so it runs on Steam Deck etc.
# needs cmake, c++23 compiler, qt6 dev, imagemagick, python3.
# linuxdeploy/-qt/appimagetool pulled into build-appimage/tools/.

set -euo pipefail

# goes into the filename, bump with the release
APPIMAGE_VERSION="0.6"

REPO_ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
BUILD_DIR="$REPO_ROOT/build-appimage"
APPDIR="$BUILD_DIR/AppDir"
TOOLS_DIR="$BUILD_DIR/tools"
DIST_DIR="$REPO_ROOT/dist"

G='\033[0;32m'; R='\033[0m'
step() { echo -e "${G}-- $* ${R}"; }

# Zip a single file into a zip (flat, no leading dirs).  Prefers the `zip` CLI;
# falls back to Python's stdlib when absent.
zip_file() {
    local src="$1" out="$2" dir base
    dir="$(dirname "$src")"; base="$(basename "$src")"
    rm -f "$out"
    if command -v zip >/dev/null 2>&1; then
        ( cd "$dir" && zip -q "$out" "$base" )
    else
        python3 - "$src" "$base" "$out" <<'PY'
import sys, zipfile
src, base, out = sys.argv[1], sys.argv[2], sys.argv[3]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(src, base)
PY
    fi
}

# Build
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

# Install into AppDir
step "Installing into AppDir"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"

cp "$REPO_ROOT/nerevarine_organizer.desktop" "$APPDIR/nerevarine_organizer.desktop"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
convert "$REPO_ROOT/assets/icons/cystal_full_0.png" \
    -resize 256x256! "$APPDIR/nerevarine_organizer.png"

# Fetch tools
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

# Extract AppImage tools and patch their bundled strip
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

# Populate AppDir (Qt + all deps) - no AppImage yet
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

# Clear glibc version requirements (≥ 2.42 → no version constraint)
step "Clearing GLIBC_2.42+ symbol version requirements"
python3 "$REPO_ROOT/scripts/patch_glibc.py" \
    --max-minor 41 \
    "$APPDIR"

# Build glibc compat shim and wrap AppRun with LD_PRELOAD
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
# Write a per-launch fontconfig config that adds our bundled font dir
# (NotoColorEmoji.ttf) and then includes the system config.  Qt's emoji
# fallback goes through fontconfig, not QFontDatabase, so addApplicationFont
# alone is not enough to make colour glyphs render in toolbar buttons.
_FC_CONF="${XDG_RUNTIME_DIR:-/tmp}/nerev-fc-$$.conf"
cat > "$_FC_CONF" << FCEOF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>${HERE}/usr/share/fonts</dir>
  <include ignore_missing="yes">/etc/fonts/fonts.conf</include>
</fontconfig>
FCEOF
export FONTCONFIG_FILE="$_FC_CONF"
exec "${HERE}/AppRun.orig" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

# don't ship a build needing a newer glibc than patch_glibc.py targets (2.41),
# or it breaks on Fedora / Steam Deck with "GLIBC_2.43 not found"
step "Verifying glibc portability (target <= 2.41)"
MAX_VER=$(find "$APPDIR" -type f ! -l -exec sh -c '
    readelf -V "$1" 2>/dev/null | grep -oP "GLIBC_\K[\d.]+"
' _ {} \; 2>/dev/null | sort -V | tail -1 || true)
if [[ -n "$MAX_VER" && "$(printf '%s\n2.41\n' "$MAX_VER" | sort -V | tail -1)" != "2.41" ]]; then
    echo -e "${R}bundled libs need GLIBC_${MAX_VER} (> 2.41), too new to ship.${R}" >&2
    echo "  This AppImage would fail on any glibc < ${MAX_VER} (Fedora, Steam Deck, ...)." >&2
    echo "  patch_glibc.py did not cover these files:" >&2
    find "$APPDIR" -type f ! -l -exec sh -c '
        readelf -V "$1" 2>/dev/null | grep -q "GLIBC_'"$MAX_VER"'" && echo "    $1"
    ' _ {} \; 2>/dev/null | sort -u >&2
    exit 1
fi
echo "  OK - max glibc requirement: ${MAX_VER:-none} (<= 2.41)."

# the dev tree keeps modlists/loadorders/backups next to the binary; make sure
# none of it got copied into the AppDir before shipping
step "Scanning AppDir for personal state (must be none)"
LEAKS=$(find "$APPDIR" -type f \( \
        -iname 'modlist_*'      -o \
        -iname 'loadorder_*'    -o \
        -iname 'forbidden_*'    -o \
        -iname 'log.txt'        -o \
        -iname '*.bak'          -o \
        -iname '*.bak.*'        -o \
        -iname '*.good.*'       -o \
        -iname '*.premanualedit' \) 2>/dev/null || true)
# Note: *.conf is intentionally NOT matched here - linuxdeploy legitimately
# bundles fontconfig/.conf system files into the AppDir.
if [[ -n "$LEAKS" ]]; then
    echo -e "${R}personal/state files found in the AppDir, refusing to ship:${R}" >&2
    echo "$LEAKS" | sed 's/^/    /' >&2
    exit 1
fi
echo "  clean - no modlist/state files in AppDir."

# .AppImage goes at the repo root: NerevarineOrganizer-<ver>-x86_64.AppImage
step "Packaging AppImage"
DEST="$REPO_ROOT/NerevarineOrganizer-${APPIMAGE_VERSION}-x86_64.AppImage"
rm -f "$DEST"
ARCH=x86_64 "$AT_EXTRACTED/AppRun" --no-appstream "$APPDIR" "$DEST"

echo -e "${G}AppImage ready: $DEST${R}"

# Also drop a zipped copy into dist/ for local distribution
step "Packaging dist/ zip"
mkdir -p "$DIST_DIR"
DIST_ZIP="$DIST_DIR/NerevarineOrganizer-${APPIMAGE_VERSION}-x86_64.AppImage.zip"
zip_file "$DEST" "$DIST_ZIP"
echo "  -> $DIST_ZIP"

# Show actual minimum glibc the patched image requires
MAX_VER=$(find "$APPDIR" -type f ! -l -exec sh -c '
    readelf -V "$1" 2>/dev/null | grep -oP "GLIBC_\K[\d.]+"
' _ {} \; 2>/dev/null | sort -V | tail -1 || true)
echo ""
echo "  Bundled Qt + all dependencies - no system Qt required."
[[ -n "$MAX_VER" ]] && echo "  Minimum glibc after patching: $MAX_VER"
echo ""
