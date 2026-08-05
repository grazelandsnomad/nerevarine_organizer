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

// LoadOrderController - owns the off-thread file-system scans that feed the
// delegate's conflict / missing-master icon strip.  No Qt Widgets, no ModRole
// references: the UI-side slot reads the emitted maps and writes roles back
// onto the right rows.
//
// Step 3 of the mainwindow.cpp god-object breakup.  Currently covers the
// always-on background conflict scanner.  Follow-up commits add the
// missing-master scan (+ its mtime cache) and may extract autoSortLoadOrder.

class ConflictScanWorker;   // private impl, defined in .cpp
class QMutex;

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

signals:
    void conflictsScanned(
        const QHash<QString, conflict_direction::Directions> &byModPath,
        const QHash<QString, QList<plugin_records::RecordClash>> &recordClashes);
    void missingMastersScanned(
        const QHash<QString, QPair<bool, QStringList>> &byModPath);

private:
    // Conflict-scan QThread lifecycle.
    ConflictScanWorker *m_activeScanner = nullptr;

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
