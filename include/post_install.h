#pragma once

// post_install - decision helpers for the optional prompts MainWindow fires
// after a mod is registered (groundcover, splash replacement, bundled-patch
// re-enable). Pulled out of addModFromPath so the detection rules are
// testable without QtWidgets.

#include <QString>
#include <QStringList>

namespace post_install {

// True if the mod looks like grass/groundcover OpenMW should manage via
// groundcover= lines. Matches "grass"/"groundcover" plus a curated list of
// named grass mods containing neither word.
bool looksLikeGroundcover(const QString &modPath, const QString &displayName);

// Find a Splash/ dir of splash images in a mod. The mod root may BE the
// Splash dir, or a Splash/ subdir may sit up to three levels down. Absolute
// path, or empty if none.
QString findSplashDir(const QString &modRoot);

// (External data= dirs are parsed by openmw::externalDataPaths in
// openmwconfigwriter, not here.)

// Comparison key for a display name: lowercased, letters and digits only.
// "01 Grass for Remiros' Groundcover" -> "01grassforremiros...".
QString normalizeModName(const QString &s);

// True if `subfolderName` is a bundled-patch subfolder shaped
// "<N>[letter] <something> for <target>" whose normalized <target> contains
// the given already-normalized mod name. Normalized forms shorter than 4
// chars never match (avoids false hits on tiny names).
bool bundledPatchMatchesMod(const QString &subfolderName,
                            const QString &normalizedModName);

} // namespace post_install
