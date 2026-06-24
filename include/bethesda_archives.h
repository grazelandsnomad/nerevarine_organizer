#pragma once

// Configure Oblivion.ini's [Archive] section so deployed mods' assets load.
// Oblivion predates Skyrim's auto-by-name BSA loading, hence two quirks:
//   1. A BSA loads only if listed in [Archive] SArchiveList (comma-separated).
//      A mod's .bsa is invisible until its name is added there.
//   2. Loose/replacer assets need archive invalidation or the compressed
//      vanilla BSA wins. Fix (as Wrye Bash/OBMM do): bInvalidateOlderFiles=1 +
//      an empty SInvalidationFile= in [Archive].
//
// configureArchives() is a pure text transform on the ini so it's testable.

#include <QString>
#include <QStringList>

namespace bethesda_archives {

// Return iniText with [Archive] configured: bInvalidateOlderFiles=1,
// SInvalidationFile= (empty), and SArchiveList extended with modBsas (existing
// entries kept, case-insensitive de-dup, order preserved). A missing [Archive]
// is appended; a missing SArchiveList is seeded with the vanilla BSAs first.
// Output uses CRLF line endings (Windows ini).
QString configureArchives(const QString &iniText, const QStringList &modBsas);

} // namespace bethesda_archives
