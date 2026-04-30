#!/usr/bin/env bash
# Build script for nerevarine_organizer.
# Double-click the matching .desktop launcher on your Desktop to run this
# in a terminal window - or invoke it manually from the repo root.

set -e  # stop on first error so build failures are immediately visible

G='\033[0;32m'
R='\033[0m'

cd "$(dirname "$(readlink -f "$0")")"

echo -e "${G}-- Configuring ---${R}"
cmake -S . -B build

echo ""
echo -e "${G}-- Building ---${R}"
cmake --build build -j"$(nproc)"

echo ""
echo -e "${G}-- Installing to bin/Release_Linux ---${R}"
# CMake drops the binary + runtime assets into build/.  Mirror them into
# bin/Release_Linux/ so both launch paths (CMake build dir and the
# historical Code::Blocks output location) stay in sync - avoids the
# "I edited code but the old binary still runs" trap.
mkdir -p bin/Release_Linux/translations
cp -f  build/nerevarine_organizer        bin/Release_Linux/nerevarine_organizer
cp -f  build/nerevarine_prefs.ini        bin/Release_Linux/nerevarine_prefs.ini
cp -rf build/translations/.              bin/Release_Linux/translations/

echo ""
echo -e "${G}✓ Build finished.${R}"
echo -e "${G}   CMake binary:      $(pwd)/build/nerevarine_organizer${R}"
echo -e "${G}   Release_Linux:     $(pwd)/bin/Release_Linux/nerevarine_organizer${R}"
echo ""
# -n 1: return after one key; -s: don't echo the key; -r: raw (don't eat \);
# /dev/tty reads from the terminal directly, which matters when konsole
# launches the script without wiring stdin to the tty.
read -n 1 -s -r -p "Press any key to close…" </dev/tty
echo ""
