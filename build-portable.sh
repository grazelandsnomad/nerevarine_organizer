#!/usr/bin/env bash
# Build portable release artifacts inside an old-glibc container.
#
# The durable fix for the recurring "version `GLIBC_2.xx' not found" breakage
# on Fedora / Steam Deck (see packaging/Dockerfile.portable for the why):
# instead of building on the maintainer's bleeding-edge host and patching the
# glibc requirements back down afterwards, build in Ubuntu 24.04 (glibc 2.39)
# so BOTH the AppImage and the plain release binary target an old glibc
# natively and run everywhere newer.
#
# Usage:
#   ./build-portable.sh                 # build everything (AppImage + release)
#   ./build-portable.sh appimage        # AppImage only
#   ./build-portable.sh release         # plain release build only
#   CONTAINER_ENGINE=podman ./build-portable.sh
#
# Outputs land in the repo root, same as running the build scripts directly.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
IMAGE="${NRV_BUILD_IMAGE:-nrv-portable-build}"
DOCKERFILE="$REPO_ROOT/packaging/Dockerfile.portable"
TARGET="${1:-all}"

ENGINE="${CONTAINER_ENGINE:-}"
if [[ -z "$ENGINE" ]]; then
    ENGINE="$(command -v docker || command -v podman || true)"
fi
[[ -n "$ENGINE" ]] || { echo "ERROR: need docker or podman on PATH (or set CONTAINER_ENGINE)." >&2; exit 1; }

G='\033[0;32m'; R='\033[0m'
step() { echo -e "${G}== $* ==${R}"; }

step "Building the old-glibc build image ($IMAGE)"
"$ENGINE" build -f "$DOCKERFILE" -t "$IMAGE" "$REPO_ROOT"

# What to build inside the container. build-appimage.sh already bundles Qt and
# runs the patch_glibc + portability gate, which become a harmless no-op here
# (nothing requires a glibc newer than the 2.39 base). build-release.sh is run
# for the plain (non-AppImage) artifact so it is portable too - this is the
# half of the bug that a new AppImage alone never fixed.
case "$TARGET" in
    appimage) INNER='./build-appimage.sh' ;;
    release)  INNER='./build-release.sh' ;;
    all)      INNER='./build-appimage.sh; [[ -x ./build-release.sh ]] && ./build-release.sh' ;;
    *) echo "ERROR: unknown target '$TARGET' (use: all | appimage | release)." >&2; exit 1 ;;
esac

step "Building inside the container (target: $TARGET)"
# Run as the host user so output files are not left root-owned. The repo is
# mounted read-write; artifacts are written next to the scripts as usual.
"$ENGINE" run --rm \
    -e CC -e CXX \
    -u "$(id -u):$(id -g)" \
    -v "$REPO_ROOT":/src -w /src \
    "$IMAGE" bash -euo pipefail -c "$INNER"

step "Done - artifacts are in $REPO_ROOT"
echo "  Verify portability of a result, e.g.:"
echo "    ./NerevarineOrganizer-*-x86_64.AppImage --appimage-extract >/dev/null"
echo "    readelf -V squashfs-root/usr/lib/libQt6Gui.so.6 | grep -i 'GLIBC_2.4[0-9]' || echo 'clean'"
