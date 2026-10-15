#include "library_flow.h"

#include "async_guarded.h"
#include "mainwindow_internal.h"
#include "mod_cleanup.h"
#include "modlist_model.h"
#include "modrow.h"
#include "prompts.h"
#include "report_dialog.h"
#include "safe_fs.h"
#include "translator.h"
#include "undo_stack.h"
#include "window_shell.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSet>
#include <QStorageInfo>
#include <QTextStream>

#include <algorithm>
#include <atomic>
#include <memory>

LibraryFlow::LibraryFlow(WindowShell &shell, QObject *parent)
    : QObject(parent), m_shell(shell)
{
}

namespace {

QString fmtGb(qint64 b)
{
    const double GB = 1024.0 * 1024.0 * 1024.0;
    return QString::number(b / GB, 'f', 2) + " GB";
}

QString fmtMbGb(qint64 b)
{
    const double MB = 1024.0 * 1024.0;
    const double GB = MB * 1024.0;
    if (b >= GB) return QString::number(b / GB, 'f', 2) + " GB";
    return QString::number(b / MB, 'f', 1) + " MB";
}

// What became of one folder, as the worker reports it. Paths only: the
// worker never touches a row.
struct FolderMoveOutcome {
    int     index = -1;
    bool    ok    = false;
    QString newPath;
    QString error;        // empty when ok
};

// One run, shared between the worker and the UI thread. The worker appends
// each outcome under the mutex as the folder lands and the UI thread drains
// the list, so a folder that moved always reaches its row: cancelled or not,
// an outcome the worker recorded is never dropped.
struct FolderMoveRun {
    QList<QPair<QString, QString>> jobs;      // oldPath -> newPath, in order
    std::atomic<bool>              cancelled{false};
    QMutex                         mu;
    QList<FolderMoveOutcome>       outcomes;
};

} // namespace
namespace {

// Recursive byte size of a folder. Used only to show the user what they are
// about to reclaim, so a stat failure just contributes 0 rather than aborting.
qint64 folderSizeBytes(const QString &path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

QString cleanupFmtBytes(qint64 b)
{
    const double MB = 1024.0 * 1024.0;
    const double GB = MB * 1024.0;
    if (b >= GB) return QString::number(b / GB, 'f', 2) + QStringLiteral(" GB");
    if (b >= MB) return QString::number(b / MB, 'f', 1) + QStringLiteral(" MB");
    return QString::number(b / 1024.0, 'f', 0) + QStringLiteral(" KB");
}

} // namespace
bool LibraryFlow::preflightFolderMoves(const QList<FolderMove> &moves,
                                      const QString &destRoot, qint64 totalBytes,
                                      const QString &title)
{
    // Free space with 10% headroom: a cross-filesystem move copies first.
    const QStorageInfo dstStorage(destRoot);
    const qint64 required = qint64(double(totalBytes) * 1.10);
    if (dstStorage.bytesAvailable() < required) {
        ui::warn(m_shell.dialogParent(), title, T("move_mods_err_no_space")
                                  .arg(fmtGb(dstStorage.bytesAvailable()))
                                  .arg(fmtGb(required)));
        return false;
    }

    // A folder of the same name already under the destination: bail rather
    // than pick a winner.
    QStringList collisions;
    for (const FolderMove &m : moves)
        if (QFileInfo::exists(QDir(destRoot).filePath(m.folderName)))
            collisions << m.folderName;
    if (!collisions.isEmpty()) {
        QString list = collisions.mid(0, 10).join("\n  • ");
        if (collisions.size() > 10)
            list += "\n  … (+" + QString::number(collisions.size() - 10) + ")";
        ui::warn(m_shell.dialogParent(), title, T("move_mods_err_collision").arg(list));
        return false;
    }
    return true;
}

void LibraryFlow::moveModFolders(const QList<FolderMove> &moves, const QString &destRoot,
                                const QString &title,
                                std::function<void(int movedOk)> done)
{
    if (moves.isEmpty()) return;

    auto run = std::make_shared<FolderMoveRun>();
    for (const FolderMove &m : moves)
        run->jobs.append({m.oldPath, QDir(destRoot).filePath(m.folderName)});

    // Modal to the window, so the list cannot change under the move. Cancel
    // stops the worker after the folder it is on; the dialog hides itself and
    // the summary names what moved.
    //
    // A plain dialog, not a QProgressDialog: that one processes events from
    // inside setValue() while it is modal, and the queued hop that delivers
    // each landed folder re-entered it from there and crashed in it.
    const auto labelFor = [moves](int i) {
        return T("move_mods_progress_detail")
            .arg(i + 1).arg(moves.size()).arg(moves.at(i).folderName);
    };
    auto *progress = new QDialog(m_shell.dialogParent());
    progress->setWindowTitle(title);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumWidth(420);
    auto *layout = new QVBoxLayout(progress);
    layout->addWidget(new QLabel(T("move_mods_progress").arg(moves.size()), progress));
    auto *detail = new QLabel(labelFor(0), progress);
    layout->addWidget(detail);
    auto *bar = new QProgressBar(progress);
    bar->setRange(0, int(moves.size()));
    bar->setValue(0);
    layout->addWidget(bar);
    auto *cancel = new QPushButton(T("move_mods_cancel"), progress);
    layout->addWidget(cancel, 0, Qt::AlignRight);
    connect(cancel, &QPushButton::clicked, this, [run, cancel] {
        run->cancelled = true;
        cancel->setEnabled(false);
    });
    progress->show();
    const QPointer<QDialog> dlg(progress);
    const QPointer<QProgressBar> barPtr(bar);
    const QPointer<QLabel> detailPtr(detail);

    // UI-thread bookkeeping: rows repointed and the list saved as each folder
    // lands, so a crash midway loses nothing; the tally feeds the summary.
    struct Tally { int applied = 0; int movedOk = 0; QStringList failures; };
    auto tally = std::make_shared<Tally>();
    const auto drain = [this, run, moves, tally, barPtr, detailPtr, labelFor] {
        QList<FolderMoveOutcome> landed;
        {
            QMutexLocker lock(&run->mu);
            landed.swap(run->outcomes);
        }
        for (const FolderMoveOutcome &out : landed) {
            const FolderMove &m = moves.at(out.index);
            if (out.ok) {
                ++tally->movedOk;
                if (ModRow item = m_shell.rowById(m.row)) {
                    item->setData(ModRole::ModPath, out.newPath);
                    item->setToolTip(out.newPath);
                }
                m_shell.saveModList();
            } else {
                tally->failures.append(
                    m.folderName
                    + (out.error.isEmpty() ? QString()
                                           : QStringLiteral(" - ") + out.error));
            }
            ++tally->applied;
            if (barPtr) barPtr->setValue(tally->applied);
            if (detailPtr && tally->applied < moves.size())
                detailPtr->setText(labelFor(tally->applied));
        }
    };

    const QPointer<LibraryFlow> guard(this);
    async::guarded(this,
        [run, guard, drain](LibraryFlow *) {
            for (int i = 0; i < run->jobs.size(); ++i) {
                if (run->cancelled) return;
                FolderMoveOutcome out;
                out.index   = i;
                out.newPath = run->jobs.at(i).second;
                const auto res = safefs::moveTreeVerified(
                    run->jobs.at(i).first, out.newPath,
                    [run] { return run->cancelled.load(); });
                out.ok = res.has_value();
                if (!res) out.error = res.error();
                {
                    QMutexLocker lock(&run->mu);
                    run->outcomes.append(std::move(out));
                }
                // Let the row land while the next folder moves. The window
                // may be gone by then; the queued call checks first.
                if (guard)
                    QMetaObject::invokeMethod(guard.data(), [guard, drain] {
                        if (guard) drain();
                    }, Qt::QueuedConnection);
            }
        },
        [this, drain, dlg, tally, title, done](LibraryFlow *) {
            drain();   // whatever the last hop had not delivered yet
            if (dlg) {
                dlg->hide();
                dlg->deleteLater();
            }

            if (tally->movedOk > 0) {
                if (done) done(tally->movedOk);
                m_shell.saveModList();
                m_shell.syncGameConfig();
                m_shell.scanMissingMasters();
                m_shell.scheduleSizeScan();
                // File moves can't be undone via the snapshot stack; clear it
                // so the user isn't tempted into a half-reverted state.
                if (UndoStack *undo = m_shell.undo()) undo->clear();
            }

            // -- Report ---
            QString body = T("move_mods_done_ok").arg(tally->movedOk);
            if (!tally->failures.isEmpty()) {
                QString flist = tally->failures.mid(0, 15).join("\n  • ");
                if (tally->failures.size() > 15)
                    flist += "\n  … (+" + QString::number(tally->failures.size() - 15) + ")";
                body += "\n\n" + T("move_mods_done_failures")
                                    .arg(tally->failures.size()) + "\n  • " + flist;
            }
            m_shell.reportResult(title, body, !tally->failures.isEmpty());
            emit movesFinished(tally->movedOk, int(tally->failures.size()));
        });
}

void LibraryFlow::onMoveModsDir()
{
    // -- Pre-flight: downloads must be idle ---
    if (!m_shell.downloadsIdle()) {
        ui::warn(m_shell.dialogParent(), T("move_mods_title"), T("move_mods_err_downloads"));
        return;
    }

    // Refresh sizes so the "X GB will be moved" figure is honest.
    m_shell.scheduleSizeScan();

    // -- Gather the set of mod folders to move ---
    QList<FolderMove> moves;
    qint64 totalBytes = 0;

    const QString modsRoot = QFileInfo(m_shell.modsDir()).absoluteFilePath();

    const QList<ModEntry> rows = m_shell.model().all();
    for (const ModEntry &it : rows) {
        if (!it.isMod() || !it.isInstalled()) continue;
        const QString path = it.modPath;
        if (path.isEmpty() || !QFileInfo(path).isDir())              continue;

        const QString abs = QFileInfo(path).absoluteFilePath();
        // Only move entries that actually live under the active mods dir.
        // Mods pointing somewhere else (user symlinked, manually added from
        // /opt/…, etc.) are left alone to avoid surprises.
        if (!abs.startsWith(modsRoot + "/") && abs != modsRoot)      continue;

        const QString folderName = QDir(modsRoot).relativeFilePath(abs);
        // We only know how to move direct children of modsRoot; anything
        // nested (moved via repair, for instance) is left where it is.
        if (folderName.contains('/')) continue;

        FolderMove m;
        m.row        = it.rowId;
        m.oldPath    = abs;
        m.folderName = folderName;
        m.sizeBytes  = it.modSize;
        if (m.sizeBytes < 0) m.sizeBytes = 0;
        moves.append(m);
        totalBytes += m.sizeBytes;
    }

    if (moves.isEmpty()) {
        ui::info(m_shell.dialogParent(), T("move_mods_title"), T("move_mods_err_nothing"));
        return;
    }

    // -- Ask for the destination ---
    QString dest = QFileDialog::getExistingDirectory(
        m_shell.dialogParent(), T("move_mods_pick_dir"),
        QFileInfo(m_shell.modsDir()).absolutePath());
    if (dest.isEmpty()) return;
    dest = QFileInfo(dest).absoluteFilePath();

    // -- Destination validation ---
    if (!QFileInfo(dest).isDir()) {
        ui::warn(m_shell.dialogParent(), T("move_mods_title"), T("move_mods_err_dest_not_dir").arg(dest));
        return;
    }
    if (dest == modsRoot) {
        ui::warn(m_shell.dialogParent(), T("move_mods_title"), T("move_mods_err_dest_same"));
        return;
    }
    // Reject nesting in either direction - catching someone picking a sub
    // folder of the current mods dir (or accidentally a parent that contains
    // the current mods dir).
    if (dest.startsWith(modsRoot + "/") || modsRoot.startsWith(dest + "/")) {
        ui::warn(m_shell.dialogParent(), T("move_mods_title"), T("move_mods_err_dest_nested"));
        return;
    }

    // Writable test: create + delete a stamp file.
    {
        QString stamp = QDir(dest).filePath(".nerev_write_test");
        QFile probe(stamp);
        if (!probe.open(QIODevice::WriteOnly)) {
            ui::warn(m_shell.dialogParent(), T("move_mods_title"), T("move_mods_err_dest_not_writable").arg(dest));
            return;
        }
        probe.close();
        QFile::remove(stamp);
    }

    if (!preflightFolderMoves(moves, dest, totalBytes, T("move_mods_title"))) return;

    // -- Final confirmation ---
    QMessageBox confirm(m_shell.dialogParent());
    confirm.setWindowTitle(T("move_mods_title"));
    confirm.setIcon(QMessageBox::Warning);
    confirm.setText(T("move_mods_confirm_text")
                        .arg(moves.size())
                        .arg(fmtMbGb(totalBytes))
                        .arg(dest));
    confirm.setInformativeText(T("move_mods_confirm_info"));
    auto *yes = confirm.addButton(T("move_mods_confirm_yes"),
                                   QMessageBox::DestructiveRole);
    confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.exec();
    if (confirm.clickedButton() != yes) return;

    moveModFolders(moves, dest, T("move_mods_title"), [this, dest](int) {
        // Whatever moved now lives under dest: point the profile there.
        m_shell.setModsDir(dest);
    });
}

void LibraryFlow::onConsolidateModsIntoActiveProfile()
{
    if (!m_shell.downloadsIdle()) {
        ui::warn(m_shell.dialogParent(), T("consolidate_mods_title"), T("move_mods_err_downloads"));
        return;
    }

    if (!m_shell.ensureModsDirForActiveProfile()) return;
    if (m_shell.modsDir().isEmpty()) return;

    m_shell.scheduleSizeScan();

    QList<FolderMove> moves;
    qint64 totalBytes = 0;

    const QString modsRoot = QFileInfo(m_shell.modsDir()).absoluteFilePath();

    const QList<ModEntry> rows = m_shell.model().all();
    for (const ModEntry &it : rows) {
        if (!it.isMod() || !it.isInstalled()) continue;
        const QString path = it.modPath;
        if (path.isEmpty() || !QFileInfo(path).isDir())              continue;

        const QString abs = QFileInfo(path).absoluteFilePath();
        // Skip mods that are already inside the active profile's modsDir
        // (or AT modsRoot, though that's an unusual layout).
        if (abs == modsRoot || abs.startsWith(modsRoot + "/"))       continue;

        FolderMove m;
        m.row        = it.rowId;
        m.oldPath    = abs;
        m.folderName = QFileInfo(abs).fileName();
        m.sizeBytes  = it.modSize;
        if (m.sizeBytes < 0) m.sizeBytes = 0;
        moves.append(m);
        totalBytes += m.sizeBytes;
    }

    if (moves.isEmpty()) {
        ui::info(m_shell.dialogParent(), T("consolidate_mods_title"), T("consolidate_mods_nothing"));
        return;
    }

    if (!preflightFolderMoves(moves, modsRoot, totalBytes, T("consolidate_mods_title")))
        return;

    // Confirmation: list the source roots so the user sees WHICH external
    // dirs are about to be drained.  Showing the source tells them what
    // "the other profile's modsDir will keep working" looks like.
    QSet<QString> sourceRoots;
    for (const FolderMove &m : moves)
        sourceRoots.insert(QFileInfo(m.oldPath).absolutePath());
    QStringList rootsList(sourceRoots.begin(), sourceRoots.end());
    rootsList.sort();
    const QString rootsText = rootsList.mid(0, 5).join("\n  • ")
        + (rootsList.size() > 5
              ? QStringLiteral("\n  … (+%1)").arg(rootsList.size() - 5)
              : QString());

    QMessageBox confirm(m_shell.dialogParent());
    confirm.setWindowTitle(T("consolidate_mods_title"));
    confirm.setIcon(QMessageBox::Warning);
    confirm.setText(T("consolidate_mods_confirm_text")
                        .arg(moves.size())
                        .arg(fmtMbGb(totalBytes))
                        .arg(QDir::toNativeSeparators(modsRoot)));
    confirm.setInformativeText(T("consolidate_mods_confirm_info").arg(rootsText));
    auto *yes = confirm.addButton(T("move_mods_confirm_yes"),
                                   QMessageBox::DestructiveRole);
    confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.exec();
    if (confirm.clickedButton() != yes) return;

    moveModFolders(moves, modsRoot, T("consolidate_mods_title"), {});
}

// Clean Up Mod Folders. Upgrades used to leave every previous build on disk
// (mod_naming::findStaleSiblings only matched a literal re-download of the same
// file), so libraries accumulated folders nothing points at - one real one had
// 96 of them holding 8.6 GiB. The install path no longer creates them; this
// reclaims what already piled up.
//
// Deliberately manual and preview-first: it is a recursive delete over the
// user's mod library, so it never runs on its own and never deletes without an
// explicit confirm.
void LibraryFlow::onCleanUpModFolders()
{
    // An in-flight download's extract dir is not in the modlist yet, so it would
    // read as an orphan. Same for an extraction still running behind a
    // still-spinning row.
    if (!m_shell.downloadsIdle()) {
        ui::warn(m_shell.dialogParent(), T("cleanup_folders_title"), T("move_mods_err_downloads"));
        return;
    }
    const QList<ModEntry> modRows = m_shell.model().all();
    for (int i = 0; i < modRows.size(); ++i) {
        if (modRows.at(i).isInstalling()) {
            ui::warn(m_shell.dialogParent(), T("cleanup_folders_title"), T("move_mods_err_downloads"));
            return;
        }
    }
    if (m_shell.modsDir().isEmpty() || !QDir(m_shell.modsDir()).exists()) {
        ui::warn(m_shell.dialogParent(), T("cleanup_folders_title"), T("cleanup_folders_no_dir"));
        return;
    }

    const QString modsRoot = QFileInfo(m_shell.modsDir()).absoluteFilePath();

    // Three independent reference sources, so one unreadable file cannot make
    // the sweep think the library is empty.
    QStringList referenced;
    for (const ModEntry &e : m_shell.model().all()) {
        if (!e.isMod()) continue;
        for (const QString &p : {e.modPath, e.intendedModPath,
                                 e.mergeTargetPath, e.prevModPath})
            if (!p.isEmpty()) referenced << p;
    }
    for (const auto &prof : m_shell.otherProfileModlists())
        for (const ModEntry &e : prof.second)
            if (!e.modPath.isEmpty()) referenced << e.modPath;
    {   // data= lines the game config still points at
        QFile cfg(QDir::homePath() + QStringLiteral("/.config/openmw/openmw.cfg"));
        if (cfg.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&cfg);
            while (!in.atEnd()) {
                const QString line = in.readLine().trimmed();
                if (!line.startsWith(QStringLiteral("data="))) continue;
                QString p = line.mid(5).trimmed();
                if (p.startsWith('"') && p.endsWith('"') && p.size() >= 2)
                    p = p.mid(1, p.size() - 2);
                if (!p.isEmpty()) referenced << p;
            }
        }
    }

