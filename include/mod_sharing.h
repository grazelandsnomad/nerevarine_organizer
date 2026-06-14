#pragma once

// mod_sharing - pure helpers for sharing a mod between modlist profiles WITHOUT
// copying its files.  A mod is two separable things: a folder on disk
// (referenced by an absolute ModPath) and a per-profile config row.  "Sharing"
// means adding a row to ANOTHER profile's modlist that points at the SAME
// folder, with that profile's own (possibly diverging) config.  These functions
// operate on ModEntry value lists + path strings so MainWindow's share action
// and its delete-time reference guard share one tested definition of "same mod
// path" and "already present".  QtCore-only; testable without QtWidgets.

#include "modentry.h"

#include <QList>
#include <QPair>
#include <QString>

namespace mod_sharing {

// Canonical comparison key for a mod folder path (QDir::cleanPath).  The
// share-dedup and the cross-profile reference scan both key off this so they
// agree on what "the same install" means.  Empty in -> empty out.
QString cleanModPath(const QString &path);

// Build the row to append to a target profile when sharing `source`.  The
// folder (modPath) and Nexus identity are kept verbatim (shared install) and
// the row is a clean, fully-installed, independent row - install/transient
// state (token, prev/merge/intended path, conflict + missing-master flags) is
// reset to defaults so it never collides with the source's install identity.
//   copyConfig=true  -> keep enabled state, FOMOD choices, annotation, deps,
//                       utility/favorite flags
//   copyConfig=false -> unchecked, no annotation/FOMOD/deps (start at default)
// customName, nexusUrl, modPath, dateAdded are kept regardless.
ModEntry makeSharedRow(const ModEntry &source, bool copyConfig);

// Index of an existing row in `target` that already represents this mod, or -1.
// Matches by cleanModPath first, then by non-empty nexusUrl (so a profile that
// already has the mod at its OWN path - e.g. a private fork - isn't double-added).
int findExistingRow(const QList<ModEntry> &target, const ModEntry &shared);

struct AppendResult {
    QList<ModEntry> entries;        // target list with `shared` appended (or unchanged)
    bool            added = false;  // false when it was already present (dedup no-op)
};

// Append `shared` to `target` unless findExistingRow says it's already there.
AppendResult appendSharedRow(QList<ModEntry> target, const ModEntry &shared);

// True if any of the given (profileKey, entries) lists contains a mod row whose
// cleanModPath equals `cleanPath`.  Pure core of the delete-time guard; the
// caller supplies the parsed modlists of every profile EXCEPT the active one.
bool pathReferencedIn(
    const QString &cleanPath,
    const QList<QPair<QString, QList<ModEntry>>> &profiles);

} // namespace mod_sharing
