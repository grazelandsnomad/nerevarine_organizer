#pragma once

// Pure detector for "modPath isn't under the mods root."
// When modlist_morrowind.txt is git-synced across machines the paths only match
// machine A's layout; on B every row points elsewhere and the launcher writes
// dangling data= lines to openmw.cfg. Flags any mod not under a supplied root so
// the Inspector can show drift before the user commits the synced file.
// No Qt Widgets and no filesystem I/O, so it's testable with fabricated inputs.

#include <QList>
#include <QString>
#include <QStringList>

namespace openmw {

struct SyncGuardInput {
    QString modLabel;   // display name of the mod, for the warning message
    QString modPath;    // absolute path the modlist stores for this mod
};

struct ModPathDrift {
    QString modLabel;
    QString modPath;
    QString reason;     // short human-readable string, e.g. "not under mods root"
};

struct ModlistSyncReport {
    // Echoed back so the UI can quote the active roots without re-deriving them.
    QStringList canonicalRoots;
    QList<ModPathDrift> driftEntries;
    int totalModsChecked = 0;
};

// Normalize a path the way this detector compares: strip trailing separators,
// collapse "/./", leave symlink text as-is (no stat). Exposed so callers can
// build root lists matching these rules.
QString canonicalizePathText(const QString &path);

// Flag every mod whose modPath isn't under one of `canonicalRoots` (prefix
// match on normalized forms with a / boundary).
// Empty `canonicalRoots` returns no drift rather than flagging everything -
// without a baseline the warnings would just drown the user.
// Empty modPath rows count toward totalModsChecked but are drift, reason
// "path is empty".
ModlistSyncReport findModlistPathDrift(
    const QList<SyncGuardInput> &mods,
    const QStringList &canonicalRoots);

} // namespace openmw
