#ifndef SCAN_CACHE_H
#define SCAN_CACHE_H

// scan_cache - the folder scan, written down so a cold start is a warm one.
//
// Building the plugin-name-to-path map for a 409-mod library measured 16.8 s
// of a profile switch, and reading those plugins' masters another 5.7 s. Both
// are facts about folders that have not changed since the last run, and both
// used to die with the process.
//
// Each entry records every directory the scan opened and the time it had.
// Checking all of them would be the thorough thing to do, and on this kind of
// library it is not affordable: measured against the real cache, 7648 recorded
// directories at 8.1 ms each is 62 seconds for one pass, and 410 stats of the
// mod folders alone is 11 seconds. Neither can run on a profile switch.
//
// So three things guard the cache, in rising cost and falling frequency:
//
//  1. The app's own invalidation (MainWindow::invalidateModScan), free, for
//     every folder the app itself changes.
//  2. One stat of the mods root per load. Adding or removing a mod folder
//     moves the root's own time, so this catches a library edited while the
//     app was closed - which is the case invalidation cannot see.
//  3. Only when that one stat disagrees: a stat of each mod folder, on a
//     worker, to find which ones. Eleven seconds, nobody waiting, rare.
//
// What is deliberately not caught is a file changed deep inside a mod folder
// without the folder itself or the root moving. That costs a full pass to
// find, and a full pass costs a minute of a switch.
//
// Qt Core only, no I/O beyond one stat in folderAsRecorded, so the parsing and
// the staleness rule are testable without a window.

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
    // The install probe's answer for the mod folder itself: does it hold
    // anything. Recorded here because the scan had the folder open anyway,
    // and because asking again costs a directory open - 103 ms on the
    // spinning disk this library lives on, times 409 mods.
    bool                               hasContent = false;
};

// The two caches, in the shapes ScanCoordinator already holds them: mods keyed
// by mod folder, masters keyed by plugin FILE - the masters cache is per file
// on purpose, so a plugin overwritten inside a warm mod folder is still caught.
struct Cache {
    QHash<QString, Entry>                      mods;
    QHash<QString, QPair<qint64, QStringList>> masters;
    // Mods root → the time it carried when every mod under it was last
    // checked against disk. Step 2 above compares one stat to this.
    QHash<QString, qint64>                     roots;
};

// Malformed, truncated or absent input is an empty cache, which is exactly
// how the app behaved before this file existed.
Cache      parse(const QByteArray &json);
QByteArray serialize(const Cache &cache);

// True while one directory still carries the time it was recorded with. One
// stat. Given a mod folder and the time Entry::dirs recorded for it, this is
// step 3 above; given the mods root and the time it was last checked at, it is
// step 2. A path that is gone, or was never recorded (a time below zero), is
// not as recorded.
//
// Times are milliseconds, so a change made inside the same millisecond as the
// recording is invisible. A scan of a mod folder takes orders of magnitude
// longer than that, so this does not come up in practice - but it is the
// reason this is a cache with a cheap check rather than a guarantee.
bool folderAsRecorded(const QString &dir, qint64 recordedMs);

} // namespace scan_cache

#endif // SCAN_CACHE_H
