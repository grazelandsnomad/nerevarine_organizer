#ifndef SCAN_COORDINATOR_H
#define SCAN_COORDINATOR_H

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>

class QListWidget;
class QTimer;

// Owns the size-scan debounce timer + cache + in-flight tracking, and the
// data-folders cache. Both scans avoid blocking the UI: the size scan runs
// on a worker via QtConcurrent::run; the data-folders cache is UI-thread
// only but offers a snapshot accessor for off-thread reads.
class ScanCoordinator : public QObject {
    Q_OBJECT
public:
    ScanCoordinator(QListWidget *list, QObject *parent = nullptr);

    // Debounced trigger (200 ms single-shot). Coalesces rapid successive
    // calls (every checkbox toggle fires saveModList → this) into one
    // worker run. Safe to call before the timer is created - no-op then.
    void scheduleSizeScan();

    // Cache lookup or synchronous compute, for callers that need a size
    // before the async scan has landed (e.g. the modlist summary).
    // Returns the recursive size of `path` in bytes, or 0 if path is empty.
    qint64 sizeOf(const QString &path) const;

    // Data-folders cache - UI-thread only.
    QList<QPair<QString, QStringList>> cachedDataFolders(
        const QString &path, const QStringList &exts);
    void invalidateDataFoldersCache(const QString &path);

    // Value-copy snapshot of the data-folders cache for safe off-thread reads.
    QHash<QString, QList<QPair<QString, QStringList>>>
        dataFoldersSnapshot() const { return m_dataFoldersCache; }

    // Off-thread pre-warm: calls plugins::collectDataFolders for each path
    // not yet in the cache and posts the results back to the UI thread.
    // Used after loadModList so that the first user-triggered remove/inspect
    // hits a warm cache instead of a cold filesystem scan.
    void warmDataFoldersCache(const QStringList &paths);

    // Recursive BSA filename scan (deduped by basename) - UI-thread only.
    // Caching matters because syncOpenMWConfig used to run a full
    // QDirIterator-Subdirectories walk per installed mod on every save,
    // and that landed inside the user-perceived UI freeze on add/edit.
    // Invalidated through invalidateDataFoldersCache (single cache key
    // for both data + BSA scans on the same path).
    QStringList cachedBsaFiles(const QString &path);

    // TES3 master record cache, keyed by (plugin path, mtime).  Each
    // saveModList runs reconcileLoadOrder, which used to call
    // plugins::readTes3Masters per plugin per call.  Cheap individually
    // (~1 KB read), but on a 100-plugin order that's 100 file opens
    // every save; this cache zeros it out for warm hits.  Stale entries
    // for uninstalled mods are pruned when invalidateDataFoldersCache
    // is called for a containing modPath.
    QStringList cachedTes3Masters(const QString &pluginPath);

private:
    void runSizeScan();
    void applySizeResults(const QHash<QString, qint64> &bytesByPath);

    QListWidget *m_list = nullptr;
    QTimer      *m_sizeScanTimer = nullptr;

    // modPath → (folder mtime ms since epoch, total bytes). mtime -1 = unknown.
    QHash<QString, QPair<qint64, qint64>> m_sizeCache;
    mutable QMutex                        m_sizeCacheMu;
    bool                                  m_sizeScanInFlight = false;
    bool                                  m_sizeScanPending  = false;

    // Data-folders cache: modPath → collectDataFolders(path, contentExtensions()).
    // Written and read on the UI thread only; workers receive a value-copy
    // snapshot via dataFoldersSnapshot().
    QHash<QString, QList<QPair<QString, QStringList>>> m_dataFoldersCache;

    // BSA basename cache: modPath → list of *.bsa filenames found anywhere
    // under modPath.  UI-thread only.  Cleared in lockstep with
    // m_dataFoldersCache via invalidateDataFoldersCache.
    QHash<QString, QStringList> m_bsaCache;

    // TES3 master cache: plugin file path → (mtime ms since epoch,
    // list of MAST-declared parent filenames).  Keyed at the FILE
    // level, not modPath, so mtime gating handles file-level edits
    // even when the containing modPath cache is warm.  UI-thread only.
    QHash<QString, QPair<qint64, QStringList>> m_mastersCache;
};

#endif // SCAN_COORDINATOR_H
