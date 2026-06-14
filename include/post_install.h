#pragma once

// post_install - pure decision helpers for the optional prompts MainWindow
// fires after a mod is registered (groundcover management, splash-screen
// replacement, bundled-patch re-enable).  These used to be inlined inside the
// 490-line addModFromPath; pulling the detection logic out here keeps that
// method to orchestration and makes the "what counts as X" rules unit-testable
// without QtWidgets (QtCore + a QTemporaryDir is enough).

#include <QString>
#include <QStringList>

namespace post_install {

// True if the mod (by folder path and/or display name) looks like a
// grass/groundcover mod OpenMW should manage via groundcover= lines.  Matches
// "grass"/"groundcover" case-insensitively, plus a curated list of named grass
// mods that contain neither word.
bool looksLikeGroundcover(const QString &modPath, const QString &displayName);

// Locate a Splash/ directory containing splash images within a mod.  The mod
// root itself may BE the Splash dir, or a Splash/ subdir may sit up to three
// levels down.  Returns the absolute path of the splash dir, or empty if none.
QString findSplashDir(const QString &modRoot);

// (openmw.cfg's external data= dirs are parsed by openmw::externalDataPaths in
// openmwconfigwriter - the managed-block format lives there, not here.)

// Normalize a mod display name to a comparison key: lowercased, letters and
// digits only.  "01 Grass for Remiros' Groundcover" -> "01grassforremiros...".
QString normalizeModName(const QString &s);

// True if `subfolderName` is a bundled-patch subfolder of the shape
// "<N>[letter] <something> for <target>" whose <target>, normalized, contains
// the given already-normalized mod name.  Mirrors the auto-skip detection used
// when a patch-host mod's companion finally gets installed.  Names whose
// normalized form is shorter than 4 chars never match (avoids false hits on
// tiny names).
bool bundledPatchMatchesMod(const QString &subfolderName,
                            const QString &normalizedModName);

} // namespace post_install
