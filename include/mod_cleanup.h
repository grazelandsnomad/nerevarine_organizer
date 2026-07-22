#pragma once

// mod_cleanup - the pure half of "Clean Up Mod Folders".
//
// Mods dirs accumulate folders nothing points at any more: older builds left by
// upgrades before the sibling matchers caught them, extract dirs from installs
// that never completed, leftovers from mods removed while keeping files on disk.
// One real library had 96 such folders holding 8.6 GiB.
//
// Deciding WHAT is unreferenced is a set operation on paths, so it lives here
// with no filesystem and no widget access and is unit-tested directly. The
// caller owns gathering the inputs (every profile's modlist, the game config)
// and the deleting.

#include <QString>
#include <QStringList>

namespace mod_cleanup {

// Entries of `onDiskDirs` (plain folder names, direct children of `modsDir`)
// that no path in `referencedPaths` is at or under.
//
// "At or under" is mandatory, not a nicety: dive-into-single-subdir installs
// register the row at "<modsDir>/<wrapper>/<inner>", so the wrapper is live even
// though no row names it. Paths outside `modsDir` are ignored. Comparison is via
// QDir::cleanPath; an empty `modsDir` yields an empty result, so a caller that
// has lost its mods dir can never be talked into deleting anything.
QStringList unreferencedFolders(const QString     &modsDir,
                                const QStringList &onDiskDirs,
                                const QStringList &referencedPaths);

} // namespace mod_cleanup
