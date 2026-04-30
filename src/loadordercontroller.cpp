#include "loadordercontroller.h"

#include "pluginparser.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QThread>
#include <QtConcurrent>

#include <algorithm>

// Pure filesystem walk + pair-wise bucketing. Declared here (in the .cpp) so
// the controller's public header doesn't pull in a moc dependency.
class ConflictScanWorker : public QThread {
    static constexpr int kMaxShownFiles = 8;
public:
    explicit ConflictScanWorker(const QHash<QString, QString> &modPaths,
                                QObject *parent = nullptr)
        : QThread(parent), m_input(modPaths) {}

    // Safe to read after finished() fires.
    const QHash<QString, QStringList> &results() const { return m_results; }

protected:
    void run() override
    {
        // 1. Build reverse map: relative path (lower-case) -> modPaths that contain it.
        QHash<QString, QStringList> fileOwners;
        fileOwners.reserve(4096);

        for (auto it = m_input.constBegin(); it != m_input.constEnd(); ++it) {
            const QString &modPath = it.key();
            QDir dir(modPath);
            if (!dir.exists()) continue;
            QDirIterator dit(modPath, QDir::Files | QDir::NoDotAndDotDot,
                             QDirIterator::Subdirectories);
            while (dit.hasNext()) {
                dit.next();
                QString rel = dir.relativeFilePath(dit.filePath()).toLower();
                if (rel.endsWith(".txt")) continue;  // .txt never triggers conflicts
                fileOwners[rel].append(modPath);
            }
        }

        // 2. Collect shared files per (unordered) mod pair.
        QHash<QString, QStringList> pairFiles;
        for (auto fit = fileOwners.constBegin(); fit != fileOwners.constEnd(); ++fit) {
            const QStringList &owners = fit.value();
            if (owners.size() < 2) continue;
            for (int i = 0; i < owners.size(); ++i) {
                for (int j = i + 1; j < owners.size(); ++j) {
                    const QString &a = owners[i], &b = owners[j];
                    QString key = (a < b) ? (a + '\t' + b) : (b + '\t' + a);
                    pairFiles[key].append(fit.key());
                }
            }
        }

        // 3. Convert pairs -> per-mod results.
        for (auto pit = pairFiles.constBegin(); pit != pairFiles.constEnd(); ++pit) {
            const QStringList parts = pit.key().split('\t');
            const QString &pathA = parts[0], &pathB = parts[1];
            QStringList files = pit.value();
            files.sort(Qt::CaseInsensitive);

            auto makeEntry = [&](const QString &otherPath) -> QString {
                const QStringList shown = files.mid(0, kMaxShownFiles);
                QString entry = m_input.value(otherPath, otherPath);
                entry += '\t';
                entry += shown.join('\t');
                if (files.size() > kMaxShownFiles)
                    entry += '\t' + QString("…+%1 more").arg(files.size() - kMaxShownFiles);
                return entry;
            };

            m_results[pathA].append(makeEntry(pathB));
            m_results[pathB].append(makeEntry(pathA));
        }

        // 4. Sort each mod's conflict list by the first field (display name).
        for (auto &list : m_results)
            std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) {
                return a.section('\t', 0, 0).compare(b.section('\t', 0, 0),
                                                     Qt::CaseInsensitive) < 0;
            });
    }

private:
    QHash<QString, QString>     m_input;
    QHash<QString, QStringList> m_results;
};

LoadOrderController::LoadOrderController(QObject *parent)
    : QObject(parent), m_mastersCacheMu(new QMutex) {}

LoadOrderController::~LoadOrderController()
{
    // The QThread is parented to this controller, so ~QObject() handles
    // the delete.  Guard against an edge case where the worker is still
    // running at shutdown: block briefly so its run() doesn't dereference
    // a half-destroyed ConflictScanWorker.
    if (m_activeScanner && m_activeScanner->isRunning()) {
        m_activeScanner->requestInterruption();
        m_activeScanner->wait(2000);
    }
    delete m_mastersCacheMu;
}

void LoadOrderController::scanConflicts(const QHash<QString, QString> &modPaths)
{
    // If a previous scan is still running, drop this call.  The caller is
    // typically a debounced QTimer, so the next edit will retrigger.
    if (m_activeScanner && m_activeScanner->isRunning())
        return;

    delete m_activeScanner;
    m_activeScanner = new ConflictScanWorker(modPaths, this);
    connect(m_activeScanner, &QThread::finished, this, [this] {
        // Copy results before deleting the worker, so the signal delivery
        // (even if Direct) still has a valid reference.
        const QHash<QString, QStringList> results = m_activeScanner->results();
        m_activeScanner->deleteLater();
        m_activeScanner = nullptr;
        emit conflictsScanned(results);
    });
    m_activeScanner->start(QThread::LowPriority);
}

void LoadOrderController::scanMissingMasters(
    const QList<MastersInput> &enabledMods,
    const QSet<QString> &availableLower)
{
    if (m_mastersScanInFlight) {
        // Buffer the newest request; an older pending one is superseded.
        m_pendingMastersInput     = enabledMods;
        m_pendingMastersAvailable = availableLower;
        m_mastersScanPending      = true;
        return;
    }
    m_mastersScanInFlight = true;

    QPointer<LoadOrderController> safeSelf(this);
    (void)QtConcurrent::run(
        [safeSelf, enabledMods, availableLower]() {
        // Base Morrowind masters are always considered available -- they
        // don't live in any mod, so the scan would spuriously flag them.
        static const QSet<QString> baseMasters = {
            "morrowind.esm", "tribunal.esm", "bloodmoon.esm"
        };

        QHash<QString, QPair<bool, QStringList>> byModPath;
        if (!safeSelf) return;

        for (const MastersInput &e : enabledMods) {
            QStringList entries;
            bool anyMissing = false;

            for (const auto &plug : e.plugins) {
                const QString &pluginPath = plug.first;
                const QString &pluginName = plug.second;

                // mtime-keyed cache: skip reading plugins that haven't
                // changed on disk since the last scan.
                const qint64 mtime = QFileInfo(pluginPath)
                                        .lastModified().toMSecsSinceEpoch();
                QStringList masters;
                bool hit = false;
                if (safeSelf) {
                    QMutexLocker lk(safeSelf->m_mastersCacheMu);
                    auto it = safeSelf->m_mastersCache.constFind(pluginPath);
                    if (it != safeSelf->m_mastersCache.constEnd()
                     && it.value().first == mtime) {
                        masters = it.value().second;
                        hit = true;
                    }
                }
                if (!hit) {
                    masters = plugins::readTes3Masters(pluginPath);
                    if (safeSelf) {
                        QMutexLocker lk(safeSelf->m_mastersCacheMu);
                        safeSelf->m_mastersCache.insert(pluginPath,
                                                        { mtime, masters });
                    }
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

        if (!safeSelf) return;
        QMetaObject::invokeMethod(safeSelf.data(),
            [safeSelf, byModPath]() {
            if (!safeSelf) return;
            emit safeSelf->missingMastersScanned(byModPath);
            safeSelf->m_mastersScanInFlight = false;
            // Drain a buffered retrigger exactly once.
            if (safeSelf->m_mastersScanPending) {
                safeSelf->m_mastersScanPending = false;
                const auto  in = std::move(safeSelf->m_pendingMastersInput);
                const auto  av = std::move(safeSelf->m_pendingMastersAvailable);
                safeSelf->m_pendingMastersInput.clear();
                safeSelf->m_pendingMastersAvailable.clear();
                safeSelf->scanMissingMasters(in, av);
            }
        }, Qt::QueuedConnection);
    });
}
