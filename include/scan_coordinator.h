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

// Owns the size-scan debounce timer + cache + in-flight tracking, plus the
// data-folders cache. Neither blocks the UI: the size scan runs on a worker
// (QtConcurrent::run); the data-folders cache is UI-thread only but hands out
// a snapshot for off-thread reads.
class ScanCoordinator : public QObject {
    Q_OBJECT
public:
    ScanCoordinator(QListWidget *list, QObject *parent = nullptr);

    // Debounced trigger (200 ms single-shot). Every checkbox toggle fires
    // saveModList -> this, so rapid calls coalesce into one worker run. No-op
    // if the timer isn't created yet.
    void scheduleSizeScan();

    // Cache hit, or synchronous compute for callers that need a size before
    // the async scan lands (e.g. the modlist summary). Recursive size of
    // `path` in bytes, 0 if empty.
    qint64 sizeOf(const QString &path) const;

    // UI-thread only.
    QList<QPair<QString, QStringList>> cachedDataFolders(
        const QString &path, const QStringList &exts);
    void invalidateDataFoldersCache(const QString &path);

    // Value-copy snapshot for safe off-thread reads.
    QHash<QString, QList<QPair<QString, QStringList>>>
        dataFoldersSnapshot() const { return m_dataFoldersCache; }

    // Off-thread pre-warm: collectDataFolders for each uncached path, results
    // posted back to the UI thread. Run after loadModList so the first
    // remove/inspect hits a warm cache, not a cold disk scan.
    void warmDataFoldersCache(const QStringList &paths);

    // Recursive BSA filename scan (deduped by basename), UI-thread only.
    // Without the cache, syncOpenMWConfig walked QDirIterator-Subdirectories
    // per mod on every save - that was inside the add/edit UI freeze.
    // invalidateDataFoldersCache clears this too (shared key).
    QStringList cachedBsaFiles(const QString &path);

    // TES3 master cache keyed by (plugin path, mtime). reconcileLoadOrder
    // (run per saveModList) used to call readTes3Masters per plugin: cheap
    // alone (~1 KB read), but 100 file opens per save on a 100-plugin order.
    // Stale entries for uninstalled mods get pruned via
    // invalidateDataFoldersCache on the containing modPath.
    QStringList cachedTes3Masters(const QString &pluginPath);

private:
    void runSizeScan();
    void applySizeResults(const QHash<QString, qint64> &bytesByPath);

    QListWidget *m_list = nullptr;
    QTimer      *m_sizeScanTimer = nullptr;

    // modPath -> (folder mtime ms, total bytes). mtime -1 = unknown.
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
