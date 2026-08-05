#pragma once

// mainwindow_internal - shared internals for the mainwindow_*.cpp translation
// units. mainwindow.cpp is split across several TUs (so they compile in
// parallel) that all define MainWindow methods; this header carries the few
// file-scope helpers that more than one of those TUs needs. Not part of the
// public surface - only the mainwindow_*.cpp files include it.

#include <QString>
#include <QStringList>

// Resolve a writable path for a user-state file (modlist, load order, deploy
// manifest, ...). Under an AppImage the app dir is a read-only squashfs mount,
// so this routes to AppDataLocation; otherwise it prefers next to the binary.
// Defined in mainwindow.cpp; used there and by the deploy TU's state paths.
QString resolveUserStatePath(const QString &filename);

// LOOT game-id for a profile (empty => LOOT not applicable). Defined in
// mainwindow.cpp; used by the toolbar/menu gating there and the config TU.
QString lootGameFor(const QString &profileId);

// Recursive-delete a set of mod folders without blocking the GUI.
//
// Can't defer the whole delete: the openmw.cfg sync that runs right after
// probes the disk (prepareForSync rescues an orphan-managed data= line whose
// folder still EXISTS with plugins), so a folder still present during that sync
// gets its data=/content= resurrected. So it vacates the real path
// synchronously with a same-filesystem rename (metadata-only, fast even on
// NTFS, and the part that matters for cfg correctness), then defers only the
// slow byte-delete of the renamed folder.
//
// Callers MUST have dropped every modlist reference to these paths first.
// Defined in mainwindow_install.cpp; used there and by the cleanup sweep.
void removeModFoldersAsync(QStringList paths);

// Suffix predicates for drop handling - used by the ModListWidget drop target
// (setup TU) and MainWindow's drag/drop events (home).
bool isInstallableArchiveSuffix(const QString &path);
bool isImportFileSuffix(const QString &path);

