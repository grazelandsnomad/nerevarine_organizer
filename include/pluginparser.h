#ifndef PLUGINPARSER_H
#define PLUGINPARSER_H

// PluginParser - pure filesystem + TES3-binary helpers extracted from
// MainWindow.  No Qt widgets, no signals, no MainWindow state.  Everything
// in here is free-function-callable from anywhere, including unit tests.

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace plugins {

// Canonical list of file extensions the Morrowind / OpenMW engines load as
// plugins.  Shared across collectDataFolders callers so everyone scans for
// the same set.
inline QStringList contentExtensions()
{
    return {".esp", ".esm", ".omwaddon", ".omwscripts"};
}

// Recursively walk `root` (up to `maxDepth` levels) and return every
// directory that contains one or more files ending in one of `exts`.
// Each entry is a pair of (absolute-dir-path, [filenames-in-that-dir]).
//
// Deliberately recursive so nested mod layouts (FOMOD options,
// "Data Files/…" wrappers, layered directories) are all picked up - a
// shallow scan misses OAAB-style plugin placement.  Skips well-known
// non-data subdirectories (`fomod`, `.git`, `.svn`, `docs`,
// `documentation`) so installer configs and VCS metadata don't get
// listed as data= paths.
QList<QPair<QString, QStringList>>
    collectDataFolders(const QString &root,
                       const QStringList &exts,
                       int maxDepth = 6);

// Find folders under `root` that look like OpenMW asset roots - i.e. they
// contain at least one recognized resource subdirectory (textures, meshes,
// splash, fonts, sound, music, icons, bookart, mwscript, video, bookart,
// shaders) or a `.bsa` archive.  Used as a fallback for pure resource mods
// (main menu replacers, retextures, sound packs) that contain no plugin
// files - otherwise they'd be silently dropped from the data= list.
QStringList collectResourceFolders(const QString &root, int maxDepth = 4);

// Parse the MAST subrecords out of a Morrowind TES3 plugin header
// (top-level `TES3` record).  Returns the filenames of the masters the
// plugin expects to load after.  Empty list if:
//   · the file can't be opened
//   · it isn't a TES3-format plugin (wrong magic)
//   · the header is truncated
// Filenames are returned as-written in the plugin (the engine matches
// case-insensitively - callers usually `.toLower()` before comparing).
QStringList readTes3Masters(const QString &path);

} // namespace plugins

#endif // PLUGINPARSER_H
