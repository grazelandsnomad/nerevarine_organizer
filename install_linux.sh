#!/bin/sh
# Post-build install helper for the "Release (Linux)" Code::Blocks target.
# Called by ExtraCommands via: flatpak-spawn --host sh install_linux.sh
set -e
install -Dm755 bin/Release_Linux/nerevarine_organizer "$HOME/.local/bin/nerevarine_organizer"
install -Dm644 nerevarine_organizer.desktop "$HOME/.local/share/applications/nerevarine_organizer.desktop"
install -Dm644 assets/icons/cystal_full_0.png "$HOME/.local/share/icons/hicolor/256x256/apps/nerevarine_organizer.png"

# "Build" launcher - points at the in-tree Release binary ($PWD/bin/...).
# We substitute the absolute path at install time so the launcher keeps
# working regardless of where the repo lives.
BUILD_DESKTOP="$HOME/.local/share/applications/nerevarine_organizer_build.desktop"
install -d "$(dirname "$BUILD_DESKTOP")"
sed "s|@BUILD_EXEC@|$PWD/bin/Release_Linux/nerevarine_organizer|" \
    nerevarine_organizer_build.desktop > "$BUILD_DESKTOP"
chmod 644 "$BUILD_DESKTOP"

# Multi-size "build" icon variant (crystal + hammer badge) - installed so the
# DE has the full hicolor pyramid available for fuzzy/small-size rendering.
for size in 16x16 24x24 32x32 48x48 64x64 96x96 128x128 192x192 256x256; do
    src="assets/icons/hicolor/$size/apps/nerevarine_organizer_build.png"
    [ -f "$src" ] && install -Dm644 "$src" \
        "$HOME/.local/share/icons/hicolor/$size/apps/nerevarine_organizer_build.png"
done

# Translations + prefs next to the binary so Translator::findTranslationsDir
# (candidate 1: applicationDirPath()/translations) picks them up.  Without
# this, edits to translations/english.ini never reach the running binary
# and new translation keys silently fall back to their raw key names.
rm -rf "$HOME/.local/bin/translations"
cp -r translations "$HOME/.local/bin/translations"
install -Dm644 nerevarine_prefs.ini "$HOME/.local/bin/nerevarine_prefs.ini"

# Also refresh the translations next to the Code::Blocks build output so
# running ./bin/Release_Linux/nerevarine_organizer directly (development
# flow) picks up the same files without requiring a reinstall.
rm -rf bin/Release_Linux/translations
cp -r translations bin/Release_Linux/translations
install -Dm644 nerevarine_prefs.ini bin/Release_Linux/nerevarine_prefs.ini

update-desktop-database "$HOME/.local/share/applications" 2>/dev/null || true
gtk4-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || \
gtk-update-icon-cache  -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
echo "Installed to $HOME/.local/ (binary + translations)"
