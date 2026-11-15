#pragma once

// LibraryFlow - the mod library on disk as a whole: Move Mod Library (every
// folder under the mods dir to another root), Consolidate (every folder of
// this profile that lives outside its mods dir, into it), and Clean Up Mod
// Folders (folders nothing points at any more). The moves run on a worker
// behind a cancellable progress dialog, repointing each row as its folder
// lands; the window is reached through WindowShell only.

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
    // Offer the folders the user made in the mods directory themselves,
    // without making them go to the menu for it ("please make it so it
    // automatically rescans mods folder for added mods", MaxYari on Nexus).
    // Runs the scan on a worker and raises the sticky banner when something
    // is worth adding; clicking the banner opens Clean Up Mod Folders, which
    // previews the same folders and holds the Add button. Silent mid-download
    // or mid-install, when half-built folders exist by definition.
    void offerAdoptableFolders();

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
    };
    struct OrphanScan {
        QStringList onDisk;    // folder names directly under modsRoot
        QStringList orphans;   // those `referenced` does not cover
        QStringList adoptable; // orphans holding plugins or resource dirs
        // So many folders came back unused that a modlist probably failed to
        // load. Deleting is withheld, and the load-time offer stays quiet.
        bool suspicious = false;
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
    WindowShell &m_shell;
};
