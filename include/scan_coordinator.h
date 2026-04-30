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
};

#endif // SCAN_COORDINATOR_H
