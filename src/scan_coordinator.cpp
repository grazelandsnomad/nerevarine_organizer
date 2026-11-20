#include "scan_coordinator.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <Qt>
#include <QtConcurrent/QtConcurrent>

#include "async_guarded.h"
#include "modroles.h"
#include "pluginparser.h"
#include "install_layout.h"
#include "scan_cache.h"

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

ScanCoordinator::ScanCoordinator(RowsProvider rows, QObject *parent)
    : QObject(parent), m_rows(std::move(rows))
{
    m_sizeScanTimer = new QTimer(this);
    m_sizeScanTimer->setSingleShot(true);
    m_sizeScanTimer->setInterval(200);
    connect(m_sizeScanTimer, &QTimer::timeout, this, &ScanCoordinator::runSizeScan);
}

std::optional<bool> ScanCoordinator::cachedFolderHasContent(const QString &path)
{
    if (path.isEmpty()) return std::nullopt;
    const auto it = m_hasContentCache.constFind(path);
    if (it == m_hasContentCache.constEnd()) return std::nullopt;
    scan_cache::Entry probe;
    probe.dirs = m_dataFolderDirs.value(path);
    if (probe.dirs.isEmpty() || !scan_cache::stillValid(probe)) {
        // Something under the mod moved, so the recorded answer is not an
        // answer any more. Drop it and let the caller look.
        m_hasContentCache.remove(path);
        m_dataFoldersCache.remove(path);
        m_dataFolderDirs.remove(path);
        m_bsaCache.remove(path);
        return std::nullopt;
    }
    return *it;
}

void ScanCoordinator::loadPersistedCache(const QString &path)
{
    m_cachePath = path;
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;      // no file yet is no cache
    const scan_cache::Cache c = scan_cache::parse(f.readAll());
    f.close();
    for (auto it = c.mods.constBegin(); it != c.mods.constEnd(); ++it) {
        m_dataFoldersCache.insert(it.key(), it.value().dataFolders);
        m_dataFolderDirs.insert(it.key(), it.value().dirs);
        m_hasContentCache.insert(it.key(), it.value().hasContent);
    }
    for (auto it = c.masters.constBegin(); it != c.masters.constEnd(); ++it)
        m_mastersCache.insert(it.key(), it.value());
}

