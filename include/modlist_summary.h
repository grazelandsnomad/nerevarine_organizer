#pragma once

// modlist_summary - the arithmetic behind Mods -> "Modlist summary".
//
// Qt Core only (no Widgets), so it's unit-testable against plain ModEntry rows
// and a QTemporaryDir; the MainWindow slot stays a thin wrapper that snapshots
// the list, calls in here, and renders the numbers.
//
// The size fallback is injected rather than reaching for ScanCoordinator:
// ModEntry::modSize is 0 until the async size scan lands, and only the caller
// knows whether an unknown size is worth a synchronous directory walk.

#include <QList>
#include <QString>

#include <functional>

#include "modentry.h"

namespace modlist_summary {

struct Stats {
    int    modCount     = 0;   // installed mods (installStatus == 1)
    int    enabledCount = 0;   // ...of which ticked
    int    sepCount     = 0;
    qint64 totalBytes   = 0;   // installed mods with a known size
    qint64 enabledBytes = 0;
};

// Count installed mods / separators and sum their sizes.
//
// `sizeLookup` resolves a mod whose modSize is still unknown (<= 0); pass an
// empty function to skip the fallback. A mod with an unknown size still counts
// toward modCount/enabledCount - only the byte totals skip it - so the counts
// never under-report while a size scan is in flight.
[[nodiscard]] Stats computeStats(
    const QList<ModEntry> &entries,
    const std::function<qint64(const QString &modPath)> &sizeLookup = {});

// Human-readable size: "0 B", "512 KB", "1.5 MB", "2.00 GB".
//
// NOTE: near-duplicates of this live in src/downloadqueue.cpp (x2) and
// src/modlistdelegate.cpp, and they have already drifted - those drop the
// bytes case and render "0 KB" for a sub-KB file. Folding them in here would
// change strings the download UI shows, so it's left as a deliberate follow-up.
[[nodiscard]] QString formatBytes(qint64 bytes);

// Installed mods whose folder sits outside `modsDir` - what gates the
// "consolidate into this profile" button. Folders that don't exist aren't
// counted; an empty `modsDir` yields 0.
[[nodiscard]] int countOutsideModsDir(const QList<ModEntry> &entries,
                                      const QString &modsDir);

} // namespace modlist_summary
