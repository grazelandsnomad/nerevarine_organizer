#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

#include "conflict_direction.h"
#include "plugin_records.h"
#include "plugin_strings.h"

// LoadOrderController - owns the off-thread file-system scans that feed the
// delegate's conflict / missing-master icon strip.  No Qt Widgets, no ModRole
// references: the UI-side slot reads the emitted maps and writes roles back
// onto the right rows.
//
// Step 3 of the mainwindow.cpp god-object breakup.  Currently covers the
// always-on background conflict scanner.  Follow-up commits add the
// missing-master scan (+ its mtime cache) and may extract autoSortLoadOrder.

class ConflictScanWorker;      // private impl, defined in .cpp
class TranslationScanWorker;   // private impl, defined in .cpp
class QMutex;
class QTimer;

// Whether a mod's player-visible text is covered by a translation somewhere
// else in the list. See scanTranslations() for how a "partner" is decided.
struct TranslationCoverage {
    enum class State {
        Ok,              // nothing to translate, or a partner covers it
        NoTranslation,   // carries text, and nothing supplies an alternative
        Partial,         // a partner exists but a lot of its text is identical
    };
    State       state        = State::Ok;
    int         translatable = 0;   // player-visible strings this mod carries
    int         common       = 0;   // strings the partner also has
    int         identical    = 0;   // ...of which the partner leaves unchanged
    QString     partnerName;        // the mod supplying the alternative text
    QStringList pluginNames;        // this mod's plugins that carry text
    QStringList samples;            // examples of the identical strings
};

// One plugin's extracted text plus the stat it was read at. Both mtime AND
// size, because a same-size same-second rewrite is exactly what re-installing
// a translation over its original looks like.
//
// Out here rather than nested in the controller so the worker in the .cpp can
// name it without being made a friend.
struct CachedPluginStrings {
    qint64 mtimeMs = 0;
    qint64 size    = 0;
    plugin_strings::StringSet strings;
};

class LoadOrderController : public QObject
{
    Q_OBJECT
public:
    // Per-mod snapshot for the missing-master scan.  Caller (MainWindow)
    // builds this from the modlist + collectDataFolders() so the controller
    // never reads widget state.
    struct MastersInput {
        QString modPath;
        // Pairs of (absolute plugin path, plugin filename).  Filenames are
        // used for ModRole::MissingMasters display strings.
        QList<QPair<QString, QString>> plugins;
    };

    explicit LoadOrderController(QObject *parent = nullptr);
    ~LoadOrderController() override;

    // Fire a conflict scan over every enabled + installed mod, IN LOAD ORDER -
    // the order is what decides which mod's copy of a shared file actually
    // loads, so a set would lose the only interesting half of the answer.  If a
    // previous scan is still running, the new call is dropped - the next edit's
    // debounce will retrigger.  So the caller (usually a debounced QTimer)
    // doesn't need to track state.
    //
    // On completion, conflictsScanned emits modPath -> the directed view of
    // that mod's file conflicts (see conflict_direction), plus modPath -> the
    // mods whose PLUGINS rewrite the same records (see plugin_records). The
    // second kind shares no file at all, so it is invisible to the first.
    void scanConflicts(const QList<conflict_direction::Mod> &modsInLoadOrder);

    // Fire a missing-master scan (Morrowind-only; caller guards the profile
    // check).  For every plugin in `enabledMods`, reads the TES3 header and
    // flags masters that aren't in `availableLower` (pre-lowercased set of
    // plugin filenames present across all enabled mods).  Base Morrowind
    // masters (morrowind.esm, tribunal.esm, bloodmoon.esm) are always
    // considered available.
    //
    // Results are cached per absolute plugin path, keyed by file mtime; a
    // later call skips re-reading plugins that haven't changed on disk.
    //
    // Drops overlapping calls: if a scan is in flight, the call is buffered
    // and will fire exactly once more after the in-flight scan lands.
    //
    // On completion emits missingMastersScanned with the map
    //   modPath -> (anyMissing, entries)
    // where each entry is "pluginName\tmissingMaster1\tmissingMaster2\t..."
    // ready to write into ModRole::MissingMasters.
    void scanMissingMasters(const QList<MastersInput> &enabledMods,
                            const QSet<QString> &availableLower);