void ScanCoordinator::savePersistedCache() const
{
    if (m_cachePath.isEmpty()) return;
    scan_cache::Cache c;
    for (auto it = m_dataFoldersCache.constBegin(); it != m_dataFoldersCache.constEnd(); ++it) {
        scan_cache::Entry e;
        e.dataFolders = it.value();
        e.dirs        = m_dataFolderDirs.value(it.key());
        e.hasContent  = m_hasContentCache.value(it.key(), false);
        if (e.dirs.isEmpty()) continue;   // nothing to check it against later
        c.mods.insert(it.key(), e);
    }
    c.masters = m_mastersCache;
    QFile f(m_cachePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(scan_cache::serialize(c));
}

void ScanCoordinator::scheduleSizeScan()
{
    if (m_sizeScanTimer) m_sizeScanTimer->start();
}

qint64 ScanCoordinator::sizeOf(const QString &path) const
{
    if (path.isEmpty()) return 0;
    {
        QMutexLocker lk(&m_sizeCache->mu);
        auto it = m_sizeCache->byPath.constFind(path);
        if (it != m_sizeCache->byPath.constEnd() && it.value().second >= 0)
            return it.value().second;
    }
    return computeDirSize(path);
}

QList<QPair<QString, QStringList>> ScanCoordinator::cachedDataFolders(
    const QString &path, const QStringList &exts)
{
    auto it = m_dataFoldersCache.find(path);
    // A cached answer is only as good as the directories it was read from. The
    // check is a stat each, which measured 0.002 ms against 6.8 ms to reopen
    // the directory, so it runs every time rather than only after a load.
    if (it != m_dataFoldersCache.end()) {
        scan_cache::Entry probe;
        probe.dirs = m_dataFolderDirs.value(path);
        if (!probe.dirs.isEmpty() && !scan_cache::stillValid(probe)) {
            m_dataFoldersCache.erase(it);
            m_dataFolderDirs.remove(path);
            m_hasContentCache.remove(path);
            m_bsaCache.remove(path);
            it = m_dataFoldersCache.end();
        }
    }
    if (it == m_dataFoldersCache.end()) {
        QStringList visited;
        auto folders = plugins::collectDataFolders(
            path, plugins::contentExtensions(), 6, &visited);
        it = m_dataFoldersCache.insert(path, std::move(folders));
        QList<QPair<QString, qint64>> dirs;
        dirs.reserve(visited.size());
        for (const QString &d : visited)
            dirs.append({d, QFileInfo(d).lastModified().toMSecsSinceEpoch()});
        m_dataFolderDirs.insert(path, dirs);
        // Free while the folder is still open from the walk above, and it is
        // what spares the row loop a directory open per mod next time.
        m_hasContentCache.insert(path, install_layout::folderHasContent(path));
    }
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
    m_dataFolderDirs.remove(path);
    m_hasContentCache.remove(path);
    m_bsaCache.remove(path);

    // Master cache is keyed per-file, so walk and drop any plugin path
    // under this modPath. Nothing else tells us a mod was uninstalled or
    // re-extracted; the mtime guard handles overwrite-within-session, but
    // explicit removal stops the table growing unbounded over a long run
    // of installs/uninstalls.
    if (path.isEmpty()) return;
    const QString prefix = path.endsWith('/') ? path : (path + '/');
    for (auto it = m_mastersCache.begin(); it != m_mastersCache.end(); ) {
        if (it.key() == path || it.key().startsWith(prefix))
            it = m_mastersCache.erase(it);
        else
            ++it;
    }
}

QStringList ScanCoordinator::cachedTes3Masters(const QString &pluginPath)
{
    if (pluginPath.isEmpty()) return {};

    // mtime gate: a plugin overwrite (re-extract, manual replace) bumps
    // mtime so the cache misses and we re-read. Missing file drops the entry.
    const QFileInfo fi(pluginPath);
    if (!fi.exists()) {
        m_mastersCache.remove(pluginPath);
        return {};
    }
    const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();

    auto it = m_mastersCache.find(pluginPath);
    if (it != m_mastersCache.end() && it.value().first == mtime)
        return it.value().second;

    QStringList masters = plugins::readTes3Masters(pluginPath);
    m_mastersCache.insert(pluginPath, {mtime, masters});
    return masters;
}

QStringList ScanCoordinator::cachedBsaFiles(const QString &path)
{
    if (path.isEmpty()) return {};
    auto it = m_bsaCache.find(path);
    if (it != m_bsaCache.end()) return it.value();

    QStringList found;
    QSet<QString> seen;
    QDirIterator dit(path,
                     {QStringLiteral("*.bsa"), QStringLiteral("*.BSA")},
                     QDir::Files, QDirIterator::Subdirectories);
    while (dit.hasNext()) {
        dit.next();
        const QString name = dit.fileName();
        if (seen.contains(name)) continue;
        seen.insert(name);
        found << name;
    }
    m_bsaCache.insert(path, found);
    return found;
}

void ScanCoordinator::warmDataFoldersCache(const QStringList &paths)
{
    QStringList coldPaths;
    for (const QString &mp : paths) {
        if (mp.isEmpty()) continue;
        // An entry with no recorded directories cannot be checked later, so
        // it is worthless to keep and counts as cold.
        if (!m_dataFoldersCache.contains(mp) || !m_bsaCache.contains(mp)
            || m_dataFolderDirs.value(mp).isEmpty())
            coldPaths << mp;
    }
    if (coldPaths.isEmpty()) return;

    // Worker touches no member state, so it ignores the passed self; the cache
    // inserts all happen back on the UI thread in the epilogue.
    struct R {
        QString path;
        QList<QPair<QString, QStringList>> dataFolders;
        QList<QPair<QString, qint64>>      dirs;
        bool        hasContent = false;
        QStringList bsaFiles;
    };
    async::guarded(this,
        [coldPaths](ScanCoordinator *) -> QList<R> {
            QList<R> results;
            results.reserve(coldPaths.size());
            for (const QString &mp : coldPaths) {
                R r;
                r.path = mp;
                QStringList visited;
                r.dataFolders = plugins::collectDataFolders(
                    mp, plugins::contentExtensions(), 6, &visited);
                r.dirs.reserve(visited.size());
                for (const QString &d : visited)
                    r.dirs.append({d, QFileInfo(d).lastModified().toMSecsSinceEpoch()});
                r.hasContent = install_layout::folderHasContent(mp);
                QSet<QString> seen;
                QDirIterator dit(mp,
                    {QStringLiteral("*.bsa"), QStringLiteral("*.BSA")},
                    QDir::Files, QDirIterator::Subdirectories);
                while (dit.hasNext()) {
                    dit.next();
                    const QString name = dit.fileName();
                    if (seen.contains(name)) continue;
                    seen.insert(name);
                    r.bsaFiles << name;
                }
                results.append(std::move(r));
            }
            return results;
        },
        [](ScanCoordinator *self, QList<R> results) {
            for (const auto &r : results) {
                self->m_dataFoldersCache.insert(r.path, r.dataFolders);
                self->m_dataFolderDirs.insert(r.path, r.dirs);
                self->m_hasContentCache.insert(r.path, r.hasContent);
                self->m_bsaCache.insert(r.path, r.bsaFiles);
            }
            // The run that paid for this walk is the one that should write it
            // down, so the next cold start does not pay again.
            self->savePersistedCache();
        });
}

void ScanCoordinator::runSizeScan()
{
    // Snapshot inputs on the UI thread. The worker never sees a row, only
    // paths; results come back keyed by mod path.
    struct Input {
        QString modPath;
        qint64  folderMtime;  // -1 if the folder is gone
        bool    installed;
    };
    QList<Input> inputs;
    const QList<ModEntry> rows = m_rows ? m_rows() : QList<ModEntry>();
    for (const ModEntry &row : rows) {
        if (!row.isMod()) continue;
        Input in;
        in.modPath     = row.modPath;
        in.installed   = (row.installStatus == int(InstallStatus::Installed));
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

    async::guarded(this,
        [inputs, cache = m_sizeCache](ScanCoordinator *) -> QHash<QString, qint64> {
            QHash<QString, qint64> results;
            for (const Input &in : inputs) {
                if (!in.installed || in.modPath.isEmpty()) continue;

                qint64 cachedMt  = -1;
                qint64 cachedSz  = -1;
                {
                    QMutexLocker lk(&cache->mu);
                    auto it = cache->byPath.constFind(in.modPath);
                    if (it != cache->byPath.constEnd()) {
                        cachedMt = it.value().first;
                        cachedSz = it.value().second;
                    }
                }

                qint64 size = -1;
                if (cachedMt == in.folderMtime && cachedSz >= 0) {
                    size = cachedSz;                         // cache hit
                } else if (in.folderMtime >= 0) {
                    size = computeDirSize(in.modPath);       // slow path
                    QMutexLocker lk(&cache->mu);
                    cache->byPath.insert(in.modPath, { in.folderMtime, size });
                }
                if (size >= 0) results.insert(in.modPath, size);
            }
            return results;
        },
        [](ScanCoordinator *self, QHash<QString, qint64> results) {
            self->finishSizeScan(results);
        });
}

void ScanCoordinator::finishSizeScan(const QHash<QString, qint64> &bytesByPath)
{
    emit sizesScanned(bytesByPath);

    m_sizeScanInFlight = false;
    if (m_sizeScanPending) {
        m_sizeScanPending = false;
        scheduleSizeScan();
    }
}
