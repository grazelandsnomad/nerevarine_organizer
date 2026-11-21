#include "scan_coordinator.h"

#include <QDir>
#include <QSaveFile>
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
    return *it;   // trusted as it stands: see cachedDataFolders
}

bool ScanCoordinator::modsRootMoved(const QString &root) const
{
    if (root.isEmpty()) return false;
    const QString key = QDir::cleanPath(root);
    const auto it = m_roots.constFind(key);
    // Never looked, so look: one verify pass, and then this is quiet until
    // the root really moves.
    if (it == m_roots.constEnd()) return true;
    return !scan_cache::folderAsRecorded(key, *it);
}

void ScanCoordinator::noteModsRootChecked(const QString &root)
{
    if (root.isEmpty()) return;
    const QString key = QDir::cleanPath(root);
    const QFileInfo fi(key);
    if (!fi.isDir()) return;            // nothing to record a time for
    m_roots.insert(key, fi.lastModified().toMSecsSinceEpoch());
}

qint64 ScanCoordinator::recordedFolderTime(const QString &path) const
{
    // The mod folder's own recorded time. One stat checks it, against the 7648
    // a full check of every recorded directory needs, and it is what catches
    // the two cases that matter: the folder deleted, and its contents changed
    // from outside. A file added deeper inside it is not caught until the
    // entry is invalidated or the cache is rebuilt.
    for (const auto &d : m_dataFolderDirs.value(path))
        if (d.first == path) return d.second;
    return -1;
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
        // Unset stays unset: an entry nobody measured must reach
        // cachedFolderHasContent as "I do not know", so the row loop sends it
        // to the probe instead of calling the mod uninstalled.
        // Bound once: two it.value() calls look like two different optionals
        // to the analyzer, and the second one is then an unchecked access.
        const std::optional<bool> &has = it.value().hasContent;
        if (has.has_value()) m_hasContentCache.insert(it.key(), *has);
    }
    for (auto it = c.masters.constBegin(); it != c.masters.constEnd(); ++it)
        m_mastersCache.insert(it.key(), it.value());
    m_roots = c.roots;
}

void ScanCoordinator::savePersistedCache() const
{
    if (m_cachePath.isEmpty()) return;
    scan_cache::Cache c;
    for (auto it = m_dataFoldersCache.constBegin(); it != m_dataFoldersCache.constEnd(); ++it) {
        scan_cache::Entry e;
        e.dataFolders = it.value();
        e.dirs        = m_dataFolderDirs.value(it.key());
        // contains, not value-with-default: defaulting here is what wrote a
        // confident "empty" for every folder this process never looked at.
        if (m_hasContentCache.contains(it.key()))
            e.hasContent = m_hasContentCache.value(it.key());
        if (e.dirs.isEmpty()) continue;   // nothing to check it against later
        c.mods.insert(it.key(), e);
    }
    c.masters = m_mastersCache;
    c.roots   = m_roots;
    // Written whole or not at all. This file is over a megabyte and the disk
    // it lives on has run out of room mid-write before; a truncated one would
    // parse as no cache at all and cost a full cold rebuild.
    QSaveFile f(m_cachePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(scan_cache::serialize(c));
    f.commit();
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
    // The written-down answer is used here without being checked first.
    // Checking means a stat per directory the scan recorded, and measured
    // against the real cache that is 7648 directories at 8.1 ms each: 62
    // seconds for one pass. Nothing that expensive belongs on the UI thread,
    // and an earlier revision that checked on every call turned a switch into
    // a fifteen-minute freeze. Two things keep it honest instead:
    // invalidateDataFoldersCache, for every folder the app itself changes, and
    // MainWindow's background re-verify, which stats each mod's own folder on
    // a worker (410 stats, ~3 s, nobody waiting) and invalidates the ones that
    // moved. A change made behind the app's back is therefore picked up on the
    // next load rather than instantly, which is the trade this disk forces.
    auto it = m_dataFoldersCache.find(path);
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
