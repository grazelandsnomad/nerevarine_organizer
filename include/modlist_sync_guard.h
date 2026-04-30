#pragma once

// modlist_sync_guard - pure detector for "modPath isn't under the canonical
// mods root."  The motivating scenario: the modlist_morrowind.txt file is
// synced across machines via git.  On machine A a mod lives at
// /home/jalcazo/Games/nerevarine_mods/Foo and on machine B the same mod
// lives under /mnt/usb/mods/Foo.  When the organizer opens the synced
// modlist on B, every row points at A's layout and the launcher writes
// dangling data= lines to openmw.cfg.
//
// This detector flags any mod whose modPath isn't a direct child of one
// of the supplied canonical roots.  The Inspector dialog surfaces the
// result so drift is visible before the user commits the synced file.
//
// No Qt Widgets, no filesystem I/O.  Same "pure" pattern as
// openmwconfigwriter.h / plugin_collisions.h - golden-file testable
// against fabricated inputs, no stat() call can lie to us.

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
    QString reason;     // short human-readable string, e.g. "not under canonical root"
};

struct ModlistSyncReport {
    // Echoed back so the UI can quote the active canonical roots in its
    // "please fix your paths" message without re-deriving them.
    QStringList canonicalRoots;
    QList<ModPathDrift> driftEntries;
    int totalModsChecked = 0;
};

// Normalise a path the way this detector does comparisons: strip trailing
// separators, collapse "/./", keep symlink text as-is (we don't stat).
// Exposed so callers that want to build a "probably-canonical" list can
// match our rules exactly.
QString canonicalizePathText(const QString &path);

// Flag every mod whose modPath doesn't start with one of `canonicalRoots`
// (string-prefix match on the canonicalized forms, with a / boundary so
// "/home/x/mods" doesn't accept "/home/x/mods_backup/Foo").
//
// Empty `canonicalRoots` means "nothing is canonical" - the function
// returns an empty drift list rather than flagging everything, because
// the caller has no working baseline and a flood of warnings would
// drown the user.
//
// Empty modPath rows are counted toward totalModsChecked but treated as
// drift with reason "path is empty" - they point at something that
// doesn't exist on disk and need attention regardless of root policy.
ModlistSyncReport findModlistPathDrift(
    const QList<SyncGuardInput> &mods,
    const QStringList &canonicalRoots);

} // namespace openmw
