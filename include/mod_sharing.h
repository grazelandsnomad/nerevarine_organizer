#pragma once

// mod_sharing - helpers for sharing a mod between modlist profiles WITHOUT
// copying its files. A mod is a folder on disk (absolute ModPath) plus a
// per-profile config row. Sharing adds a row to another profile pointing at
// the SAME folder, with that profile's own config. Works on ModEntry lists +
// path strings so the share action and the delete-time reference guard agree
// on "same mod path" and "already present". QtCore-only.

#include "modentry.h"

#include <QList>
#include <QPair>
#include <QString>

namespace mod_sharing {

// Comparison key for a mod folder path (QDir::cleanPath). Share-dedup and the
// cross-profile reference scan both key off this. Empty in -> empty out.
QString cleanModPath(const QString &path);

// Build the row to append to a target profile when sharing `source`. modPath
// and Nexus identity are kept verbatim. Install/transient state (token,
// prev/merge/intended path, conflict + missing-master flags) is reset to
// defaults so it never collides with the source's install identity.
//   copyConfig=true  -> keep enabled state, FOMOD choices, annotation, deps,
//                       utility/favorite flags
//   copyConfig=false -> unchecked, no annotation/FOMOD/deps (defaults)
// customName, nexusUrl, modPath, dateAdded are kept regardless.
ModEntry makeSharedRow(const ModEntry &source, bool copyConfig);

// Index of an existing row in `target` for this mod, or -1. Matches by
// cleanModPath first, then by non-empty nexusUrl, so a profile holding the
// mod at its own path (e.g. a private fork) isn't double-added.
int findExistingRow(const QList<ModEntry> &target, const ModEntry &shared);

struct AppendResult {
    QList<ModEntry> entries;        // target with `shared` appended (or unchanged)
    bool            added = false;  // false when already present (dedup no-op)
};

// Append `shared` to `target` unless findExistingRow says it's already there.
AppendResult appendSharedRow(QList<ModEntry> target, const ModEntry &shared);

// True if any (profileKey, entries) list has a row whose cleanModPath equals
// `cleanPath`. Core of the delete-time guard; caller passes the modlists of
// every profile EXCEPT the active one.
bool pathReferencedIn(
    const QString &cleanPath,
    const QList<QPair<QString, QList<ModEntry>>> &profiles);

} // namespace mod_sharing
