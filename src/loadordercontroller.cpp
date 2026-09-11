#include "loadordercontroller.h"

#include "translation_coverage.h"
#include "translation_rules.h"
#include "vanilla_text.h"
#include "language_guess.h"

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
                          QString targetLanguage, QString vanillaDataFolder,
                          QString rulesPath,
                          Cache *cache, QMutex *cacheMu,
                          QObject *parent = nullptr)
        : QThread(parent), m_mods(mods),
          m_language(std::move(targetLanguage)),
          m_vanillaFolder(std::move(vanillaDataFolder)),
          m_rulesPath(std::move(rulesPath)),
          m_cache(cache), m_cacheMu(cacheMu) {}

    // Read only after finished() fires.
    const QHash<QString, TranslationCoverage> &results() const { return m_results; }
    const QHash<QString, QString> &pairs() const { return m_pairs; }
    // Enabled mods that carried no plugin at all. Not a failure - most are
    // meshes or SKSE libraries with genuinely nothing to translate - but the
    // scan did not look INSIDE them, so the summary must not claim it did.
    int modsWithoutPlugins() const { return m_noPluginMods; }

    // 0-100, safe to read from the UI thread at any time. Polled by the
    // controller rather than signalled per plugin: the bar only needs a few
    // updates a second, and a signal per plugin on a 300-plugin list is a lot
    // of queued events to say the same thing.
    int percent() const { return m_percent.load(std::memory_order_relaxed); }

