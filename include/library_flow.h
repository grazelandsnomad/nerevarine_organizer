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

public:
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
