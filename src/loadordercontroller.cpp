#include "loadordercontroller.h"

#include "async_guarded.h"
#include "pluginparser.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>

#include <atomic>
#include <utility>

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

// -- Translation coverage ------------------------------------------------
//
// Reads the player-visible text out of every enabled mod's plugins and decides,
// per mod, whether anything else in the list supplies alternative text for it.
//
// The pairing needs no separate record-overlap pass: plugin_strings keys are
// already "TYPE:formid:SUB:index", normalised the same way for both plugins, so
// two plugins editing the same records simply share keys. Sharing most of the
// smaller plugin's keys IS the "these two are about the same content" test, and
// it comes free from the comparison we have to run anyway.
class TranslationScanWorker : public QThread {
    // Two plugins are treated as a translation pair when the smaller one's keys
    // are mostly present in the larger. Half is deliberately generous: a
    // translation that skips a chunk of a mod is precisely what we want to
    // catch, so the bar for "these are the same content" has to sit below the
    // bar for "this translation is complete".
    static constexpr double kPairRatio = 0.5;

public:
    using Cache = QHash<QString, CachedPluginStrings>;

    TranslationScanWorker(const QList<conflict_direction::Mod> &mods,
                          QString targetLanguage,
                          Cache *cache, QMutex *cacheMu,
                          QObject *parent = nullptr)
        : QThread(parent), m_mods(mods),
          m_language(std::move(targetLanguage)),
          m_cache(cache), m_cacheMu(cacheMu) {}

    // Read only after finished() fires.
    const QHash<QString, TranslationCoverage> &results() const { return m_results; }