    // Fire a translation-coverage scan over every enabled + installed mod.
    //
    // Deliberately NOT folded into scanConflicts: that one runs on a 400 ms
    // debounce and only reads record headers, while this reads and decompresses
    // record bodies. So it is on-demand, driven by the toolbar toggle.
    //
    // `targetLanguage` is the lowercase game-language token ("spanish") used to
    // look for Strings/<plugin>_<language>.* alongside a localized plugin. It
    // has no bearing on the string comparison itself, which never decides which
    // side of a pair is the translation.
    //
    // Extracted strings are cached per plugin path, keyed by mtime + size, so a
    // reorder re-decides coverage without re-reading a single plugin.
    //
    // On completion emits translationsScanned with modPath -> coverage. Mods
    // with nothing to translate are omitted rather than sent as Ok.
    void scanTranslations(const QList<conflict_direction::Mod> &modsInLoadOrder,
                          const QString &targetLanguage);

signals:
    void conflictsScanned(
        const QHash<QString, conflict_direction::Directions> &byModPath,
        const QHash<QString, QList<plugin_records::RecordClash>> &recordClashes);
    void missingMastersScanned(
        const QHash<QString, QPair<bool, QStringList>> &byModPath);
    // `modsWithoutPlugins` is how many enabled mods carried no .esp/.esm/.esl
    // at all. The scan reads plugins, so those mods were not examined - text
    // built into an SKSE DLL, an MCM menu or packed in a BSA is invisible to
    // it. Reported so the summary can say what it did NOT check instead of
    // claiming everything is covered.
    void translationsScanned(
        const QHash<QString, TranslationCoverage> &byModPath,
        int modsWithoutPlugins);
    // 0-100 while a translation scan runs, polled off the worker. Emitted at 0
    // the moment a scan starts, so the UI can put its progress panel up before
    // the first plugin is read.
    void translationScanProgress(int percent);

private:
    // Conflict-scan QThread lifecycle.
    ConflictScanWorker *m_activeScanner = nullptr;

    // Translation-scan QThread lifecycle, plus the extracted-string cache it
    // reads and fills, keyed by absolute plugin path. Mutex-guarded for the
    // same reason as m_mastersCache.
    TranslationScanWorker              *m_activeTranslationScanner = nullptr;
    QHash<QString, CachedPluginStrings> m_stringsCache;
    QMutex                             *m_stringsCacheMu = nullptr;
    // Newest request that arrived mid-scan, re-fired exactly once when the
    // in-flight one lands. See scanTranslations for why this buffers where the
    // conflict scan drops.
    QList<conflict_direction::Mod>      m_pendingTranslationMods;
    QString                             m_pendingTranslationLanguage;
    bool                                m_translationScanPending = false;
    // Drives translationScanProgress off the worker's atomic counter.
    QTimer                             *m_translationProgressTimer = nullptr;

    // Missing-master scan state.  mtime-keyed cache so unchanged plugins
    // skip the disk reread.  Mutex guards the cache across the worker
    // thread and the UI thread (a new scan can be scheduled while the
    // previous one still holds a read lock).
    QHash<QString, QPair<qint64, QStringList>>  m_mastersCache;
    QMutex                                     *m_mastersCacheMu = nullptr;
    bool m_mastersScanInFlight = false;
    bool m_mastersScanPending  = false;
    // When a call comes in while a scan is already running, we stash the
    // inputs here and re-fire exactly once when the in-flight scan lands.
    QList<MastersInput>  m_pendingMastersInput;
    QSet<QString>        m_pendingMastersAvailable;
};
