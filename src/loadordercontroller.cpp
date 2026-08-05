#include "loadordercontroller.h"

#include "async_guarded.h"
#include "pluginparser.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <QtConcurrent>

// Filesystem walk feeding conflict_direction::resolve. Kept in the .cpp so the
// public header stays moc-free. The walk is the only expensive part; deciding
// who wins is pure and lives in conflict_direction, where it is tested.
class ConflictScanWorker : public QThread {
    static constexpr int kMaxShownFiles = 8;
public:
    explicit ConflictScanWorker(const QList<conflict_direction::Mod> &mods,
                                QObject *parent = nullptr)
        : QThread(parent), m_mods(mods) {}

    // Read only after finished() fires.
    const QHash<QString, conflict_direction::Directions> &results() const
    { return m_results; }
    const QHash<QString, QList<plugin_records::RecordClash>> &recordClashes() const
    { return m_recordClashes; }

protected:
    void run() override
    {
        // Reverse map: lowercased relative path -> indices of the mods shipping
        // it. Indices, not paths, because the index IS the load-order position
        // resolve() needs to tell winner from loser.
        QHash<QString, QList<int>> fileOwners;
        fileOwners.reserve(4096);

        QList<plugin_records::ModPlugins> modPlugins;
        modPlugins.reserve(m_mods.size());

        for (int i = 0; i < m_mods.size(); ++i) {
            // ~LoadOrderController requests interruption then wait()s; without
            // these checks a big scan ignores it and the wait just times out.
            if (isInterruptionRequested()) return;
            QDir dir(m_mods[i].path);
            if (!dir.exists()) continue;
            plugin_records::ModPlugins mp{m_mods[i].path, m_mods[i].name, {}, {}};
            QDirIterator dit(m_mods[i].path, QDir::Files | QDir::NoDotAndDotDot,
                             QDirIterator::Subdirectories);
            while (dit.hasNext()) {
                if (isInterruptionRequested()) return;
                dit.next();
                QString rel = dir.relativeFilePath(dit.filePath()).toLower();
                if (rel.endsWith(".txt")) continue;  // .txt never triggers conflicts
                fileOwners[rel].append(i);

                // Record-level pass. scan() rejects anything that isn't a
                // TES4-family plugin on the 4-byte magic, so a Morrowind list
                // costs one open per plugin and nothing else.
                if (rel.endsWith(QLatin1String(".esm"))
                 || rel.endsWith(QLatin1String(".esp"))
                 || rel.endsWith(QLatin1String(".esl"))) {
                    const auto pl = plugin_records::scan(dit.filePath());
                    if (pl.valid && !pl.overrides.isEmpty()) {
                        mp.pluginNames << QFileInfo(dit.filePath()).fileName();
                        mp.overrides += pl.overrides;
                    }
                }
            }
            if (!mp.overrides.isEmpty()) modPlugins.append(mp);
        }
        if (isInterruptionRequested()) return;

        m_results = conflict_direction::resolve(m_mods, fileOwners, kMaxShownFiles);
        m_recordClashes = plugin_records::findClashes(modPlugins);
    }

private:
    QList<conflict_direction::Mod>                        m_mods;
    QHash<QString, conflict_direction::Directions>        m_results;
    QHash<QString, QList<plugin_records::RecordClash>>    m_recordClashes;
};

LoadOrderController::LoadOrderController(QObject *parent)
    : QObject(parent), m_mastersCacheMu(new QMutex) {}

LoadOrderController::~LoadOrderController()
{
    // Worker is parented to us, so ~QObject() deletes it. But if it's still
    // running at shutdown, block briefly first or run() dereferences a
    // half-destroyed worker.
    if (m_activeScanner && m_activeScanner->isRunning()) {
        m_activeScanner->requestInterruption();
        m_activeScanner->wait(2000);
    }
    delete m_mastersCacheMu;
}

