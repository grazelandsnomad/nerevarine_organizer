#pragma once

// mainwindow_internal - shared internals for the mainwindow_*.cpp translation
// units. mainwindow.cpp is split across several TUs (so they compile in
// parallel) that all define MainWindow methods; this header carries the few
// file-scope helpers that more than one of those TUs needs. Not part of the
// public surface - only the mainwindow_*.cpp files include it.

#include <QString>

// Resolve a writable path for a user-state file (modlist, load order, deploy
// manifest, ...). Under an AppImage the app dir is a read-only squashfs mount,
// so this routes to AppDataLocation; otherwise it prefers next to the binary.
// Defined in mainwindow.cpp; used there and by the deploy TU's state paths.
QString resolveUserStatePath(const QString &filename);

// LOOT game-id for a profile (empty => LOOT not applicable). Defined in
// mainwindow.cpp; used by the toolbar/menu gating there and the config TU.
QString lootGameFor(const QString &profileId);
