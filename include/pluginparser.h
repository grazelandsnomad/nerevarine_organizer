#ifndef PLUGINPARSER_H
#define PLUGINPARSER_H

// PluginParser - filesystem + TES3-binary helpers extracted from MainWindow.
// No widgets, no signals, no MainWindow state; free-function-callable from
// anywhere including unit tests.

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace plugins {

// File extensions the Morrowind / OpenMW engines load as plugins. Shared
// across collectDataFolders callers so everyone scans for the same set.
inline QStringList contentExtensions()
{
    return {".esp", ".esm", ".omwaddon", ".omwscripts"};
}

// Walk `root` (up to `maxDepth` levels) and return every directory holding
// a file ending in one of `exts`, as (absolute-dir-path, [filenames]) pairs.
//
// Recursive: a shallow scan misses nested layouts (FOMOD options,
// "Data Files/…" wrappers, OAAB-style placement). Skips fomod, .git, .svn,
// docs, documentation so installer configs / VCS metadata aren't listed as
// data= paths.
QList<QPair<QString, QStringList>>
    collectDataFolders(const QString &root,
                       const QStringList &exts,
                       int maxDepth = 6);

// Find folders under `root` that look like asset roots: containing a
// recognized resource subdir (textures, meshes, splash, fonts, sound, music,
// icons, bookart, mwscript, video, shaders) or a .bsa. Fallback for pure
// resource mods (menu replacers, retextures, sound packs) with no plugin
// files, which would otherwise drop out of the data= list.
QStringList collectResourceFolders(const QString &root, int maxDepth = 4);

// Parse the MAST subrecords from a Morrowind TES3 plugin header. Returns the
// masters the plugin must load after. Empty if the file won't open, isn't
// TES3 (wrong magic), or the header is truncated. Filenames come back
// as-written; the engine matches case-insensitively (callers usually
// .toLower() first).
QStringList readTes3Masters(const QString &path);

} // namespace plugins

#endif // PLUGINPARSER_H
