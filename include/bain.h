#pragma once

// bain - BAIN-archive (Wrye Bash) detection + staging.
//
// A BAIN archive groups its content under numbered top-level subfolders -
// "00 Core", "01 Optional Textures", "10 Alternate Meshes" - where each
// numbered folder is a SELECTABLE sub-package whose contents are a
// game-data-rooted tree (meshes/, textures/, *.esp, ...). The user picks a
// subset; the chosen packages are merged, in numeric order, into one data root.
//
// Unlike FOMOD this is a naming CONVENTION, not a spec - so a BAIN archive and
// a normal "install all of these" multi-data-root mod (Tamriel Rebuilt's
// "00 Core" + "01 Faction Integration") can look byte-for-byte identical.
// looksLikeBain() is therefore deliberately conservative, and the caller treats
// a match as "offer a package picker with everything pre-checked", never as a
// silent restructuring: on a false positive the user just clicks Install with
// all boxes ticked and gets the same result as a plain install.
//
// FOMOD takes precedence: looksLikeBain() returns false when a fomod/ installer
// is present, and the caller only reaches BAIN after FomodWizard::hasFomod()
// has already declined.
//
// Pure (no Qt widgets) so detection + merge are unit-testable against a
// QTemporaryDir. Staging reuses fomod_copy / fomod::resolveDest for the
// case-insensitive, traversal-guarded, last-writer-wins merge.

#include <QString>
#include <QStringList>
#include <QList>

namespace bain {

struct Package {
    QString name;     // the on-disk folder name, e.g. "01 Optional Textures"
    QString path;     // absolute path to that folder
};

// True when `modPath`'s top level has the BAIN shape: at least two
// numbered-prefixed subfolders (e.g. "00 Core"), none of them a bare OpenMW
// asset root, and NO fomod/ installer present (FOMOD wins). Conservative by
// design - a miss just means the user installs normally; a false positive only
// costs one extra click on an all-checked dialog.
bool looksLikeBain(const QString &modPath);

// The numbered packages under `modPath`, in ascending numeric order. Empty when
// the shape isn't BAIN. Order matters: later packages overwrite earlier ones
// during the merge (last-writer-wins).
QList<Package> packages(const QString &modPath);

// Merge the chosen packages (by folder name) into a fresh staging dir
// `<modPath>/../bain_install` and return its path. Packages are applied in the
// order packages() reports (numeric), so a higher-numbered choice overwrites a
// lower one. Non-destructive to the source packages (re-runnable to change the
// selection). Returns "" if nothing was chosen or staging produced no files.
QString stage(const QString &modPath, const QStringList &chosenNames);

} // namespace bain