void LoadOrderController::scanConflicts(
    const QList<conflict_direction::Mod> &modsInLoadOrder)
{
    // Drop the call if a scan is already running; the caller is a debounced
    // timer, so the next edit retriggers.
    if (m_activeScanner && m_activeScanner->isRunning())
        return;

    delete m_activeScanner;
    m_activeScanner = new ConflictScanWorker(modsInLoadOrder, this);
    connect(m_activeScanner, &QThread::finished, this, [this] {
        // Copy results before deleting the worker so a Direct signal still
        // has a valid reference.
        const QHash<QString, conflict_direction::Directions> results =
            m_activeScanner->results();
        const QHash<QString, QList<plugin_records::RecordClash>> clashes =
            m_activeScanner->recordClashes();
        m_activeScanner->deleteLater();
        m_activeScanner = nullptr;
        emit conflictsScanned(results, clashes);
    });
    m_activeScanner->start(QThread::LowPriority);
}

void LoadOrderController::scanMissingMasters(
    const QList<MastersInput> &enabledMods,
    const QSet<QString> &availableLower)
{
    if (m_mastersScanInFlight) {
        // Buffer the newest request, dropping any older pending one.
        m_pendingMastersInput     = enabledMods;
        m_pendingMastersAvailable = availableLower;
        m_mastersScanPending      = true;
        return;
    }
    m_mastersScanInFlight = true;

    async::guarded(this,
        [enabledMods, availableLower](LoadOrderController *self)
            -> QHash<QString, QPair<bool, QStringList>> {
        // Base Morrowind masters live in no mod; treat as always available or
        // the scan flags them.
        static const QSet<QString> baseMasters = {
            "morrowind.esm", "tribunal.esm", "bloodmoon.esm"
        };

        QHash<QString, QPair<bool, QStringList>> byModPath;
        for (const MastersInput &e : enabledMods) {
            QStringList entries;
            bool anyMissing = false;

            for (const auto &plug : e.plugins) {
                const QString &pluginPath = plug.first;
                const QString &pluginName = plug.second;

                // mtime-keyed cache: don't re-read plugins unchanged since
                // the last scan.  m_mastersCacheMu makes the cache the one
                // thread-safe member the worker may touch.
                const qint64 mtime = QFileInfo(pluginPath)
                                        .lastModified().toMSecsSinceEpoch();
                QStringList masters;
                bool hit = false;
                {
                    QMutexLocker lk(self->m_mastersCacheMu);
                    auto it = self->m_mastersCache.constFind(pluginPath);
                    if (it != self->m_mastersCache.constEnd()
                     && it.value().first == mtime) {
                        masters = it.value().second;
                        hit = true;
                    }
                }
                if (!hit) {
                    masters = plugins::readTes3Masters(pluginPath);
                    QMutexLocker lk(self->m_mastersCacheMu);
                    self->m_mastersCache.insert(pluginPath, { mtime, masters });
                }

                QStringList missing;
                for (const QString &m : masters) {
                    const QString lm = m.toLower();
                    if (availableLower.contains(lm)) continue;
                    if (baseMasters.contains(lm))    continue;
                    missing << m;
                }
                if (!missing.isEmpty()) {
                    anyMissing = true;
                    entries << pluginName + "\t" + missing.join('\t');
                }
            }
            byModPath.insert(e.modPath, { anyMissing, entries });
        }
        return byModPath;
    },
        [](LoadOrderController *self,
           QHash<QString, QPair<bool, QStringList>> byModPath) {
        emit self->missingMastersScanned(byModPath);
        self->m_mastersScanInFlight = false;
        // Drain a buffered retrigger once.
        if (self->m_mastersScanPending) {
            self->m_mastersScanPending = false;
            const auto  in = std::move(self->m_pendingMastersInput);
            const auto  av = std::move(self->m_pendingMastersAvailable);
            self->m_pendingMastersInput.clear();
            self->m_pendingMastersAvailable.clear();
            self->scanMissingMasters(in, av);
        }
    });
}
