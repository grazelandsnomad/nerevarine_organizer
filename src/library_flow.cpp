#include "library_flow.h"

#include "async_guarded.h"
#include "mainwindow_internal.h"
#include "mod_cleanup.h"
#include "modlist_model.h"
#include "modrow.h"
#include "notify_banner.h"
#include "prompts.h"
#include "placeholder_state.h"
#include "pluginparser.h"
#include "report_dialog.h"
#include "safe_fs.h"
#include "settings.h"
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
                // may be gone by then: posted to qApp and checked there, on
                // the GUI thread, never posted to a flow that may be deleted
                // between a check here and the post (see async_guarded.h).
                QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, drain] {
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
    // Nor may a row still be installing: its folder is being written this
    // moment, and with a toolbar button the move is one click away from it.
    for (const ModEntry &it : m_shell.model().all()) {
        if (it.isInstalling()) {
            ui::warn(m_shell.dialogParent(), T("move_mods_title"), T("move_mods_err_installing"));
            return;
        }
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
namespace {

// Worth offering to the modlist: the folder holds something a game would
// load. The same two scans the config writer uses to decide what a mod
// contributes, so a folder that would register nothing is not offered.
bool folderHoldsGameData(const QString &path)
{
    if (!plugins::collectDataFolders(path, plugins::contentExtensions()).isEmpty())
        return true;
    return !plugins::collectResourceFolders(path).isEmpty();
}

} // namespace

LibraryFlow::OrphanInputs LibraryFlow::orphanInputs() const
{
    OrphanInputs in;
    if (m_shell.modsDir().isEmpty() || !QDir(m_shell.modsDir()).exists()) return in;
    in.modsRoot = QFileInfo(m_shell.modsDir()).absoluteFilePath();

    // Three independent reference sources, so one unreadable file cannot make
    // the sweep think the library is empty.
    for (const ModEntry &e : m_shell.model().all()) {
        if (!e.isMod()) continue;
        for (const QString &p : {e.modPath, e.intendedModPath,
                                 e.mergeTargetPath, e.prevModPath})
            if (!p.isEmpty()) in.referenced << p;
    }
    for (const auto &prof : m_shell.otherProfileModlists())
        for (const ModEntry &e : prof.second)
            if (!e.modPath.isEmpty()) in.referenced << e.modPath;
    // A sibling profile whose file is there and would not open names mods we
    // cannot see, and every one of them then reads as unused. Nothing may act
    // on that picture; the caller decides how loudly to say so.
    if (m_shell.unreadableProfileModlists() > 0) in.referencesComplete = false;
    {   // data= lines the game config still points at
        QFile cfg(QDir::homePath() + QStringLiteral("/.config/openmw/openmw.cfg"));
        if (cfg.exists() && !cfg.isOpen() && !QFileInfo(cfg.fileName()).isReadable())
            in.referencesComplete = false;
        if (cfg.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream ts(&cfg);
            while (!ts.atEnd()) {
                const QString line = ts.readLine().trimmed();
                if (!line.startsWith(QStringLiteral("data="))) continue;
                QString p = line.mid(5).trimmed();
                if (p.startsWith('"') && p.endsWith('"') && p.size() >= 2)
                    p = p.mid(1, p.size() - 2);
                if (!p.isEmpty()) in.referenced << p;
            }
        }
    }
    return in;
}

LibraryFlow::OrphanScan LibraryFlow::scanOrphans(const OrphanInputs &in)
{
    OrphanScan out;
    if (in.modsRoot.isEmpty()) return out;

    const QDir root(in.modsRoot);
    for (const QFileInfo &fi : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (fi.isSymLink()) continue; // never recurse out of the mods dir
        // Already staged for deletion by removeModFoldersAsync.
        if (fi.fileName().contains(QStringLiteral(".__deleting__"))) continue;
        out.onDisk << fi.fileName();
    }

    out.orphans =
        mod_cleanup::unreferencedFolders(in.modsRoot, out.onDisk, in.referenced);
    // A modlist that failed to parse looks exactly like a huge pile-up, so
    // deleting is off the table. (Calibration: the observed real case was
    // 26%.) Adding the folders to the list stays available: it is reversible,
    // and a user who keeps many spare folders looks the same from here.
    out.suspicious =
        !out.onDisk.isEmpty() && out.orphans.size() * 100 / out.onDisk.size() > 60;

    for (const QString &name : std::as_const(out.orphans))
        if (folderHoldsGameData(root.filePath(name))) out.adoptable << name;

    // Measuring is what the delete preview shows and sorts by, and it is the
    // expensive half - a recursive stat of every file of every orphan. It
    // stays here, on the worker, and only when someone asked for it.
    if (in.wantSizes) {
        for (const QString &name : std::as_const(out.orphans)) {
            const qint64 b = folderSizeBytes(root.filePath(name));
            out.sized.append({b, name});
            out.totalBytes += b;
        }
        std::sort(out.sized.begin(), out.sized.end(),
                  [](const auto &a, const auto &b) { return a.first > b.first; });
    }
    return out;
}

namespace {

// Nothing may be added while the library is being written into: an extract
// dir, a staged BAIN tree or a half-unpacked install is in no modlist yet and
// would read as a folder the user made by hand.
bool libraryIsSettled(const WindowShell &shell)
{
    if (!shell.downloadsIdle()) return false;
    const QList<ModEntry> rows = const_cast<WindowShell &>(shell).model().all();
    for (int i = 0; i < rows.size(); ++i)
        if (rows.at(i).isInstalling()) return false;
    return true;
}

// At most this many folders join the list in one pass. A library whose
// modlist is half-parsed can hand back a hundred perfectly real-looking
// folders, and a hundred rows appearing at once is indistinguishable from a
// bug; the rest come on the next pass, or through Clean Up Mod Folders.
constexpr int kMaxAdoptedPerPass = 20;

} // namespace

void LibraryFlow::adoptNewFolders()
{
    if (!m_shell.hasProfile() || m_adoptScanning) return;
    const QString gameId = m_shell.currentGameId();
    if (!Settings::autoAdoptFolders(gameId)) return;
    if (!libraryIsSettled(m_shell)) return;
    if (m_shell.modsDir().isEmpty()) return;

    // One stat, and usually the whole pass is over: the directory's mtime
    // changes when a folder is added or removed, so an unchanged stamp means
    // there is nothing new to find. This is what lets the window-activation
    // hook be free in the common case.
    const QString   root  = QFileInfo(m_shell.modsDir()).absoluteFilePath();
    const QDateTime stamp = QFileInfo(root).lastModified();
    if (root == m_lastAdoptRoot && stamp.isValid() && stamp == m_lastAdoptStamp) return;

    OrphanInputs in = orphanInputs();
    if (in.modsRoot.isEmpty()) return;
    // An incomplete picture is not evidence that anything is unused. Say
    // nothing and leave the stamp alone, so the next pass tries again.
    if (!in.referencesComplete) return;
    in.wantSizes = false;

    m_adoptScanning           = true;
    const quint64 epoch       = ++m_scanEpoch;
    const QString listAtStart = m_shell.modlistPath();

    async::guarded(
        this, [in](LibraryFlow *) { return scanOrphans(in); },
        [epoch, root, stamp, listAtStart, gameId](LibraryFlow      *self,
                                                  const OrphanScan &scan) {
            self->m_adoptScanning = false;
            // The user can switch profile while a walk of a slow mount runs;
            // everything below was decided about a list that may be gone.
            if (epoch != self->m_scanEpoch) return;
            if (self->m_shell.modlistPath() != listAtStart) return;
            if (self->m_shell.currentGameId() != gameId) return;
            if (QFileInfo(self->m_shell.modsDir()).absoluteFilePath() != root) return;
            // And a download may have started during it.
            if (!libraryIsSettled(self->m_shell)) return;
            if (scan.suspicious) return;
            self->applyAdoption(scan, root, stamp, gameId);
        });
}

void LibraryFlow::applyAdoption(const OrphanScan &scan, const QString &modsRoot,
                                const QDateTime &stamp, const QString &gameId)
{
    // Folders the user took out of the list while keeping the files. Entries
    // whose folder is gone are dropped here rather than growing forever.
    const QStringList remembered = Settings::ignoredModFolders(gameId);
    QStringList       stillThere;
    for (const QString &p : remembered)
        if (QFileInfo::exists(p)) stillThere << p;
    if (stillThere != remembered) Settings::setIgnoredModFolders(gameId, stillThere);
    const QSet<QString> ignored(stillThere.begin(), stillThere.end());

    // A row may have appeared for one of these while the scan ran.
    QSet<QString> alreadyRows;
    for (const ModEntry &e : m_shell.model().all())
        if (e.isMod() && !e.modPath.isEmpty())
            alreadyRows.insert(QDir::cleanPath(QFileInfo(e.modPath).absoluteFilePath()));

    QStringList fresh;
    for (const QString &name : scan.adoptable) {
        const QString path = QDir::cleanPath(QDir(modsRoot).absoluteFilePath(name));
        if (ignored.contains(path)) continue;
        if (alreadyRows.contains(path)) continue;
        if (!QFileInfo(path).isDir()) continue; // gone since the scan
        fresh << path;
    }

    // Nothing to do, but the directory has been looked at: remember the stamp
    // so the next activation costs one stat.
    m_lastAdoptRoot  = modsRoot;
    m_lastAdoptStamp = stamp;
    if (fresh.isEmpty()) return;

    const int held = qMax(0, int(fresh.size()) - kMaxAdoptedPerPass);
    if (held > 0) fresh = fresh.mid(0, kMaxAdoptedPerPass);

    for (const QString &path : std::as_const(fresh)) {
        ModEntry e = ModEntry::named(QFileInfo(path).fileName());
        placeholder_state::markInstalled(e, path);
        // Unticked, and set BEFORE the row exists. Unchecking a live row
        // emits a check-state change, which the window answers with the
        // "these mods depend on it, disable them too?" dialog - at startup,
        // for a mod nothing has ever depended on.
        e.checked = false;
        m_shell.model().append(e);
    }
    m_shell.saveModList();
    m_shell.updateModCount();
    m_shell.updateSectionCounts();
    m_shell.scheduleConflictScan();
    m_shell.scheduleSizeScan();

    const QString msg = held > 0 ? T("folders_adopted_capped").arg(fresh.size()).arg(held)
                                 : T("folders_adopted").arg(fresh.size());
    m_shell.showStatus(msg, 10000);
    // The banner is the visible half, but there is no window in a test.
    if (NotifyBanner *banner = m_shell.notify())
        banner->show(msg, QStringLiteral("#8a4a12"));
}

void LibraryFlow::onCleanUpModFolders()
{
    // An in-flight download's extract dir is not in the modlist yet, so it would
    // read as an orphan. Same for an extraction still running behind a
    // still-spinning row.
    if (!libraryIsSettled(m_shell)) {
        ui::warn(m_shell.dialogParent(), T("cleanup_folders_title"),
                 T("move_mods_err_downloads"));
        return;
    }
    if (m_shell.modsDir().isEmpty() || !QDir(m_shell.modsDir()).exists()) {
        ui::warn(m_shell.dialogParent(), T("cleanup_folders_title"),
                 T("cleanup_folders_no_dir"));
        return;
    }
    if (m_cleanupScanning) return; // the menu and a second click

    OrphanInputs inputs = orphanInputs();
    if (inputs.modsRoot.isEmpty()) return;
    // Sizes are what this dialog shows and sorts by, so they are wanted here -
    // and they are why the whole scan has to be off the GUI thread: measuring
    // every orphan is a recursive stat of possibly tens of thousands of files
    // on a mount that can be slow, and it used to freeze the window outright.
    inputs.wantSizes = true;

    m_cleanupScanning         = true;
    const quint64 epoch       = ++m_scanEpoch;
    const QString listAtStart = m_shell.modlistPath();
    const QString root        = inputs.modsRoot;
    m_shell.showStatus(T("cleanup_folders_scanning"), 0);

    async::guarded(
        this, [inputs](LibraryFlow *) { return scanOrphans(inputs); },
        [epoch, root, listAtStart](LibraryFlow *self, const OrphanScan &scan) {
            self->m_cleanupScanning = false;
            if (epoch != self->m_scanEpoch) return;
            if (self->m_shell.modlistPath() != listAtStart) return;
            // A download can have started while a slow mount was walked, and
            // its extract dir would be in the list about to be deleted.
            if (!libraryIsSettled(self->m_shell)) {
                self->m_shell.showStatus(T("move_mods_err_downloads"), 8000);
                return;
            }
            self->showCleanUpReport(scan, root);
        });
}

void LibraryFlow::showCleanUpReport(const OrphanScan &scan, const QString &modsRoot)
{
    m_shell.showStatus(QString(), 1);
    const QStringList &orphans = scan.orphans;
    if (orphans.isEmpty()) {
        ui::info(m_shell.dialogParent(), T("cleanup_folders_title"),
                 T("cleanup_folders_none"));
        return;
    }
    if (scan.suspicious)
        ui::warn(
            m_shell.dialogParent(), T("cleanup_folders_title"),
            T("cleanup_folders_suspicious").arg(orphans.size()).arg(scan.onDisk.size()));

    // A folder holding something a game would load can join the modlist
    // instead of being deleted - the MO2 habit of making a mod folder by hand
    // and having it picked up (GitHub issue #2). One marked "+" per line.
    // Most of these now become rows on their own before anyone opens this
    // dialog; what is left here is the rest.
    const QStringList &adoptable = scan.adoptable;

    QString body;
    for (const auto &e : scan.sized)
        body += QStringLiteral("%1  %2  %3\n")
                    .arg(adoptable.contains(e.second) ? QStringLiteral("+")
                                                      : QStringLiteral(" "))
                    .arg(cleanupFmtBytes(e.first), 10).arg(e.second);

    // What deleting will actually do, decided before the user agrees to it.
    const bool toTrash = safefs::trashable(modsRoot);
    QString    summary = T("cleanup_folders_summary")
                          .arg(orphans.size())
                          .arg(cleanupFmtBytes(scan.totalBytes));
    if (!adoptable.isEmpty())
        summary += QLatin1Char('\n') + T("cleanup_folders_adoptable").arg(adoptable.size());
    summary += QLatin1Char('\n') + (toTrash ? T("cleanup_folders_will_trash")
                                            : T("cleanup_folders_will_delete"));

    const ui::ReportChoice choice = ui::chooseMonospaceReport(
        m_shell.dialogParent(), T("cleanup_folders_title"), body, 720, 480, summary,
        adoptable.isEmpty() ? QString()
                            : T("cleanup_folders_adopt").arg(adoptable.size()),
        scan.suspicious ? QString()
                        : (toTrash ? T("cleanup_folders_accept_trash").arg(orphans.size())
                                   : T("cleanup_folders_accept").arg(orphans.size())));

    if (choice == ui::ReportChoice::Safe) {
        if (m_shell.undo()) m_shell.undo()->pushUndo();
        // Asking for a folder back undoes having sent it away before.
        const QString gameId  = m_shell.currentGameId();
        QStringList   ignored = Settings::ignoredModFolders(gameId);
        bool          changed = false;
        for (const QString &name : adoptable) {
            const QString path = QDir(modsRoot).filePath(name);
            if (ignored.removeAll(QDir::cleanPath(QFileInfo(path).absoluteFilePath())) >
                0)
                changed = true;
            m_shell.installFolderAsMod(path);
        }
        if (changed) Settings::setIgnoredModFolders(gameId, ignored);
        m_shell.saveModList();
        m_shell.updateModCount();
        m_shell.updateSectionCounts();
        m_shell.scheduleConflictScan();
        m_shell.scheduleSizeScan();
        m_shell.showStatus(T("cleanup_folders_adopted").arg(adoptable.size()), 8000);
        return;
    }
    if (choice != ui::ReportChoice::Destructive) return;

    QStringList toDelete;
    for (const auto &e : scan.sized) toDelete << QDir(modsRoot).filePath(e.second);

    // Trashing writes a .trashinfo and renames per folder; deleting walks the
    // trees. Neither belongs on the thread drawing the window.
    const qint64 bytes = scan.totalBytes;
    async::guarded(
        this,
        [toDelete, toTrash](LibraryFlow *) {
            return safefs::removeFolders(toDelete, toTrash);
        },
        [bytes, toTrash](LibraryFlow *self, const safefs::RemovedFolders &out) {
            self->m_shell.scheduleSizeScan();
            QString msg;
            if (!out.trashed.isEmpty())
                msg = T("cleanup_folders_trashed")
                          .arg(out.trashed.size())
                          .arg(cleanupFmtBytes(bytes));
            else if (!out.deleted.isEmpty())
                msg =
                    T("cleanup_folders_done")
                        .arg(QString::number(out.deleted.size()), cleanupFmtBytes(bytes));
            // A folder that could not be moved is still on disk. Saying so is
            // the whole point: it was never deleted as a consolation prize.
            if (!out.failed.isEmpty()) {
                const QString left =
                    T("cleanup_folders_not_removed").arg(out.failed.size());
                msg = msg.isEmpty() ? left : msg + QLatin1Char(' ') + left;
                QStringList names;
                for (const QString &p : out.failed) names << QFileInfo(p).fileName();
                if (names.size() > 5)
                    names = names.mid(0, 5)
                            << QStringLiteral("… (+%1)").arg(names.size() - 5);
                ui::warn(self->m_shell.dialogParent(), T("cleanup_folders_title"),
                         left + QLatin1Char('\n') + names.join(QStringLiteral("\n  ")));
            }
            Q_UNUSED(toTrash);
            if (!msg.isEmpty()) self->m_shell.showStatus(msg, 10000);
        });
}
