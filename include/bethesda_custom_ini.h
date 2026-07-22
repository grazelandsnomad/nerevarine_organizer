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

// The ini key used to register archives that Starfield will not pick up by
// name. UNVERIFIED against a real install: Bethesda has shipped both
// sResourceArchiveList2 and sResourceIndexFileList across titles, and Starfield
// was not available to test here. Isolated deliberately so correcting it is a
// one-line change, and so the invalidation keys above work regardless.
extern const QString kStrayArchiveKey;

// Return `iniText` with [Archive] configured: bInvalidateOlderFiles=1, an empty
// sResourceDataDirsFinal=, and kStrayArchiveKey extended with `strayBa2s`
// (existing entries kept, case-insensitive de-dup, order preserved). A missing
// [Archive] is appended; empty input yields a complete minimal file. Unrelated
// sections, comments, casing and key order are preserved. Idempotent. Output
// uses CRLF line endings (Windows ini).
QString configureCustomIni(const QString &iniText, const QStringList &strayBa2s);

// The subset of `deployedBa2s` Starfield will NOT load on its own: those whose
// name does not begin with the stem of any plugin in `deployedPlugins`.
// "Foo - Main.ba2" and "Foo - Textures.ba2" are covered by "Foo.esm";
// "SomeTextures.ba2" with no matching plugin is not. Inputs are bare file names.
QStringList strayArchives(const QStringList &deployedBa2s,
                          const QStringList &deployedPlugins);

} // namespace bethesda_custom_ini
