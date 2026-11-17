#pragma once

// LibraryFlow - the mod library on disk as a whole: Move Mod Library (every
// folder under the mods dir to another root), Consolidate (every folder of
// this profile that lives outside its mods dir, into it), and Clean Up Mod
// Folders (folders nothing points at any more). The moves run on a worker
// behind a cancellable progress dialog, repointing each row as its folder
// lands; the window is reached through WindowShell only.

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>

#include <functional>

class WindowShell;

class LibraryFlow : public QObject {
    Q_OBJECT
public:
    explicit LibraryFlow(WindowShell &shell, QObject *parent = nullptr);

    // One folder to move: the row it belongs to, where it is now, and what
    // it will be called under the destination root.
    struct FolderMove {
        QUuid   row;              // the row's id; resolved when the move lands
        QString oldPath;
        QString folderName;
        qint64  sizeBytes = 0;
    };

public slots:
    void onMoveModsDir();
    // Pull any of this profile's mods living outside its modsDir into it. Main
    // case: a cloned profile still points at the source's mods on disk; move
    // them in to decouple the two profiles. No-op when all mods are already
    // inside. Refuses mid-download or with the mods dir unset.
    void onConsolidateModsIntoActiveProfile();
    // Reclaim mod folders nothing points at any more: older builds left behind
    // by upgrades, extract dirs from installs that never finished, leftovers
    // from "remove but keep files". Previews everything with sizes and deletes
    // only on explicit confirm. Refuses mid-download or mid-install.
    void onCleanUpModFolders();
    // Take in the folders the user made in the mods directory themselves -
    // the MO2 habit of building or copying a mod folder by hand and having it
    // picked up on the next refresh (GitHub issue #2, MaxYari).
    //
    // No dialog and no button: rows appear by themselves, UNTICKED, so a
    // folder that turns up can never change what the game loads. Run after a
    // profile load and whenever the window comes back to the front, which is
    // the moment right after the user made the folder in their file manager.
    //
    // Deliberately far away from anything that deletes: the first answer to
    // this request put the Add button in the Clean Up dialog, next to Delete,
    // and the reporter said - fairly - that he was afraid to go near it.
    // Adoption also protects those folders from the sweep, because a folder
    // that has become a row is in use and is no longer an orphan.
    //
    // Silent while a download or an install runs (half-built folders exist by
    // definition then), when the references are incomplete, and when so many
    // folders read as unused that a modlist has probably failed to load.
    void adoptNewFolders();

public:
    // Which folders under the mods dir nothing points at any more, and which
    // of those hold something a game would load.
    //
    // Split in two so the filesystem half can run on a worker: `orphanInputs`
    // reads the model and the game config and must run on the GUI thread,
    // `scanOrphans` touches only its argument and the disk.
    struct OrphanInputs {
        QString     modsRoot;   // absolute; empty = nothing to scan
        QStringList referenced; // every path any profile or the game names
        // Every reference source was read. False when a sibling profile's
        // modlist or the game config exists and would not open - then the
        // folders they name look unused, and nothing may act on that.
        bool referencesComplete = true;
        // Measuring every orphan folder is a recursive stat of possibly tens
        // of thousands of files; only the delete preview needs it.
        bool wantSizes = false;
    };
    struct OrphanScan {
        QStringList onDisk;    // folder names directly under modsRoot
        QStringList orphans;   // those `referenced` does not cover
        QStringList adoptable; // orphans holding plugins or resource dirs
        // So many folders came back unused that a modlist probably failed to
        // load. Deleting is withheld, and adoption stays quiet.
        bool suspicious = false;
        // With wantSizes: every orphan as (bytes, name), largest first.
        QList<QPair<qint64, QString>> sized;
        qint64                        totalBytes = 0;
    };
    OrphanInputs      orphanInputs() const;
    static OrphanScan scanOrphans(const OrphanInputs &in);

    // The checks both moves share before asking: enough free space under
    // destRoot (10% headroom, since a cross-filesystem move copies first) and
    // no folder of the same name already there. Tells the user and returns
    // false when either fails.
    bool preflightFolderMoves(const QList<FolderMove> &moves, const QString &destRoot,
                              qint64 totalBytes, const QString &title);
    // Move each folder under destRoot on a worker (safefs::moveTreeVerified)
    // behind a cancellable progress dialog. Every folder that lands has its
    // row repointed and the list saved at once, so a crash midway loses
    // nothing. `done(movedOk)` runs on the UI thread after the last one, before
    // the tail both moves share (config sync, rescans, undo cleared) and the
    // summary.
    void moveModFolders(const QList<FolderMove> &moves, const QString &destRoot,
                        const QString &title, std::function<void(int movedOk)> done);

signals:
    // After the summary of a move: how many folders landed and how many did not.
    void movesFinished(int movedOk, int failed);

private:
    // The second half of adoptNewFolders, once the scan is back on the GUI
    // thread: prune the ignore list, drop what is ignored or has become a row
    // meanwhile, and append what is left as unticked rows.
    void applyAdoption(const OrphanScan &scan, const QString &modsRoot,
                       const QDateTime &stamp, const QString &gameId);
    // The report half of onCleanUpModFolders, once its scan is back.
    void showCleanUpReport(const OrphanScan &scan, const QString &modsRoot);

    WindowShell &m_shell;
    // A scan runs on a worker while the user can still switch profiles, and
    // this object outlives every switch (it is made once and never destroyed,
    // so the QPointer in async::guarded never fires). Each pass carries the
    // counter's value and drops its result if the counter moved on - without
    // it, profile A's folders land in profile B's list.
    quint64 m_scanEpoch       = 0;
    bool    m_adoptScanning   = false;
    bool    m_cleanupScanning = false;
    // The mods directory's own mtime at the last adoption pass. A folder
    // added or removed changes it, so an unchanged stamp means the whole
    // scan can be skipped - which is what makes running on every window
    // activation cost one stat instead of a directory walk.
    QString   m_lastAdoptRoot;
    QDateTime m_lastAdoptStamp;
};
