#!/usr/bin/env bash
# Build a self-contained AppImage of Nerevarine Organizer.
#
# Prerequisites (install these on the builder host, NOT packaged into the
# AppImage):
#   · linuxdeploy + linuxdeploy-plugin-qt
#     https://github.com/linuxdeploy/linuxdeploy/releases
#   · Qt 6 (Widgets + Network + Concurrent) dev packages
#   · qtkeychain-qt6 dev package
#   · cmake, ninja (or make), 7z, unzip
#
# Run from the repo root:
#   bash packaging/appimage/build-appimage.sh
#
# Output: ./Nerevarine_Organizer-<version>-x86_64.AppImage
#
# The AppImage bundles Qt6 libraries so users without Qt6 installed can run
# it.  qtkeychain is linked but the system's libsecret / KWallet backend
# must still be present on the target machine (both are ubiquitous on
# modern Linux desktops).

set -euo pipefail

APP_NAME="Nerevarine_Organizer"
APP_VERSION="$(grep -E '^project\(.*VERSION' CMakeLists.txt | sed -E 's/.*VERSION ([0-9.]+).*/\1/')"
ARCH="${ARCH:-x86_64}"

BUILD_DIR="build-appimage"
APPDIR="${BUILD_DIR}/AppDir"

echo "==> Configuring (build=${BUILD_DIR}, arch=${ARCH}, version=${APP_VERSION})"
cmake -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr

echo "==> Building"
cmake --build "${BUILD_DIR}" --parallel

echo "==> Installing into AppDir"
rm -rf "${APPDIR}"
DESTDIR="$(pwd)/${APPDIR}" cmake --install "${BUILD_DIR}"

# linuxdeploy expects a specific layout; the CMake install above already
# lays things out under /usr/bin, /usr/share/applications, /usr/share/icons.
# If the desktop file doesn't exist (older build), drop a copy in.
if [[ ! -f "${APPDIR}/usr/share/applications/nerevarine_organizer.desktop" ]]; then
    install -Dm644 nerevarine_organizer.desktop \
        "${APPDIR}/usr/share/applications/nerevarine_organizer.desktop"
fi
if [[ ! -f "${APPDIR}/usr/share/icons/hicolor/256x256/apps/nerevarine_organizer.png" ]]; then
    install -Dm644 assets/icons/cystal_full_0.png \
        "${APPDIR}/usr/share/icons/hicolor/256x256/apps/nerevarine_organizer.png"
fi

echo "==> Running linuxdeploy with Qt plugin"
OUTPUT="${APP_NAME}-${APP_VERSION}-${ARCH}.AppImage" \
    linuxdeploy \
        --appdir "${APPDIR}" \
        --plugin qt \
        --output appimage \
        --desktop-file "${APPDIR}/usr/share/applications/nerevarine_organizer.desktop" \
        --icon-file    "${APPDIR}/usr/share/icons/hicolor/256x256/apps/nerevarine_organizer.png"

echo ""
echo "==> Done."
ls -lh ${APP_NAME}-*.AppImage 2>/dev/null || true
echo ""
echo "Test it with:  ./${APP_NAME}-${APP_VERSION}-${ARCH}.AppImage"
