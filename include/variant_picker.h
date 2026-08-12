#ifndef VARIANT_PICKER_H
#define VARIANT_PICKER_H

// Undeclared variant choosers: a mod that ships N alternatives and expects the
// user to copy one in by hand.
//
// Main Menu Redone is the shape this was built from: its Data/ holds a
// `mainmenuwallpapers/` folder whose 14 subfolders (AuroraBorealis/,
// Blackreach/, ...) each contain their own `DATA/` mirror of the game's Data
// tree. No FOMOD, no BAIN numbering - the readme just says "copy the DATA of
// the wallpaper you want". Installed as-is, the chooser tree deploys inertly
// and the mod appears to do nothing, with nothing anywhere saying a choice was
// expected.
//
// Detection is deliberately strict: a folder counts as a chooser only when it
// has at least two subfolders and EVERY subfolder carries a case-insensitive
// `data/` child. Weakening either rule starts matching texture packs whose
// subfolders merely organise content.
//
// Applying a choice copies the chosen option's data/ contents into the folder
// the chooser sits in (for Main Menu Redone: its Data/), which is exactly what
// the readme asks for. Destinations route through fomod::resolveDest so an
// upper-case `DATA/TEXTURES` merges into an existing `Data/textures` instead
// of forking a duplicate case-variant tree - the same Linux hazard the FOMOD
// installer already solves. Re-applying a different option later simply
// overwrites (last-writer-wins), so the choice stays revisable.
//
// Pure filesystem logic, no widgets: MainWindow owns the dialog.

#include <QList>
#include <QString>
#include <QStringList>

namespace variant_picker {

struct Chooser {
    QString dir;            // the chooser folder itself
    QString destDir;        // where an option's data/ contents belong (dir's parent)
    QStringList options;    // subfolder names, sorted case-insensitively
};

// Scan `modRoot` (a few levels deep) for the first chooser. Invalid (empty
// dir) when the mod has none - the usual case, checked cheaply.
Chooser find(const QString &modRoot);

struct ApplyResult {
    int         copied = 0;
    QStringList errors;
};

// Copy `option`'s data/ contents into ch.destDir, merging case-insensitively.
ApplyResult apply(const Chooser &ch, const QString &option);

} // namespace variant_picker

#endif // VARIANT_PICKER_H
