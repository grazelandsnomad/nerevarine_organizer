#pragma once

// proton - pure path construction for files that live inside a Proton/Steam
// compatibility prefix (steamapps/compatdata/<appid>/pfx/drive_c/...).
//
// Bethesda games run under Proton on Linux, so their per-user config - the
// engine .ini and, crucially for load-order management, Plugins.txt - lives
// inside the prefix's emulated Windows user profile, NOT under the real $HOME.
// Locating those files means walking from a Steam library's compatdata root
// down through a couple of layout variants that differ across Proton versions
// ("Documents/My Games" vs "My Documents/My Games").
//
// These helpers BUILD the candidate paths; the caller probes them against the
// filesystem (and honours any user override) - so this stays pure (no QFileInfo,
// no $HOME, no QSettings) and the layout logic is unit-testable in isolation.

#include <QString>
#include <QStringList>

namespace proton {

// "<compatdataRoot>/<appId>/pfx/drive_c/users/steamuser" - the emulated
// Windows user profile inside the prefix.  Empty if either input is empty.
QString prefixUserDir(const QString &compatdataRoot, const QString &appId);

// "<prefixUserDir>/AppData/Local[/<folder>]" - where Bethesda Plugins.txt
// lives (e.g. AppData/Local/Oblivion/Plugins.txt).  `folder` optional.
QString localAppData(const QString &prefixUserDir, const QString &folder = {});

// Candidate "Documents/My Games[/<folder>]" dirs under the prefix user dir,
// in probe order: the newer "Documents/My Games" first, then the older
// "My Documents/My Games".  Where the engine .ini lives.  `folder` optional.
QStringList myGamesDirs(const QString &prefixUserDir, const QString &folder = {});

// Map "<lib>/steamapps/common" roots (the shape game_profiles hands out for
// installed games) to their sibling "<lib>/steamapps/compatdata" roots.
// Inputs that don't end in "/steamapps/common" are skipped; result is
// de-duplicated, order preserved.
QStringList compatdataRootsFromCommon(const QStringList &commonRoots);

} // namespace proton