protected:
    void run() override
    {
        // On this thread, deliberately: see m_vanillaFolder. Shared and built
        // at most once per process, so a warm cache is a hash lookup.
        const vanilla_text::Table &vanilla = vanilla_text::cached(m_vanillaFolder);
        // Only [ordinary] is wanted: the rest of the file says how to TRANSLATE
        // things, and nothing here translates anything.
        const QSet<QString> ordinary =
            m_rulesPath.isEmpty() ? QSet<QString>()
                                  : translation_rules::load(m_rulesPath).ordinary;

        // One entry per plugin across the whole list, so the pairing pass is a
        // plain O(n^2) over plugins rather than a nested walk of mods.
        QList<translation_coverage::Entry> entries;
        // Lower-cased "<pluginbase>_<language>" tokens found under any enabled
        // mod's Strings/ dir - how a localized plugin gets translated.
        QSet<QString> stringFiles;

        for (int i = 0; i < m_mods.size(); ++i) {
            if (isInterruptionRequested()) return;
            if (!m_mods.isEmpty()) setPercent(90 * i / m_mods.size());
            QDir dir(m_mods[i].path);
            if (!dir.exists()) continue;

            bool sawPlugin = false;
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
                sawPlugin = true;

                auto st = cachedExtract(dit.filePath());
                if (!st.valid) continue;
                // AFTER the cache read, never inside it: cachedExtract
                // memoises the raw StringSet by mtime+size, and filtering in
                // there would hand a pruned set to every later reader.
                //
                // What the mod itself says, which is the only thing a verdict
                // may be computed from. Without this a mod that re-saved the
                // base game's own settings looked ~93% identical to its own
                // finished translation, and the near-verbatim rejector threw
                // the translation out. See vanilla_text::dropBaseGameText.
                vanilla_text::dropBaseGameText(st, vanilla);
                // And the mod's own invented names, which read the same in
                // every language: True Vvardenfell - Dagoths Domain says
                // nothing but "Veythrazel" and was reported untranslated for
                // work that does not exist. See dropBareNames.
                translation_coverage::dropBareNames(st, ordinary);
                if (!st.valid) continue;
                // Nothing to say only when BOTH tiers are empty - the real
                // mesh/texture case. A plugin whose only text is secondary
                // (an NPC_ name, a location) used to be dropped here, and the
                // silence read as "nothing to translate"; see plugin_strings.h.
                if (st.empty() && !st.localized) continue;
                entries.append({i, lower, st});
            }
            if (!sawPlugin) ++m_noPluginMods;
        }
        if (isInterruptionRequested()) return;

        // The judgement itself lives in translation_coverage, where a test can
        // reach it: this is a QThread, and five calibrated thresholds inside
        // run() had nothing pinning them.
        QStringList modNames;
        modNames.reserve(m_mods.size());
        for (const auto &m : m_mods) modNames << m.name;

        setPercent(95);
        const auto verdicts = translation_coverage::judge(
            entries, stringFiles, modNames, m_language);
        if (isInterruptionRequested()) return;

        for (const auto &v : verdicts)
            noteCoverage(v.modIdx, v.pluginName, v.translatable, v.state,
                         v.partnerMod, v.samples, v.common, v.identical);

        // The pairing, DIRECTED: translation -> source. Recorded separately
        // from the coverage map, which throws away everything it has nothing
        // to complain about - and a successful pairing is precisely that.
        //
        // A mod with several plugins votes once per plugin, so collect the
        // language verdict first and let any plugin reading as the target
        // language speak for the mod.
        QHash<int, bool> inTarget;
        for (const auto &v : verdicts)
            if (v.readsAsTarget) inTarget.insert(v.modIdx, true);
        for (const auto &v : verdicts) {
            if (v.partnerModIdx < 0 || v.partnerModIdx >= m_mods.size()) continue;
            if (v.modIdx < 0 || v.modIdx >= m_mods.size())                continue;
            // One side in the target language and the other not. Two English
            // mods that merely share keys - a compatibility patch and the mod
            // it patches - name no direction, and get no claim made about
            // them.
            if (!inTarget.value(v.modIdx) || inTarget.value(v.partnerModIdx))
                continue;
            m_pairs.insert(m_mods[v.modIdx].path, m_mods[v.partnerModIdx].path);
        }
        setPercent(100);

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
    // Strings to judge the language on: both tiers, since an NPC-name-only
    // mod is exactly the case this has to answer. Capped because the test is
    // a proportion, not a census, and a 5000-string plugin decides itself long
    // before the end.
    static QStringList sampleText(const plugin_strings::StringSet &s)
    {
        constexpr int kMax = 300;
        QStringList out;
        out.reserve(qMin(kMax, int(s.byKey.size() + s.auxByKey.size())));
        for (auto it = s.byKey.cbegin(); it != s.byKey.cend(); ++it) {
            if (out.size() >= kMax) return out;
            out << it.value();
        }
        for (auto it = s.auxByKey.cbegin(); it != s.auxByKey.cend(); ++it) {
            if (out.size() >= kMax) return out;
            out << it.value();
        }
        return out;
    }

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
    // The FOLDER, resolved on the UI thread; the table is fetched here so a
    // cold cache costs the worker 94 MB of walking rather than the window.
    QString                             m_vanillaFolder;
    // Read here too, for the same reason: [ordinary] is the user's override on
    // dropBareNames, and a file read is not the window's business.
    QString                             m_rulesPath;
    int                                 m_noPluginMods = 0;
    Cache                              *m_cache   = nullptr;
    QMutex                             *m_cacheMu = nullptr;
    QHash<QString, TranslationCoverage> m_results;
    QHash<QString, QString>             m_pairs;   // mod path -> partner path
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
    const QString &targetLanguage,
    const QString &vanillaDataFolder,
    const QString &rulesPath)
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
        m_pendingTranslationMods          = modsInLoadOrder;
        m_pendingTranslationLanguage      = targetLanguage;
        m_pendingTranslationVanillaFolder = vanillaDataFolder;
        m_pendingTranslationRulesPath     = rulesPath;
        m_translationScanPending      = true;
        return;
    }

    delete m_activeTranslationScanner;
    m_activeTranslationScanner = new TranslationScanWorker(
        modsInLoadOrder, targetLanguage, vanillaDataFolder, rulesPath,
        &m_stringsCache, m_stringsCacheMu, this);

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
        // Read before the worker is destroyed - it is what lets the summary
        // say what it did not examine.
        const int noPlugin = m_activeTranslationScanner->modsWithoutPlugins();
        const auto pairs   = m_activeTranslationScanner->pairs();
        m_activeTranslationScanner->deleteLater();
        m_activeTranslationScanner = nullptr;
        emit translationsScanned(results, noPlugin, pairs);
        // Serve whatever came in while this one was running, so the last edit
        // the user made is always the one reflected on screen.
        if (m_translationScanPending) {
            m_translationScanPending = false;
            scanTranslations(m_pendingTranslationMods,
                             m_pendingTranslationLanguage,
                             m_pendingTranslationVanillaFolder,
                             m_pendingTranslationRulesPath);
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