    // 0-100, safe to read from the UI thread at any time. Polled by the
    // controller rather than signalled per plugin: the bar only needs a few
    // updates a second, and a signal per plugin on a 300-plugin list is a lot
    // of queued events to say the same thing.
    int percent() const { return m_percent.load(std::memory_order_relaxed); }

protected:
    void run() override
    {
        // One entry per plugin across the whole list, so the pairing pass is a
        // plain O(n^2) over plugins rather than a nested walk of mods.
        struct Entry {
            int         modIdx = 0;
            QString     pluginName;   // file name, lower case
            plugin_strings::StringSet strings;
        };
        QList<Entry> entries;
        // Lower-cased "<pluginbase>_<language>" tokens found under any enabled
        // mod's Strings/ dir - how a localized plugin gets translated.
        QSet<QString> stringFiles;

        for (int i = 0; i < m_mods.size(); ++i) {
            if (isInterruptionRequested()) return;
            if (!m_mods.isEmpty()) setPercent(90 * i / m_mods.size());
            QDir dir(m_mods[i].path);
            if (!dir.exists()) continue;

            QDirIterator dit(m_mods[i].path, QDir::Files | QDir::NoDotAndDotDot,
                             QDirIterator::Subdirectories);
            while (dit.hasNext()) {
                if (isInterruptionRequested()) return;
                dit.next();
                const QString lower = dit.fileName().toLower();

                if (lower.endsWith(QLatin1String(".strings"))
                 || lower.endsWith(QLatin1String(".dlstrings"))
                 || lower.endsWith(QLatin1String(".ilstrings"))) {
                    stringFiles.insert(lower.section(QLatin1Char('.'), 0, 0));
                    continue;
                }
                if (!lower.endsWith(QLatin1String(".esp"))
                 && !lower.endsWith(QLatin1String(".esm"))
                 && !lower.endsWith(QLatin1String(".esl"))) continue;

                const auto st = cachedExtract(dit.filePath());
                if (!st.valid) continue;
                if (st.byKey.isEmpty() && !st.localized) continue;  // nothing to say
                entries.append({i, lower, st});
            }
        }
        if (isInterruptionRequested()) return;

        // Pair every plugin with the best candidate from a DIFFERENT mod: the
        // one sharing the most keys, provided it shares most of the smaller
        // set. Same-mod plugins are skipped - a mod does not translate itself.
        for (int a = 0; a < entries.size(); ++a) {
            if (isInterruptionRequested()) return;
            if (!entries.isEmpty()) setPercent(90 + 10 * a / entries.size());
            const Entry &ea = entries[a];

            // A localized plugin keeps its text in Strings/, so coverage is a
            // file-presence question and no comparison is possible.
            if (ea.strings.localized) {
                const QString base = ea.pluginName.section(QLatin1Char('.'), 0, 0);
                const bool covered = m_language.isEmpty()
                    || stringFiles.contains(base + QLatin1Char('_') + m_language);
                noteCoverage(ea.modIdx, ea.pluginName, /*translatable=*/0,
                             covered ? TranslationCoverage::State::Ok
                                     : TranslationCoverage::State::NoTranslation,
                             {}, {});
                continue;
            }

            int bestShared = 0, bestIdx = -1;
            plugin_strings::Comparison best;
            for (int b = 0; b < entries.size(); ++b) {
                if (b == a || entries[b].modIdx == ea.modIdx) continue;
                if (entries[b].strings.localized) continue;
                const auto cmp = plugin_strings::compare(ea.strings, entries[b].strings);
                const int smaller = qMin(ea.strings.byKey.size(),
                                         entries[b].strings.byKey.size());
                if (smaller <= 0) continue;
                if (double(cmp.common) < kPairRatio * double(smaller)) continue;
                if (cmp.common > bestShared) {
                    bestShared = cmp.common;
                    bestIdx    = b;
                    best       = cmp;
                }
            }

            const int translatable = ea.strings.byKey.size();
            if (bestIdx < 0) {
                noteCoverage(ea.modIdx, ea.pluginName, translatable,
                             TranslationCoverage::State::NoTranslation, {}, {});
                continue;
            }
            const bool partial = best.ratio() >= plugin_strings::kPartialRatio
                              && best.identical >= plugin_strings::kPartialCount;
            noteCoverage(ea.modIdx, ea.pluginName, translatable,
                         partial ? TranslationCoverage::State::Partial
                                 : TranslationCoverage::State::Ok,
                         m_mods[entries[bestIdx].modIdx].name, best.samples,
                         best.common, best.identical);
        }

        // Mods that turned out to have nothing to say are dropped here rather
        // than at every call site that reads the map.
        for (auto it = m_results.begin(); it != m_results.end(); ) {
            if (it->state == TranslationCoverage::State::Ok) it = m_results.erase(it);
            else                                             ++it;
        }
    }

private:
    // Merge one plugin's verdict into its mod's. A mod with several plugins
    // takes the worst of them: one untranslated plugin is one untranslated
    // plugin, however many of its siblings are fine.
    void noteCoverage(int modIdx, const QString &pluginName, int translatable,
                      TranslationCoverage::State state, const QString &partner,
                      const QStringList &samples, int common = 0, int identical = 0)
    {
        // Single lookup, and the reference never outlives a call that could
        // rehash the container.
        auto &cov = m_results[m_mods[modIdx].path];
        cov.translatable += translatable;
        cov.common       += common;
        cov.identical    += identical;
        if (state != TranslationCoverage::State::Ok) {
            cov.pluginNames << pluginName;
            for (const QString &s : samples)
                if (cov.samples.size() < 8 && !cov.samples.contains(s))
                    cov.samples << s;
        }
        if (!partner.isEmpty() && cov.partnerName.isEmpty()) cov.partnerName = partner;
        // NoTranslation outranks Partial outranks Ok.
        if (state == TranslationCoverage::State::NoTranslation
            || cov.state == TranslationCoverage::State::NoTranslation)
            cov.state = TranslationCoverage::State::NoTranslation;
        else if (state == TranslationCoverage::State::Partial)
            cov.state = TranslationCoverage::State::Partial;
    }

