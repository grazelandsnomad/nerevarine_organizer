#ifndef SCAN_CACHE_H
#define SCAN_CACHE_H

// scan_cache - the folder scan, written down so a cold start is a warm one.
//
// Building the plugin-name-to-path map for a 409-mod library measured 16.8 s
// of a profile switch, and reading those plugins' masters another 5.7 s. Both
// are facts about folders that have not changed since the last run, and both
// used to die with the process.
//
// Checking a written-down answer is not the same price as recomputing it.
// Measured on cold directories: 296 stats cost 0.6 ms, 296 opendirs cost
// 2019 ms. So each entry records every directory the scan opened and the
// modification time it had, and is trusted only while all of them still
// match - a plugin added anywhere under the mod changes the time of the
// directory holding it, and a new directory changes its parent's.
//
// The app's own invalidation (MainWindow::invalidateModScan) covers what the
// app does. This covers the hours it was not running, which nothing else can.
//
// Qt Core only, no I/O beyond the stats in stillValid, so the parsing and the
// staleness rule are testable without a window.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace scan_cache {

struct Entry {
    // collectDataFolders' answer: (absolute dir, plugin filenames).
    QList<QPair<QString, QStringList>> dataFolders;
    // Every directory that walk opened, with its modification time in ms.
    QList<QPair<QString, qint64>>      dirs;
};

// The two caches, in the shapes ScanCoordinator already holds them: mods keyed
// by mod folder, masters keyed by plugin FILE - the masters cache is per file
// on purpose, so a plugin overwritten inside a warm mod folder is still caught.
struct Cache {
    QHash<QString, Entry>                      mods;
    QHash<QString, QPair<qint64, QStringList>> masters;
};

// Malformed, truncated or absent input is an empty cache, which is exactly
// how the app behaved before this file existed.
Cache      parse(const QByteArray &json);
QByteArray serialize(const Cache &cache);

// True while every directory the scan opened still carries the time it had.
// One stat per directory, which is what makes this worth doing at all.
bool stillValid(const Entry &entry);

} // namespace scan_cache

#endif // SCAN_CACHE_H
