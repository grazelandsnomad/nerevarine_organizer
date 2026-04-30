#include "scan_coordinator.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <Qt>
#include <QtConcurrent/QtConcurrent>

#include "modroles.h"
#include "pluginparser.h"

namespace {
qint64 computeDirSize(const QString &path)
{
    if (path.isEmpty() || !QFileInfo(path).isDir()) return 0;
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}
} // namespace

ScanCoordinator::ScanCoordinator(QListWidget *list, QObject *parent)
    : QObject(parent), m_list(list)
{
    m_sizeScanTimer = new QTimer(this);
    m_sizeScanTimer->setSingleShot(true);
    m_sizeScanTimer->setInterval(200);
    connect(m_sizeScanTimer, &QTimer::timeout, this, &ScanCoordinator::runSizeScan);
}

void ScanCoordinator::scheduleSizeScan()
{
    if (m_sizeScanTimer) m_sizeScanTimer->start();
}

qint64 ScanCoordinator::sizeOf(const QString &path) const
{
    if (path.isEmpty()) return 0;
    {
        QMutexLocker lk(&m_sizeCacheMu);
        auto it = m_sizeCache.constFind(path);
        if (it != m_sizeCache.constEnd() && it.value().second >= 0)
            return it.value().second;
    }
    return computeDirSize(path);
}

QList<QPair<QString, QStringList>> ScanCoordinator::cachedDataFolders(
    const QString &path, const QStringList &exts)
{
    auto it = m_dataFoldersCache.find(path);
    if (it == m_dataFoldersCache.end())
        it = m_dataFoldersCache.insert(path,
                 plugins::collectDataFolders(path, plugins::contentExtensions()));
    const auto &all = it.value();
    if (exts == plugins::contentExtensions()) return all;
    QList<QPair<QString, QStringList>> result;
    result.reserve(all.size());
    for (const auto &p : all) {
        QStringList filtered;
        for (const QString &f : p.second)
            for (const QString &e : exts)
                if (f.endsWith(e, Qt::CaseInsensitive)) { filtered << f; break; }
        if (!filtered.isEmpty()) result.append({p.first, filtered});
    }
    return result;
}

void ScanCoordinator::invalidateDataFoldersCache(const QString &path)
{
    m_dataFoldersCache.remove(path);
}

void ScanCoordinator::warmDataFoldersCache(const QStringList &paths)
{
    QStringList coldPaths;
    for (const QString &mp : paths) {
        if (!mp.isEmpty() && !m_dataFoldersCache.contains(mp))
            coldPaths << mp;
    }
    if (coldPaths.isEmpty()) return;

    QPointer<ScanCoordinator> safeSelf(this);
    (void)QtConcurrent::run([safeSelf, coldPaths]() {
        using R = QPair<QString, QList<QPair<QString, QStringList>>>;
        QList<R> results;
        results.reserve(coldPaths.size());
        for (const QString &mp : coldPaths)
            results.append({mp, plugins::collectDataFolders(
                                    mp, plugins::contentExtensions())});
        QMetaObject::invokeMethod(safeSelf.data(), [safeSelf, results]() {
            if (!safeSelf) return;
            for (const auto &r : results)
                safeSelf->m_dataFoldersCache.insert(r.first, r.second);
        }, Qt::QueuedConnection);
    });
}

void ScanCoordinator::runSizeScan()
{
    // Capture immutable inputs on the UI thread. The worker never touches
    // QListWidget items - items can be deleted/reordered concurrently.
    // Results come back keyed by ModPath, which is stable as long as an
    // item exists.
    struct Input {
        QString modPath;
        qint64  folderMtime;  // -1 if the folder is gone
        bool    installed;
    };
    QList<Input> inputs;
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        Input in;
        in.modPath    = item->data(ModRole::ModPath).toString();
        in.installed  = (item->data(ModRole::InstallStatus).toInt() == 1);
        in.folderMtime = in.installed
            ? QFileInfo(in.modPath).lastModified().toMSecsSinceEpoch()
            : -1;
        inputs.append(in);
    }

    if (m_sizeScanInFlight) {
        m_sizeScanPending = true;
        return;
    }
    m_sizeScanInFlight = true;

    QPointer<ScanCoordinator> safeSelf(this);
    (void)QtConcurrent::run([safeSelf, inputs]() {
        if (!safeSelf) return;
        ScanCoordinator *self = safeSelf.data();

        QHash<QString, qint64> results;
        for (const Input &in : inputs) {
            if (!in.installed || in.modPath.isEmpty()) continue;

            qint64 cachedMt  = -1;
            qint64 cachedSz  = -1;
            {
                QMutexLocker lk(&self->m_sizeCacheMu);
                auto it = self->m_sizeCache.constFind(in.modPath);
                if (it != self->m_sizeCache.constEnd()) {
                    cachedMt = it.value().first;
                    cachedSz = it.value().second;
                }
            }

            qint64 size = -1;
            if (cachedMt == in.folderMtime && cachedSz >= 0) {
                size = cachedSz;                         // cache hit
            } else if (in.folderMtime >= 0) {
                size = computeDirSize(in.modPath);       // slow path
                QMutexLocker lk(&self->m_sizeCacheMu);
                self->m_sizeCache.insert(in.modPath,
                                          { in.folderMtime, size });
            }
            if (size >= 0) results.insert(in.modPath, size);
        }

        QMetaObject::invokeMethod(safeSelf.data(), [safeSelf, results]{
            if (!safeSelf) return;
            safeSelf->applySizeResults(results);
        }, Qt::QueuedConnection);
    });
}

void ScanCoordinator::applySizeResults(const QHash<QString, qint64> &bytesByPath)
{
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 1) {
            item->setData(ModRole::ModSize, QVariant());
            continue;
        }
        QString path = item->data(ModRole::ModPath).toString();
        auto it = bytesByPath.constFind(path);
        if (it != bytesByPath.constEnd())
            item->setData(ModRole::ModSize, it.value());
    }
    m_list->viewport()->update();

    m_sizeScanInFlight = false;
    if (m_sizeScanPending) {
        m_sizeScanPending = false;
        scheduleSizeScan();
    }
}