    QStringList onDisk;
    {
        const QDir root(modsRoot);
        for (const QFileInfo &fi : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (fi.isSymLink()) continue;   // never recurse out of the mods dir
            // Already staged for deletion by removeFoldersAsync.
            if (fi.fileName().contains(QStringLiteral(".__deleting__"))) continue;
            onDisk << fi.fileName();
        }
    }

    const QStringList orphans =
        mod_cleanup::unreferencedFolders(modsRoot, onDisk, referenced);
    if (orphans.isEmpty()) {
        ui::info(m_shell.dialogParent(), T("cleanup_folders_title"), T("cleanup_folders_none"));
        return;
    }
    // A modlist that failed to parse looks exactly like a huge pile-up. Refuse
    // rather than act on it. (Calibration: the observed real case was 26%.)
    if (!onDisk.isEmpty() && orphans.size() * 100 / onDisk.size() > 60) {
        ui::warn(m_shell.dialogParent(), T("cleanup_folders_title"),
                 T("cleanup_folders_suspicious")
                     .arg(orphans.size()).arg(onDisk.size()));
        return;
    }

    QList<QPair<qint64, QString>> sized;
    qint64 totalBytes = 0;
    for (const QString &name : orphans) {
        const qint64 b = folderSizeBytes(QDir(modsRoot).filePath(name));
        sized.append({ b, name });
        totalBytes += b;
    }
    std::sort(sized.begin(), sized.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });

    QString body;
    for (const auto &e : sized)
        body += QStringLiteral("%1  %2\n")
                    .arg(cleanupFmtBytes(e.first), 10).arg(e.second);

    const QString summary = T("cleanup_folders_summary")
                                .arg(orphans.size())
                                .arg(cleanupFmtBytes(totalBytes));
    if (!ui::confirmMonospaceReport(m_shell.dialogParent(), T("cleanup_folders_title"), body,
                                    720, 480, summary,
                                    T("cleanup_folders_accept").arg(orphans.size())))
        return;

    QStringList toDelete;
    for (const auto &e : sized)
        toDelete << QDir(modsRoot).filePath(e.second);
    removeModFoldersAsync(toDelete);

    m_shell.scheduleSizeScan();
    m_shell.showStatus(
        T("cleanup_folders_done").arg(QString::number(orphans.size()),
                                      cleanupFmtBytes(totalBytes)),
        8000);
}

