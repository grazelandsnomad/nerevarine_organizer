#pragma once

#include <QString>
#include <QStringList>

// mod_naming - pure folder-name heuristics carved out of
// MainWindow::addModFromPath in 0.4.  Three classes of decision the
// install pipeline used to inline:
//
//   · Sibling-folder dedup ("one folder per mod" - delete `<base>` and
//     `<base>_<ts1>` when `<base>_<ts2>` is the freshly-installed dir).
//   · Generic-folder detection ("scripts" / "00 Core" / "main-12345-..."
//     are too generic to use as the modlist display name; the caller
//     replaces the name with a cached Nexus title or a sibling row's
//     CustomName).
//   · Trailing-version-chain strip ("Shishi - Redoran Outpost-57535-v1-1"
//     → "Shishi - Redoran Outpost").
//
// All Qt-Core-only.  No FS access, no widget access, no MainWindow
// state.  Tests in test_mod_naming.cpp pin the regex thresholds and
// the "Nexus folder shape" guard against past misclassifications.

namespace mod_naming {

// Sibling-folder dedup: scan `siblings` (typically QDir::entryList of
// the mods-root directory) and return the names that should be
// removed because they're previous versions of the freshly-installed
// `currentFolderName`.
//
// Returns an empty list unless `currentFolderName` (after stripping a
// trailing "_<digits>" timestamp suffix) matches the Nexus-archive
// shape "name-<id>".  Without that guard a user-named folder whose
// name happens to end in "_1234" would trip the cleanup and silently
// nuke unrelated state.
//
// `currentFolderName` itself is never returned even if it matches
// the pattern (the caller just installed it - it's not a stale
// sibling).
QStringList findStaleSiblings(const QString     &currentFolderName,
                              const QStringList &siblings);

// Siblings that are OLDER BUILDS of the same Nexus mod: folder names carrying
// "-<modId>" followed by a version/timestamp chain.
//
// findStaleSiblings above keys on the WHOLE folder name, so it only ever
// matches a literal re-download of the same file (where InstallController
// appended "_<ts>" to dodge the path collision). A Nexus folder is
// "<Name>-<modId>-<version>-<uploadTs>", so an upgrade produces a completely
// different name and was invisible to it - which is how mods dirs grew nine
// builds of the same mod. This matcher closes that gap.
//
// `modId` must come from the row's NexusUrl (parseNexusModUrl in nxmurl.h), NOT
// from the folder name: splitting the name on '-' breaks on any mod whose title
// contains a dash ("OSSC - Oblivion-Style Spell Casting 2.0-58653-..."). Returns
// {} when modId <= 0, so a row with no Nexus URL keeps today's behaviour.
//
// `currentFolderName` is never returned. Callers MUST still check that nothing
// else references a hit before deleting it: one mod id can legitimately own
// several installed folders (separate files on one Nexus page).
QStringList findOlderVersionSiblings(const QString     &currentFolderName,
                                     const QStringList &siblings,
                                     int                modId);

// Returns true when `folderName` looks generic enough that the
// modlist display name should be replaced with the cached Nexus title
// (or a sibling row's CustomName, or fetched async).  Catches three
// classes:
//   · Exact name in a curated list ("scripts", "data", "00 core",
//     "main", "mygui", ...).
//   · Nexus-archive slug shape "main-<id>-<v>...-<ts>" or
//     "complete pack-<id>-<v>...-<ts>".
//   · Generic versioned-archive shape "<anything>-<id>-<v>{4,}".
//   · Common inner-folder prefixes ("sound", "audio", "mesh*", "fix*").
//
// All comparisons case-insensitive.
bool folderNameLooksGeneric(const QString &folderName);

// Strip a trailing Nexus version chain off a folder name and return
// the cleaned-up base.  Matches "[-_]<id>([-_]v?<v>){2,}" at the end
// of the name.  Returns empty string when the input doesn't end in
// such a chain (caller leaves the name untouched).
//
// Examples:
//   "Shishi - Redoran Outpost-57535-v1-1-1760726463"
//     → "Shishi - Redoran Outpost"
//   "Foo Mod-12345" → ""   (single trailing -<id> doesn't qualify)
//   "Plain Folder Name" → ""
QString stripTrailingVersionChain(const QString &folderName);

// Look up a hard-coded display-name override for this folder name.
// Currently a tiny table - "restock" → "(OpenMW 0.49) Restocking" -
// kept as a function for testability and so future entries land
// somewhere with a clear contract.  Returns empty string when no
// entry matches.  Comparison is case-insensitive on the input.
QString hardcodedRename(const QString &folderName);

} // namespace mod_naming