    // Extraction is the expensive half, and a reorder changes who covers whom
    // without changing a byte on disk - so cache by mtime + size.
    plugin_strings::StringSet cachedExtract(const QString &path)
    {
        const QFileInfo fi(path);
        const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
        const qint64 size  = fi.size();
        {
            QMutexLocker lock(m_cacheMu);
            const auto it = m_cache->constFind(path);
            if (it != m_cache->constEnd()
                && it->mtimeMs == mtime && it->size == size)
                return it->strings;
        }
        const auto st = plugin_strings::extract(path);
        {
            QMutexLocker lock(m_cacheMu);
            (*m_cache)[path] = {mtime, size, st};
        }
        return st;
    }

    // Reading the plugins dominates, so it owns the first 90% and the pairing
    // pass the last 10%. Split this way the bar only ever moves forward - a
    // single done/total pair would have to revise `total` upward once the
    // plugin count is known, and jump backwards on screen.
    void setPercent(int p) { m_percent.store(p, std::memory_order_relaxed); }

    QList<conflict_direction::Mod>      m_mods;
    QString                             m_language;
    Cache                              *m_cache   = nullptr;
    QMutex                             *m_cacheMu = nullptr;
    QHash<QString, TranslationCoverage> m_results;
    std::atomic<int>                    m_percent{0};
};

LoadOrderController::LoadOrderController(QObject *parent)
    : QObject(parent), m_mastersCacheMu(new QMutex), m_stringsCacheMu(new QMutex) {}

LoadOrderController::~LoadOrderController()
{
    // Workers are parented to us, so ~QObject() deletes them. But if one is
    // still running at shutdown, block briefly first or run() dereferences a
    // half-destroyed worker - and, for the translation scan, a freed cache.
    for (QThread *w : {static_cast<QThread *>(m_activeScanner),
                       static_cast<QThread *>(m_activeTranslationScanner)}) {
        if (w && w->isRunning()) {
            w->requestInterruption();
            w->wait(2000);
        }
    }
    delete m_mastersCacheMu;
    delete m_stringsCacheMu;
}

void LoadOrderController::scanTranslations(
    const QList<conflict_direction::Mod> &modsInLoadOrder,
    const QString &targetLanguage)
{
    // Buffer rather than drop when a scan is in flight.
    //
    // The conflict scan can afford to drop - something edits the list again
    // within seconds and retriggers it. This one is answering a question the
    // user asked once, and the edit that matters most (unticking the mod whose
    // translation you are testing) is often the LAST thing they do. Dropping it
    // there leaves the previous scan's verdict painted, which reads as the
    // feature being broken. So keep the newest request and re-fire once.
    if (m_activeTranslationScanner && m_activeTranslationScanner->isRunning()) {
        m_pendingTranslationMods     = modsInLoadOrder;
        m_pendingTranslationLanguage = targetLanguage;
        m_translationScanPending      = true;
        return;
    }

    delete m_activeTranslationScanner;
    m_activeTranslationScanner = new TranslationScanWorker(
        modsInLoadOrder, targetLanguage, &m_stringsCache, m_stringsCacheMu, this);

    // Poll the worker's counter onto the UI rather than have it signal per
    // plugin. 100ms is well under the eye's "is this thing alive" threshold and
    // costs one atomic read.
    if (!m_translationProgressTimer) {
        m_translationProgressTimer = new QTimer(this);
        m_translationProgressTimer->setInterval(100);
        connect(m_translationProgressTimer, &QTimer::timeout, this, [this] {
            if (m_activeTranslationScanner)
                emit translationScanProgress(m_activeTranslationScanner->percent());
        });
    }
    emit translationScanProgress(0);
    m_translationProgressTimer->start();

    connect(m_activeTranslationScanner, &QThread::finished, this, [this] {
        m_translationProgressTimer->stop();
        const QHash<QString, TranslationCoverage> results =
            m_activeTranslationScanner->results();
        m_activeTranslationScanner->deleteLater();
        m_activeTranslationScanner = nullptr;
        emit translationsScanned(results);
        // Serve whatever came in while this one was running, so the last edit
        // the user made is always the one reflected on screen.
        if (m_translationScanPending) {
            m_translationScanPending = false;
            scanTranslations(m_pendingTranslationMods, m_pendingTranslationLanguage);
        }
    });
    m_activeTranslationScanner->start(QThread::LowPriority);
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
