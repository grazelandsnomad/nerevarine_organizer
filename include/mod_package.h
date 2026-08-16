#ifndef MOD_PACKAGE_H
#define MOD_PACKAGE_H

// Pack a mod folder into an archive - the shape a mod page expects.
//
// Written for the translation mods this app generates: once "Bandit Chief" is
// "Lider Bandido" in a plugin, the remaining work to publish it is entirely
// mechanical, and a manager that produced the folder may as well produce the
// upload too.
//
// -- The layout is the whole point ------------------------------------
//
// A mod archive holds the mod's files AT ITS ROOT, not inside a wrapper
// folder named after the mod. Get that wrong and every installer downstream -
// this one included - has to guess whether the top-level directory is part of
// the mod or packaging noise, which is exactly the "dive into a single
// subdirectory" heuristic that keeps needing exceptions (install_layout.h).
// So the archive is built with the mod folder as the working directory and
// "." as the argument, which stores entries relative to it and preserves any
// internal Data/ structure the mod already has.
//
// 7z does the compressing. It is already a hard dependency for extraction, so
// this adds no new one, and it writes .zip as happily as .7z - Nexus accepts
// both, and zip is the default because every user on every platform can open
// one without installing anything.

#include <QString>

namespace mod_package {

enum class Format { Zip, SevenZip };

// Format implied by a path's suffix. Anything that is not ".7z" is Zip: the
// safer default, and the one a stray or missing suffix should land on.
Format formatForPath(const QString &path);

// A filename to offer in the save dialog: the mod's name with anything a
// filesystem or an upload form would object to replaced. Carries the suffix
// for `f`.
QString suggestedFileName(const QString &modName, Format f);

struct Result {
    bool    ok = false;
    QString archivePath;
    qint64  bytes = 0;      // size of the finished archive
    QString error;          // set only when ok is false
};

// Packs `modFolder`'s CONTENTS into `dstPath`. Blocking, and slow for a large
// mod - call it off the UI thread.
//
// An existing file at dstPath is removed first: 7z would otherwise ADD to it,
// quietly producing an archive holding two versions of the mod.
Result create(const QString &modFolder, const QString &dstPath);

} // namespace mod_package

#endif // MOD_PACKAGE_H
