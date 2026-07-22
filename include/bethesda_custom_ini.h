#pragma once

// Configure StarfieldCustom.ini's [Archive] section so deployed mods actually
// load. Two separate concerns, and the first is the one that matters:
//
//   1. Loose files. Starfield ignores loose assets in Data/ unless archive
//      invalidation is on, so a deployed mod list otherwise looks installed and
//      changes nothing in game. The fix is the long-standing Bethesda pair
//      bInvalidateOlderFiles=1 + an empty sResourceDataDirsFinal=.
//   2. Stray .ba2 archives. Starfield auto-loads "<Plugin> - Main.ba2" /
//      "<Plugin> - Textures.ba2" by plugin name, so only an archive whose stem
//      matches no deployed plugin needs registering by hand.
//
// Unlike Oblivion.ini, StarfieldCustom.ini is a user override file the game does
// not ship, so the caller must be prepared to CREATE it (passing empty text
// here does the right thing).
//
// A pure text transform, like bethesda_archives::configureArchives, so it is
// unit-testable without touching a prefix.

#include <QString>
#include <QStringList>

namespace bethesda_custom_ini {

// Return `iniText` with [Archive] configured for loose files:
// bInvalidateOlderFiles=1 and an empty sResourceDataDirsFinal=. A missing
// [Archive] is appended; empty input yields a complete minimal file. Unrelated
// sections, comments, casing and key order are preserved. Idempotent. Output
// uses CRLF line endings (Windows ini).
//
// It deliberately does NOT register .ba2 archives. The archive-list keys
// (sResourceIndexFileList for textures, sResourceArchive2List otherwise)
// REPLACE the base ini's value rather than extending it, so a Custom.ini
// listing only a mod's archives unloads every vanilla one - a real
// StarfieldCustom.ini has to re-list "Starfield - Textures01.ba2" and its
// eleven siblings just to add a single mod archive. That vanilla list varies by
// game version and DLC, so writing the key at all would mean guessing it, with
// missing base-game textures as the failure mode. Archives named after their
// plugin auto-load and need none of this; anything else is reported by
// strayArchives() so the user can act, rather than silently half-configured.
QString configureCustomIni(const QString &iniText);

// The subset of `deployedBa2s` the engine will NOT load on its own, because
// they are named after no deployed plugin. "Foo.ba2", "Foo - Main.ba2" and
// "Foo - Textures.ba2" are all covered by a plugin "Foo.esm"; "FooPatch -
// Main.ba2" is NOT (a bare prefix test would wrongly call it covered).
// Inputs are bare file names. Caller warns; we never edit the archive lists.
QStringList strayArchives(const QStringList &deployedBa2s,
                          const QStringList &deployedPlugins);

} // namespace bethesda_custom_ini
