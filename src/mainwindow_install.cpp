// mainwindow_install - the archive verify/extract/add install pipeline slots.
// Split out of mainwindow.cpp for parallel compilation; same class, own TU.

#include "mainwindow.h"
#include "mainwindow_internal.h"
#include "settings.h"
#include "theme.h"
#include "load_order_merge.h"
#include "separatordialog.h"
#include "modlistdelegate.h"
#include "modroles.h"
#include "translator.h"
#include "fomod_install.h"
#include "fomod_copy.h"
#include "post_install.h"
#include "placeholder_state.h"
#include "fomodwizard.h"
#include "bain.h"
#include "bainwizard.h"
#include "bethesda_deploy.h"
#include "bethesda_loadorder.h"
#include "bethesda_archives.h"
#include "proton_paths.h"
#include "extract_errors.h"
#include "archive_magic.h"
#include "installcontroller.h"
#include "install_layout.h"
#include "modlist_model.h"
#include "modlist_model_widget_bridge.h"
#include "modlist_serializer.h"
#include "modlist_summary_dialog.h"
#include "mod_sharing.h"
#include "loadordercontroller.h"
#include "nexusclient.h"
#include "nexuscontroller.h"
#include "nxmurl.h"
#include "downloadqueue.h"
#include "undo_stack.h"
#include "zoom_controller.h"
#include "filter_bar.h"
#include "notify_banner.h"
#include "column_header.h"
#include "forbidden_mods.h"
#include "game_profiles.h"
#include "game_adapter.h"
#include "ini_doc.h"
#include "nexus_name.h"
#include "conflict_inspector.h"
#include "deployment_report.h"
#include "report_dialog.h"
#include "conflict_scan.h"
#include "toolbar_customization.h"
#include "scan_coordinator.h"
#include "backup_manager.h"
#include "bulk_install_queue.h"
#include "review_updates_dialog.h"
#include "launch_warnings.h"
#include "modlist_sort.h"
#include "send_to_dialog.h"
#include "logging.h"
#include "prompts.h"
#include "subprocess.h"
#include <QPushButton>
#include <QTimer>
#include <algorithm>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSet>
#include <QToolBar>
#include <QToolButton>
#include <QMenuBar>
#include <QStatusBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QIcon>
#include <QMessageBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProgressDialog>
#include <QStorageInfo>
#include <QProcess>
#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>
#include <QCheckBox>
#include <QComboBox>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QStandardPaths>
#include <QTextStream>
#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QCloseEvent>
#include <QClipboard>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QTableWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QScrollBar>
#include <QShortcut>
#include <QThread>
#include <QDirIterator>
#include <QDockWidget>
#include <QCryptographicHash>
#include <QPainter>
#include <QPixmap>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QMutex>
#include <QMutexLocker>
#include <QtConcurrent/QtConcurrent>
#include "firstrunwizard.h"
#include "fs_utils.h"
#include "pluginparser.h"
#include "mod_naming.h"
#include "modlist_io.h"
#include "openmwconfigwriter.h"
#include "log_triage.h"
#include "log_triage_dialog.h"
#include "plugin_collisions.h"
#include "asset_collisions.h"
#include "modlist_sync_guard.h"
#include "safe_fs.h"
#include "deps_resolver.h"
#include <QFutureWatcher>
using plugins::collectDataFolders;
using plugins::readTes3Masters;
#include <QDropEvent>
#include <QMimeData>
using fsutils::sanitizeFolderName;
#include "mainwindow_internal.h"

// Moved file-statics, forward-declared so intra-TU order is irrelevant.
static QListWidgetItem *findPendingRowForModId(QListWidget *list, int modId, const QString &gameId);
static int modIdFromArchiveName(const QString &archiveFileName);

void MainWindow::onArchiveVerificationFailed(const QString &archivePath,
                                             const QUuid &installToken,
                                             InstallController::VerifyFailKind kind,
                                             const QString &actual,
                                             const QString &expected)
{
    // Archive is toast regardless of kind - remove it.
    QFile::remove(archivePath);

    QString profileKey;
    QListWidgetItem *placeholder = findPlaceholderByToken(installToken, &profileKey);
    // Reset the row to "not installed" so the user can retry. Skip if it was
    // removed mid-verify.
    if (placeholder) {
        placeholder->setData(ModRole::InstallStatus,    0);
        placeholder->setData(ModRole::DownloadProgress, QVariant());
        placeholder->setData(ModRole::ExpectedMd5,      QVariant());
        placeholder->setData(ModRole::ExpectedSize,     QVariant());
        placeholder->setData(ModRole::NexusFileName,    QVariant());
        placeholder->setData(ModRole::InstallToken,     QVariant());
        placeholder_state::restoreInteractiveFlags(placeholder);
        // Restore the display name, stripping the "⠋ installing…" prefix.
        QString name = placeholder->data(ModRole::CustomName).toString();
        if (name.isEmpty()) {
            name = QFileInfo(placeholder->data(ModRole::ModPath).toString()).fileName();
            if (name.isEmpty()) name = QFileInfo(archivePath).completeBaseName();
        }
        if (!name.isEmpty()) placeholder->setText(name);
        if (profileKey.isEmpty()) {
            saveModList();
        } else {
            // Stranded under another profile - persist the reset to that
            // profile's modlist file without disturbing the active one.
            saveModListFor(profileKey, placeholder);
        }
    }

    const QString fileName = QFileInfo(archivePath).fileName();
    const QString body = (kind == InstallController::VerifyFailKind::Size)
        ? T("verify_mismatch_size").arg(fileName).arg(actual.toLongLong()).arg(expected.toLongLong())
        : T("verify_mismatch_md5").arg(fileName, actual, expected);
    ui::warn(this, T("verify_error_title"), body);
    statusBar()->showMessage(T("verify_status_failed"), 6000);
}

void MainWindow::onExtractionFailed(const QString &archivePath,
                                    const QString &extractDir,
                                    const QUuid &installToken,
                                    InstallController::ExtractFailKind kind,
                                    const QString &detail)
{
    Q_UNUSED(installToken);
    const QFileInfo fi(archivePath);
    const bool missing = (kind == InstallController::ExtractFailKind::ProgramMissing);

    // Route via the pure, unit-tested helper. Sniff the container from the bytes
    // (the archive still exists; it's removed below) rather than the extension -
    // downloads stage under a bare token UUID with none, so a failed RAR still
    // gets the unrar/p7zip message and a nonzero exit never blames a missing 7z.
    archive_magic::Format fmt = archive_magic::Format::Unknown;
    {
        QFile hf(archivePath);
        if (hf.open(QIODevice::ReadOnly)) fmt = archive_magic::sniff(hf.read(16));
    }
    const QString key = extract_errors::failureKey(missing, detail, fmt);
    QString body;
    if (key == QLatin1String("extraction_error_no_program"))
        body = T(key).arg(detail);                             // %1 = program name
    else if (key == QLatin1String("extraction_error_failed"))
        body = T(key).arg(fi.fileName()).arg(detail.toInt());  // %1 = file, %2 = code
    else
        body = T(key).arg(fi.fileName());                      // %1 = file
    ui::warn(this, T("extraction_error_title"), body);
    statusBar()->showMessage(T("status_extraction_failed"), 4000);
    QDir(extractDir).removeRecursively();
    QFile::remove(archivePath); // auto-clean on failure too
}

void MainWindow::onExtractionSucceeded(const QString &archivePath,
                                       const QString &extractDir,
                                       const QString &modPathIn,
                                       const QUuid &installToken)
{
    const QFileInfo fi(archivePath);
    QString modPath = modPathIn;

    QString profileKey;
    QListWidgetItem *placeholder = findPlaceholderByToken(installToken, &profileKey);
    if (!placeholder) {
        // Row was removed mid-extract (or app was restarted and the
        // placeholder didn't restore for some reason).  The extracted
        // folder is now orphaned data -- nothing references it, so wipe
        // it instead of leaking unreferenced GBs onto the user's disk.
        // The archive goes too.
        QDir(extractDir).removeRecursively();
        QFile::remove(archivePath);
        return;
    }

    // FOMOD installer: if the archive ships a ModuleConfig.xml, open the
    // wizard as a non-modal window so the user can run multiple wizards in
    // parallel and click between them freely.  All post-wizard bookkeeping
    // runs inside the onDone callback; we return immediately after show().
    if (FomodWizard::hasFomod(modPath)) {
        const QString priorChoices = placeholder->data(ModRole::FomodChoices).toString();
        // Existing mod names for the wizard's duplicate-name detection (model
        // read; see confirmReinstallIfInstalled for the migration rationale).
        const QStringList installedModNames =
            m_model ? m_model->modDisplayNames() : QStringList();
        const QString archiveFileName = fi.fileName();
        const QString title = placeholder->data(ModRole::NexusTitle).toString().trimmed();
        const QString sanitizedTitle  = sanitizeFolderName(title);

        // Capture the token, not the placeholder pointer -- the user may
        // switch profiles while the wizard is open, in which case the
        // placeholder lands in m_strandedInstalls and we need to re-look-up.
        FomodWizard::showAsync(modPath, priorChoices, this, installedModNames,
            [this, archivePath, extractDir, modPath,
             installToken, archiveFileName, sanitizedTitle]
            (const QString &fomodPath, const QString &fomodChoices) {

            QString fkey;
            QListWidgetItem *fph = findPlaceholderByToken(installToken, &fkey);
            if (fomodPath.isEmpty()) {
                QDir(extractDir).removeRecursively();
                QFile::remove(archivePath);
                if (fph) {
                    if (fkey.isEmpty()) {
                        resetPlaceholderAfterInstallCancel(fph, archivePath);
                    } else {
                        // Stranded: roll the placeholder back and persist into
                        // the owning profile's modlist file.
                        placeholder_state::resetToNotInstalled(
                            fph, QFileInfo(archivePath).completeBaseName());
                        saveModListFor(fkey, fph);
                    }
                }
                statusBar()->showMessage(T("fomod_cancelled"), 3000);
                return;
            }

            const auto promote = fomod_install::promote(
                extractDir, modPath, fomodPath, sanitizedTitle, m_modsDir);

            QString finalPath = modPath;
            if (promote.outcome == fomod_install::PromoteOutcome::EmptyFallback) {
                ui::warn(this, T("fomod_empty_title"), T("fomod_empty_body").arg(archiveFileName));
            } else {
                finalPath = promote.finalModPath;
                if (fph && !fomodChoices.isEmpty())
                    fph->setData(ModRole::FomodChoices, fomodChoices);
            }

            if (fph) {
                // Merge follow-through (FOMOD optionals that override the main
                // download).  promote() already removed extractDir, so finalPath
                // is the only throwaway folder to drop once merged.
                const QString effPath = applyPendingMerge(fph, finalPath, finalPath);
                if (fkey.isEmpty()) {
                    addModFromPath(effPath, fph);
                } else {
                    applyInstalledStateToStrandedPlaceholder(fph, effPath);
                    saveModListFor(fkey, fph);
                }
            }
            QFile::remove(archivePath);
        });
        return; // wizard is now shown; callback drives the rest
    }

    // BAIN installer: reached only after FOMOD declined. A BAIN archive groups
    // its content under numbered packages ("00 Core", "01 Optional", ...) the
    // user picks among. Detection is conservative (every top-level folder must
    // be numbered, no fomod/, no asset roots) and the picker pre-checks
    // everything, so a false positive on an install-everything mod (Tamriel
    // Rebuilt) is just one extra click with the same result as a plain install.
    if (bain::looksLikeBain(modPath)) {
        const QString archiveFileName = fi.fileName();
        const QString title = placeholder->data(ModRole::NexusTitle).toString().trimmed();
        const QString sanitizedTitle = sanitizeFolderName(title);
        const QString priorChoices = placeholder->data(ModRole::BainChoices).toString();

        BainWizard::showAsync(modPath, priorChoices, this,
            [this, archivePath, extractDir, modPath,
             installToken, archiveFileName, sanitizedTitle]
            (const QString &stagedPath, const QString &bainChoices) {

            QString fkey;
            QListWidgetItem *bph = findPlaceholderByToken(installToken, &fkey);
            if (stagedPath.isEmpty()) {
                QDir(extractDir).removeRecursively();
                QFile::remove(archivePath);
                if (bph) {
                    if (fkey.isEmpty()) {
                        resetPlaceholderAfterInstallCancel(bph, archivePath);
                    } else {
                        placeholder_state::resetToNotInstalled(
                            bph, QFileInfo(archivePath).completeBaseName());
                        saveModListFor(fkey, bph);
                    }
                }
                statusBar()->showMessage(T("bain_cancelled"), 3000);
                return;
            }

            // Reuse the FOMOD promote: move the staged merge out of extractDir
            // (dropping the unselected packages), rename to the title.
            const auto promote = fomod_install::promote(
                extractDir, modPath, stagedPath, sanitizedTitle, m_modsDir);

            QString finalPath = modPath;
            if (promote.outcome == fomod_install::PromoteOutcome::EmptyFallback) {
                ui::warn(this, T("fomod_empty_title"),
                         T("fomod_empty_body").arg(archiveFileName));
            } else {
                finalPath = promote.finalModPath;
                // Remember the package selection so a later reinstall/update
                // pre-ticks the same packages (like FOMOD).
                if (bph && !bainChoices.isEmpty())
                    bph->setData(ModRole::BainChoices, bainChoices);
            }

            if (bph) {
                // Merge follow-through (BAIN package overriding the main
                // download).  promote() already removed extractDir, so finalPath
                // is the only throwaway folder to drop once merged.
                const QString effPath = applyPendingMerge(bph, finalPath, finalPath);
                if (fkey.isEmpty()) {
                    addModFromPath(effPath, bph);
                } else {
                    applyInstalledStateToStrandedPlaceholder(bph, effPath);
                    saveModListFor(fkey, bph);
                }
            }
            QFile::remove(archivePath);
        });
        return; // picker shown; callback drives the rest
    }

    // Merge follow-through: when the user picked "Merge into existing", overlay
    // the freshly extracted files onto the existing folder and register the row
    // there; otherwise effPath == modPath and this is a no-op.  extractDir is
    // the throwaway wrapper to drop once a merge has consumed its contents.
    const QString effPath = applyPendingMerge(placeholder, modPath, extractDir);
    if (profileKey.isEmpty()) {
        addModFromPath(effPath, placeholder);
    } else {
        // Stranded: do the placeholder-only updates and persist to the
        // owning profile's modlist file.  m_modList iteration / load-order
        // / openmw.cfg sync are deferred until the user switches back.
        applyInstalledStateToStrandedPlaceholder(placeholder, effPath);
        saveModListFor(profileKey, placeholder);
    }
    QFile::remove(archivePath);
}

// Delete folders without freezing the GUI. Recursive removal on the (often
// NTFS/FUSE) mods drive takes seconds and stalled the UI after install/update.
//
// Can't defer the whole delete: the openmw.cfg sync that runs right after
// probes the disk (prepareForSync rescues an orphan-managed data= line whose
// folder still EXISTS with plugins), so a folder still present during that sync
// gets its data=/content= resurrected. So vacate the real path synchronously
// with a same-filesystem rename (metadata-only, fast even on NTFS, and the part
// that matters for cfg correctness), then defer only the slow byte-delete of
// the renamed folder. Callers must have dropped every modlist ref to these paths.
void removeModFoldersAsync(QStringList paths)
{
    paths.removeIf([](const QString &p) { return p.isEmpty(); });
    QStringList staged;
    for (const QString &p : paths) {
        if (!QFileInfo::exists(p)) continue;
        // Rename to a sibling that no longer matches the real path (so exists()
        // is immediately false there) nor any mod-folder shape (so it's inert to
        // the sibling-dedup / cfg scans until the async delete removes it).
        QString tmp = p + QStringLiteral(".__deleting__");
        for (int n = 1; QFileInfo::exists(tmp); ++n)
            tmp = p + QStringLiteral(".__deleting__") + QString::number(n);
        if (QDir().rename(p, tmp)) staged << tmp;
        else QDir(p).removeRecursively();  // rename failed - fall back to sync
    }
    if (staged.isEmpty()) return;
    (void)QtConcurrent::run([staged]() {
        for (const QString &p : staged) QDir(p).removeRecursively();
    });
}

QString MainWindow::applyPendingMerge(QListWidgetItem *placeholder,
                                      const QString &contentPath,
                                      const QString &discardDir)
{
    if (!placeholder) return contentPath;
    QString target = placeholder->data(ModRole::MergeTargetPath).toString();
    if (target.isEmpty()) return contentPath;          // no merge pending
    placeholder->setData(ModRole::MergeTargetPath, QVariant());  // consume

    const QString cleanTarget  = QDir::cleanPath(target);
    const QString cleanContent = QDir::cleanPath(contentPath);
    // Target vanished out-of-band (user deleted the folder between picking
    // Merge and the download finishing), or it already IS the content folder
    // -> nothing to overlay; fall back to registering the content as-is.
    if (cleanTarget == cleanContent || !QDir(target).exists())
        return contentPath;

    // Copy-on-write: a merge overlays files IN PLACE, so if the target folder is
    // shared with another profile, overlay onto a private copy instead and leave
    // the other profile's mod untouched.  Fall back to in-place if the copy
    // fails (a successful merge beats a lost optional).
    if (modPathReferencedByOtherProfile(cleanTarget)) {
        const QString forked = forkSharedModFolder(target);
        if (!forked.isEmpty()) {
            target = forked;
            statusBar()->showMessage(
                T("share_fork_on_merge").arg(QFileInfo(target).fileName()), 6000);
        }
    }

    // Overlay the freshly downloaded files on top of the existing mod folder.
    // fomod_copy::copyContents is last-writer-wins (it removes each colliding
    // destination before copying) and case-folds directory names, so the
    // optional download's files override the main download's - MO2's "merge".
    m_scans->invalidateDataFoldersCache(target);
    fomod_copy::copyContents(contentPath, target);

    // The freshly extracted/promoted folder is now redundant.  Drop it so it
    // doesn't linger as an orphan under the mods dir.  Guard against ever
    // recursing into the merge target itself.  Deleted off the GUI thread.
    if (!discardDir.isEmpty()
        && QDir::cleanPath(discardDir) != cleanTarget
        && QDir(discardDir).exists())
        removeModFoldersAsync({discardDir});

    statusBar()->showMessage(
        T("merge_done_status").arg(QFileInfo(target).fileName()), 5000);
    return target;
}

QString MainWindow::forkSharedModFolder(const QString &sharedPath)
{
    if (sharedPath.isEmpty() || m_modsDir.isEmpty()) return {};

    // Fresh, collision-safe destination under the active profile's mods dir.
    const QString folderName = QFileInfo(sharedPath).fileName();
    QString dest = QDir(m_modsDir).filePath(folderName);
    if (QFileInfo::exists(dest))
        dest += QStringLiteral("_") + QString::number(QDateTime::currentSecsSinceEpoch());

    // Verified recursive copy (cleans up its own partial dest on failure;
    // never touches the source).
    const auto r = safefs::copyTreeVerified(sharedPath, dest);
    if (!r) {
        ui::warn(this, T("share_fork_failed_title"),
                 T("share_fork_failed_body").arg(folderName));
        return {};
    }
    m_scans->invalidateDataFoldersCache(dest);
    return dest;
}

void MainWindow::onInstallFromNexus(QListWidgetItem *item)
{
    if (m_apiKey.isEmpty()) {
        ui::info(this, T("nxm_api_key_required_title"), T("nxm_api_key_required_body"));
        onSetApiKey();
        if (m_apiKey.isEmpty()) return;
    }

    const QString nexusUrl = item->data(ModRole::NexusUrl).toString();
    // path: /{game}/mods/{modId}
    const auto ref = parseNexusModUrl(nexusUrl);
    if (!ref) {
        ui::warn(this, T("nexus_api_error_title"), T("install_invalid_url"));
        return;
    }
    const QString game  = ref->game;
    const int     modId = ref->modId;

    // Forbidden mod check - hard block, no install-anyway escape.
    if (!confirmNotForbidden(game, modId)) return;

    // Already-installed guard - warn (and let user cancel) before re-installing.
    // The Search-on-Nexus flow installs into `item` itself rather than reusing
    // the existing match, so Replace and Separate are functionally identical
    // here - both proceed with `item` as the target.  Only Cancel aborts.
    // Merge is suppressed (allowMerge=false): there's no reused folder to
    // overlay onto in this flow, so offering it would mislead.
    const auto choice = confirmReinstallIfInstalled(game, modId, item,
                                                    /*allowMerge=*/false);
    if (choice == ReinstallChoice::Cancel) return;

    checkModDependencies(game, modId, item);
}

void MainWindow::addModFromPath(const QString &dirPath, QListWidgetItem *placeholder)
{
    QElapsedTimer addTimer;
    addTimer.start();

    // Finalization below is synchronous (modlist save + openmw.cfg sync); show a
    // busy cursor so the brief stall reads as work, not a hang.  Restored before
    // the modal post-install prompts.  Safe to leave un-RAII'd: this function has
    // no early returns between here and the restore.
    QApplication::setOverrideCursor(Qt::WaitCursor);

    QFileInfo fi(dirPath);

    QListWidgetItem *item;
    if (placeholder) {
        // Re-use the in-list placeholder; restore full drag/select flags.
        // Preserve any custom name the user set on the placeholder (e.g. via
        // rename, MO2 import, or Nexus title resolution) - falling back to the
        // folder name only when CustomName is empty.  Without this guard,
        // re-installing a renamed mod silently reverts the display text to
        // the ugly Nexus archive folder name.
        item = placeholder;
        QString cn = item->data(ModRole::CustomName).toString();
        item->setText(cn.isEmpty() ? fi.fileName() : cn);
        placeholder_state::restoreInteractiveFlags(item);
    } else {
        item = new QListWidgetItem(fi.fileName());
        m_modList->insertItem(m_modList->count(), item);
        m_modList->setCurrentItem(item);
        m_modList->scrollToItem(item);
    }

    // Capture whether this is an update-completion (i.e. the row had a
    // pending green triangle going in) before we flip state, so we can
    // both clear the arrow and refresh DateAdded to prevent the next
    // onCheckUpdates pass from immediately re-flagging the mod.
    const bool wasUpdate = item->data(ModRole::UpdateAvailable).toBool();

    // Update path captured by handleNxmUrl when an installed row was reused
    // as the placeholder. Resolve once now, before we overwrite ModPath, so
    // the old folder can be deleted after the new one is established.
    QString prevModPath = item->data(ModRole::PrevModPath).toString();
    if (!prevModPath.isEmpty()
        && QDir::cleanPath(prevModPath) == QDir::cleanPath(dirPath))
        prevModPath.clear();  // same folder reinstall, nothing to remove
    item->setData(ModRole::PrevModPath, QVariant());

    // Fallback when handleNxmUrl set no PrevModPath (a fresh placeholder rather
    // than a reused installed row): the folder this row is moving OFF of is
    // stale too. This is the only rule that catches bare-UUID folders from
    // extensionless CDN downloads - they carry no mod id, so the sibling
    // matchers below structurally cannot see them. The removal site downstream
    // re-checks inside-modsDir and cross-profile sharing before deleting.
    if (prevModPath.isEmpty()) {
        const QString outgoing = item->data(ModRole::ModPath).toString();
        if (!outgoing.isEmpty()
            && QDir::cleanPath(outgoing) != QDir::cleanPath(dirPath)) {
            bool referenced = false;
            for (int r = 0; r < m_modList->count() && !referenced; ++r) {
                auto *row = m_modList->item(r);
                if (row == item) continue;
                if (row->data(ModRole::ItemType).toString() != ItemType::Mod)
                    continue;
                for (int role : { ModRole::ModPath, ModRole::IntendedModPath }) {
                    const QString rmp = row->data(role).toString();
                    if (!rmp.isEmpty()
                        && QDir::cleanPath(rmp) == QDir::cleanPath(outgoing))
                        referenced = true;
                }
            }
            if (!referenced) prevModPath = outgoing;
        }
    }

    item->setData(ModRole::ItemType,      ItemType::Mod);
    item->setData(ModRole::ModPath,       dirPath);
    item->setData(ModRole::InstallStatus, 1); // installed
    m_scans->invalidateDataFoldersCache(dirPath);      // force re-scan next time
    item->setData(ModRole::UpdateAvailable, false);
    // A real install invalidates any silent-rebind patch repairEmptyModPaths
    // may have left on the row - the mod is now genuinely installed at
    // dirPath, so the "intended" path field has to be dropped or saveModList
    // would keep writing the now-stale previous path forever.
    item->setData(ModRole::IntendedModPath, QVariant());
    // Keep the existing date if the placeholder already had one (NXM flow sets it);
    // otherwise stamp now (manual add via addModFromPath with no placeholder).
    // For updates we explicitly refresh - the new install is what the next
    // update-check must compare the Nexus timestamp against.
    if (wasUpdate || !item->data(ModRole::DateAdded).toDateTime().isValid())
        item->setData(ModRole::DateAdded, QDateTime::currentDateTime());
    item->setCheckState(Qt::Checked);
    item->setToolTip(dirPath);

    // One folder per mod.  When a Nexus archive is re-downloaded, the default
    // path `<modsDir>/<baseName>` already exists and InstallController appends
    // "_<timestamp>" - left unchecked, reinstalls of the same mod pile up as
    // "<prefix>", "<prefix>_<ts1>", "<prefix>_<ts2>" …. mod_naming::findStaleSiblings
    // owns the "what counts as a sibling of THIS install" decision; this
    // block just runs the side effects (purge stale rows, recursive
    // remove, status bar).
    {
        const QFileInfo newInfo(dirPath);
        const QString parentDir = newInfo.absolutePath();
        const QString cleanMods = m_modsDir.isEmpty()
            ? QString() : QDir::cleanPath(m_modsDir);
        if (!cleanMods.isEmpty()
            && QDir::cleanPath(parentDir) == cleanMods)
        {
            QDir parent(parentDir);
            const QStringList subs = parent.entryList(
                QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            const QStringList stale =
                mod_naming::findStaleSiblings(newInfo.fileName(), subs);
            QStringList toDelete;
            for (const QString &sub : stale) {
                const QString oldPath = parent.absoluteFilePath(sub);
                // Purge any modlist rows still pointing at the old path;
                // leaving them behind would show up as "missing mod"
                // rows on the next scan.
                for (int r = m_modList->count() - 1; r >= 0; --r) {
                    auto *row = m_modList->item(r);
                    if (row == item) continue;
                    if (row->data(ModRole::ItemType).toString() != ItemType::Mod)
                        continue;
                    const QString rmp = row->data(ModRole::ModPath).toString();
                    if (QDir::cleanPath(rmp) == QDir::cleanPath(oldPath))
                        delete m_modList->takeItem(r);
                }
                // Keep the files if another profile shares this folder.
                if (modPathReferencedByOtherProfile(mod_sharing::cleanModPath(oldPath)))
                    continue;
                toDelete << oldPath;
            }

            // Older BUILDS of the same mod. findStaleSiblings above only sees a
            // literal re-download of the same file; an upgrade lands under a
            // wholly different "<Name>-<id>-<newVersion>-<newTs>" and was
            // invisible to it, which is how mods dirs accumulated nine builds of
            // one mod. The id comes from the row's URL, never from the folder
            // name (titles contain dashes).
            const auto ref =
                parseNexusModUrl(item->data(ModRole::NexusUrl).toString());
            if (ref) {
                const QStringList older = mod_naming::findOlderVersionSiblings(
                    newInfo.fileName(), subs, ref->modId);
                for (const QString &sub : older) {
                    const QString oldPath = parent.absoluteFilePath(sub);
                    if (toDelete.contains(oldPath)) continue;
                    // One mod id can legitimately own several installed folders
                    // (separate files on one Nexus page - e.g. mod 53318 ships
                    // "Dwemer Towers" and "Daedric Beacons" as two mods). So
                    // unlike the same-file case above, a still-referenced folder
                    // is a DIFFERENT mod: skip it, never purge its row.
                    bool referenced = false;
                    for (int r = 0; r < m_modList->count() && !referenced; ++r) {
                        auto *row = m_modList->item(r);
                        if (row == item) continue;
                        if (row->data(ModRole::ItemType).toString() != ItemType::Mod)
                            continue;
                        for (int role : { ModRole::ModPath, ModRole::IntendedModPath }) {
                            const QString rmp = row->data(role).toString();
                            if (!rmp.isEmpty()
                                && QDir::cleanPath(rmp) == QDir::cleanPath(oldPath))
                                referenced = true;
                        }
                    }
                    if (referenced) continue;
                    if (modPathReferencedByOtherProfile(
                            mod_sharing::cleanModPath(oldPath)))
                        continue;
                    toDelete << oldPath;
                }
            }
            // The actual recursive deletes run off the GUI thread (NTFS mods
            // drive makes them slow); references are already dropped above.
            removeModFoldersAsync(toDelete);
            if (!toDelete.isEmpty())
                statusBar()->showMessage(
                    T("mod_cleaned_siblings").arg(toDelete.size()), 5000);
        }
    }

    // Update flow: if handleNxmUrl reused an installed row, its old folder
    // is captured in prevModPath. The sibling-prefix dedup above misses
    // updates whose archive folder name diverges (different version, hash,
    // etc.), so do an explicit remove here. Skip if the path no longer
    // exists, or if it's outside m_modsDir (paranoia: never recurse-delete
    // arbitrary user paths).
    if (!prevModPath.isEmpty()) {
        const QString cleanOld = QDir::cleanPath(prevModPath);
        const QString cleanRoot = m_modsDir.isEmpty()
            ? QString() : QDir::cleanPath(m_modsDir);
        // Strictly BELOW the root: a row whose path is the mods dir itself
        // would otherwise hand the whole library to a recursive delete.
        const bool insideMods = !cleanRoot.isEmpty()
            && cleanOld.startsWith(cleanRoot + QLatin1Char('/'));
        // Don't delete the old folder if another profile shares it.
        // (prevModPath is already guaranteed != dirPath above, so deferring the
        // delete can't race-wipe the freshly-installed folder.)
        if (insideMods && QDir(prevModPath).exists()
            && !modPathReferencedByOtherProfile(cleanOld)) {
            m_scans->invalidateDataFoldersCache(prevModPath);
            removeModFoldersAsync({prevModPath});   // off the GUI thread
            statusBar()->showMessage(T("mod_cleaned_siblings").arg(1), 5000);
        }
    }

    // If the installed folder has a generic name (e.g. "scripts"), try to replace
    // it with the actual Nexus mod title so the list entry is human-readable.
    // The "what counts as generic" decision lives in mod_naming::folderNameLooksGeneric
    // (curated list + Nexus-archive slug regexes + generic-prefix detection).
    const QString folderLc = fi.fileName().toLower();
    const bool genericName = mod_naming::folderNameLooksGeneric(fi.fileName());
    if (item->data(ModRole::CustomName).toString().isEmpty() && genericName)
    {
        QString cachedTitle = item->data(ModRole::NexusTitle).toString().trimmed();
        if (!cachedTitle.isEmpty()) {
            item->setData(ModRole::CustomName, cachedTitle);
            item->setText(cachedTitle);
        } else {
            // NexusTitle not cached yet - try to inherit from a sibling item
            // with the same NexusUrl that already has a proper CustomName.
            // This covers the case where a second version of the same mod is
            // being installed and the async title fetch hasn't completed yet.
            QString nexusUrl = item->data(ModRole::NexusUrl).toString();
            bool inherited = false;
            if (!nexusUrl.isEmpty()) {
                for (int i = 0; i < m_modList->count(); ++i) {
                    auto *sib = m_modList->item(i);
                    if (sib == item) continue;
                    if (sib->data(ModRole::NexusUrl).toString() != nexusUrl) continue;
                    QString sibName = sib->data(ModRole::CustomName).toString().trimmed();
                    if (!sibName.isEmpty()) {
                        item->setData(ModRole::CustomName, sibName);
                        item->setText(sibName);
                        inherited = true;
                        break;
                    }
                    // Also try the sibling's NexusTitle.
                    QString sibTitle = sib->data(ModRole::NexusTitle).toString().trimmed();
                    if (!sibTitle.isEmpty()) {
                        item->setData(ModRole::CustomName, sibTitle);
                        item->setText(sibTitle);
                        inherited = true;
                        break;
                    }
                }
            }
            // Fall back to async fetch if no sibling had a usable name.
            if (!inherited && !nexusUrl.isEmpty()) {
                if (const auto ref = parseNexusModUrl(nexusUrl))
                    fetchNexusTitle(ref->game, ref->modId, item, /*setAsCustomName=*/true);
            }
        }
    }

    // Hard-coded renames for mods whose folder name is too terse or
    // misleading to be useful as a display name.  Table lives in
    // mod_naming::hardcodedRename.
    if (item->data(ModRole::CustomName).toString().isEmpty()) {
        const QString rename = mod_naming::hardcodedRename(fi.fileName());
        if (!rename.isEmpty()) {
            item->setData(ModRole::CustomName, rename);
            item->setText(rename);
        }
    }

    // If no custom name yet and the folder carries a Nexus version chain
    // (e.g. "Shishi - Redoran Outpost-57535-v1-1-1760726463"), strip the
    // trailing IDs/versions/timestamp and use the human-readable prefix.
    if (item->data(ModRole::CustomName).toString().isEmpty()) {
        const QString cleanName = mod_naming::stripTrailingVersionChain(fi.fileName());
        if (!cleanName.isEmpty()) {
            item->setData(ModRole::CustomName, cleanName);
            item->setText(cleanName);
        }
    }

    statusBar()->showMessage(T("status_mod_added").arg(fi.fileName()), 4000);

    // Remove any not-installed placeholder for the same mod that might have
    // been left behind - e.g. an MO2-imported name-only entry after the user
    // installed the mod via "Search on Nexus".  Match on NexusUrl (when set)
    // or CustomName/NexusTitle (case-insensitive) to catch the no-meta.ini case.
    // If the async title fetch hasn't arrived yet CustomName may still be empty;
    // a second pass runs in onTitleFetched to catch that race.
    purgeDuplicatePlaceholders(item);

    QElapsedTimer saveTimer;
    saveTimer.start();
    saveModList();         // reconciles m_loadOrder (adds the new plugins)
    qCDebug(logging::lcInstall) << "post-install saveModList ms=" << saveTimer.elapsed();
    // LOOT sorting is on-demand now - toolbar button "Sort with LOOT".
    updateModCount();
    scheduleConflictScan();

    QApplication::restoreOverrideCursor();   // done with the synchronous work

    // Post-install prompts (groundcover / splash / bundled-patch re-enable).
    // Each is self-guarding (no-op when not applicable); the "what counts as X"
    // detection lives in post_install:: so it's unit-testable.
    runGroundcoverHelper(item, fi.absoluteFilePath());
    runSplashScreenHelper(fi.absoluteFilePath());
    offerBundledPatchReenable(item);

    // User-perceived "grey freeze" on Add/Update lands here - the modal prompts
    // above (groundcover/splash/patches) are excluded only because
    // QMessageBox::exec() blocks; everything else is wall-clock UI-thread time.
    qCInfo(logging::lcModList).nospace()
        << "addModFromPath ms=" << addTimer.elapsed()
        << " name='" << QFileInfo(dirPath).fileName() << "'";
}

// Dispatcher for engine-specific config-file sync. Each supported game has its
// own conventions (openmw.cfg, Oblivion.ini plugin list, Skyrim's Plugins.txt…);
// this hook routes to the appropriate writer based on the active profile.
// Currently only Morrowind is wired - other games no-op so the user can still
// use the organizer for ordering/annotation without corrupting their config.
// Self-heal mods whose stored ModPath is an empty / wrong folder. The most
// common cause is an old FOMOD install where the wizard produced an empty
// "fomod_install" subdirectory but the real plugins live in sibling folders
// ("00 Core/OAAB_Data.esm" etc). Without this, the mod's scan returns no
// plugins and OpenMW fails with "Unable to find dependent file: …".
//
// If collectDataFolders() finds nothing at ModPath, try in order:
//   1. One level up - if the parent has plugins anywhere under it, rebind there.
//   2. A sibling in the mods dir sharing the same Nexus stem (e.g. empty
//      "OAAB_Data" -> "OAAB_Data-49042-2-5-…"). Happens when a reinstall
//      renames the sanitized folder but leaves the original extraction behind.
// Silent; the user just sees missing-master icons clear.
void MainWindow::repairEmptyModPaths()
{
    static const QStringList contentExts{".esp", ".esm", ".omwaddon", ".omwscripts"};

    const QString modsRootAbs = QFileInfo(m_modsDir).absoluteFilePath();

    // Sweep crash-leftover ".__deleting__" temp folders: removeFoldersAsync
    // renames a folder to "<name>.__deleting__" synchronously and byte-deletes
    // it on a worker; a hard kill mid-delete can leave the renamed husk behind.
    // Clean any such husks at startup (off-thread) so they don't accumulate.
    if (!modsRootAbs.isEmpty()) {
        const QStringList husks = QDir(modsRootAbs).entryList(
            {QStringLiteral("*.__deleting__*")}, QDir::Dirs | QDir::NoDotAndDotDot);
        if (!husks.isEmpty()) {
            QStringList abs;
            for (const QString &n : husks) abs << QDir(modsRootAbs).filePath(n);
            (void)QtConcurrent::run([abs]() {
                for (const QString &p : abs) QDir(p).removeRecursively();
            });
        }
    }

    // First, heal any damage a previous buggy run of this function did:
    // entries whose ModPath is the mods-root directory itself.  Rebinding
    // to the mods root made those items "own" the entire library - they'd
    // all report the same missing-master tooltips because they all scanned
    // every mod at once.  Treat them as broken placeholders: demote to
    // not-installed so the user can re-install them cleanly.
    int healed = 0;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        QString modPath = item->data(ModRole::ModPath).toString();
        if (modPath.isEmpty() || modsRootAbs.isEmpty()) continue;
        if (QFileInfo(modPath).absoluteFilePath() != modsRootAbs) continue;

        item->setData(ModRole::ModPath,       QString());
        item->setData(ModRole::InstallStatus, 0);
        item->setData(ModRole::HasMissingMaster, false);
        item->setData(ModRole::MissingMasters,   QStringList());
        item->setData(ModRole::ModSize,          QVariant());
        // Restore a plain display name (strip any "⠋ installing…" prefix).
        QString name = item->data(ModRole::CustomName).toString();
        if (name.isEmpty()) name = item->text();
        if (name.startsWith(QStringLiteral("⠋ "))) name = name.mid(2);
        if (!name.isEmpty()) item->setText(name);
        item->setToolTip(QString());
        ++healed;
    }

    // Build the set of ModPaths that are ALREADY in use, so rebinding never
    // creates duplicates below.
    QSet<QString> pathsInUse;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        QString p = item->data(ModRole::ModPath).toString();
        if (!p.isEmpty()) pathsInUse.insert(QFileInfo(p).absoluteFilePath());
    }

    bool changed = (healed > 0);
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        // InstallStatus 2 = currently installing - leave those alone. We also
        // handle status 0: loadModList() demotes a mod to "not installed" when
        // its ModPath exists but is empty (OAAB_Data reinstall-leftover case).
        // Without this branch the sibling rescue below never runs.
        if (item->data(ModRole::InstallStatus).toInt() == 2) continue;

        QString modPath = item->data(ModRole::ModPath).toString();
        if (modPath.isEmpty()) continue;
        if (!collectDataFolders(modPath, contentExts).isEmpty()) continue;
        // Resource-only mods (shaders, textures, sounds) have no plugins but
        // are correctly installed - don't try to rebind or demote them.
        if (!plugins::collectResourceFolders(modPath).isEmpty()) continue;

        QString rebind;

        // Strategy 1: rebind to parent if it contains plugins anywhere under
        // it (common for FOMOD installs where the real content sits one
        // directory up from the empty "fomod_install").
        QString parent = QFileInfo(modPath).absoluteDir().absolutePath();
        bool parentQualifies =
            !parent.isEmpty() && parent != modPath &&
            // Must be strictly inside the mods dir. Binding AT the root makes
            // this item "own" every mod in the library - that broke before.
            !modsRootAbs.isEmpty() && parent.startsWith(modsRootAbs + "/") &&
            !pathsInUse.contains(parent) &&
            !collectDataFolders(parent, contentExts).isEmpty();
        if (parentQualifies) rebind = parent;

        // Strategy 2: rebind to a sibling whose folder name shares the same
        // Nexus stem.  This catches the "reinstall left the old extraction
        // behind" case: modPath points at a freshly-created empty sanitized
        // folder, but the real plugins still live in the archive-named
        // sibling (e.g. "OAAB_Data" vs "OAAB_Data-49042-2-5-1-…").
        if (rebind.isEmpty()) {
            QString stem = nexusNameStem(QFileInfo(modPath).fileName());
            // Require a meaningfully long stem so we don't match stray
            // folders that happen to share one-letter prefixes.
            if (stem.size() >= 3 && !modsRootAbs.isEmpty() &&
                QFileInfo(modPath).absolutePath() == modsRootAbs) {
                QDir root(modsRootAbs);
                const QStringList siblings =
                    root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                QString best;
                int bestScore = 0;
                for (const QString &sib : siblings) {
                    if (sib == QFileInfo(modPath).fileName()) continue;
                    if (sib.contains(QStringLiteral(".__deleting__"))) continue;
                    if (nexusNameStem(sib) != stem)           continue;
                    QString abs = QDir(modsRootAbs).filePath(sib);
                    if (pathsInUse.contains(abs)) continue;
                    const auto folders = collectDataFolders(abs, contentExts);
                    if (folders.isEmpty()) continue;
                    int score = 0;
                    for (const auto &p : folders) score += p.second.size();
                    if (score > bestScore) {
                        bestScore = score;
                        best = abs;
                    }
                }
                if (!best.isEmpty()) rebind = best;
            }
        }

        // Strategy 3: sibling-by-modId rescue. modPath missing + parseable
        // NexusUrl -> scan modsRoot for a folder name containing "-<modId>-".
        // Catches cross-machine sync where each machine has a different version
        // of the same modId (Strategy 2 misses dive-into-single-subdir installs;
        // its absolutePath==modsRootAbs gate only fires at modsRoot). On hit:
        // rebind, flag UpdateAvailable, stash the original path in
        // IntendedModPath so saveModList keeps writing it (else machines ping-
        // pong on every sync).
        if (rebind.isEmpty()) {
            const QString nexusUrl = item->data(ModRole::NexusUrl).toString();
            static const QRegularExpression reModId(
                QStringLiteral(R"(/mods/(\d+)\b)"));
            const auto m = reModId.match(nexusUrl);
            if (m.hasMatch() && !modsRootAbs.isEmpty()) {
                const QString modIdTag =
                    QStringLiteral("-") + m.captured(1) + QStringLiteral("-");
                // Walk up to the child-of-modsRoot ancestor of modPath:
                // "/mods/X" -> "X", "/mods/X/Y/Z" -> "X". Keep the tail "Y/Z"
                // to re-apply under the chosen sibling, preserving a dive-into-
                // single-subdir install's deeper entry point.
                const QString absModPath =
                    QFileInfo(modPath).absoluteFilePath();
                QString topName;
                {
                    QString cur = absModPath;
                    while (!cur.isEmpty()) {
                        QString par = QFileInfo(cur).absolutePath();
                        if (par == modsRootAbs) {
                            topName = QFileInfo(cur).fileName();
                            break;
                        }
                        if (par == cur || par.isEmpty()) break;
                        cur = par;
                    }
                }
                if (!topName.isEmpty()) {
                    const QString wrapper =
                        QDir(modsRootAbs).filePath(topName);
                    const QString tail =
                        QDir(wrapper).relativeFilePath(absModPath);
                    QDir root(modsRootAbs);
                    const QStringList siblings =
                        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                    QString best;
                    int bestScore = 0;
                    for (const QString &sib : siblings) {
                        if (sib == topName)           continue;
                        if (sib.contains(QStringLiteral(".__deleting__"))) continue;
                        if (!sib.contains(modIdTag))  continue;
                        const QString sibAbs =
                            QDir(modsRootAbs).filePath(sib);
                        if (pathsInUse.contains(sibAbs)) continue;
                        // Try the tailed inner path first (matches the
                        // original dive-into-single-subdir install), fall
                        // back to the wrapper if the tail doesn't exist
                        // in the sibling.
                        QString candidate =
                            (tail.isEmpty() || tail == ".")
                                ? sibAbs
                                : QDir(sibAbs).filePath(tail);
                        auto folders =
                            collectDataFolders(candidate, contentExts);
                        if (folders.isEmpty() && candidate != sibAbs) {
                            candidate = sibAbs;
                            folders = collectDataFolders(candidate, contentExts);
                        }
                        if (folders.isEmpty()) continue;
                        int score = 0;
                        for (const auto &p : folders) score += p.second.size();
                        if (score > bestScore) {
                            bestScore = score;
                            best = candidate;
                        }
                    }
                    if (!best.isEmpty()) {
                        rebind = best;
                        item->setData(ModRole::IntendedModPath, modPath);
                        item->setData(ModRole::UpdateAvailable, true);
                    }
                }
            }
        }

        if (rebind.isEmpty()) continue;

        item->setData(ModRole::ModPath, rebind);
        item->setToolTip(rebind);
        // Also promote to "installed" so syncOpenMWConfig() actually writes
        // the data= and content= lines for this mod.  Without this flip an
        // item rebound from an empty folder stays at status=0 (set by
        // loadModList because the original folder was empty) and OpenMW
        // never sees the plugins even though ModPath is now correct.
        item->setData(ModRole::InstallStatus, 1);
        pathsInUse.insert(rebind);
        changed = true;
    }
    if (changed) saveModList();
    // Caller runs syncGameConfig() + scanMissingMasters() right after us, so
    // the rescued data= / content= lines land in openmw.cfg on this same
    // launch without needing an extra refresh here.
}

// Pull the Nexus mod ID out of a Nexus archive filename.  The format Nexus
// serves is consistently "<Title>-<modId>-<v>-<v>-<timestamp>.{zip,7z,rar}"
// (e.g. "Fonts-46854-1-0-1559397215.zip" → 46854).  Returns -1 if the name
// doesn't match the shape, so user-renamed archives fall through to the
// "new placeholder" path instead of accidentally hijacking a row.
static int modIdFromArchiveName(const QString &archiveFileName)
{
    const QString base = QFileInfo(archiveFileName).completeBaseName();
    const QStringList parts = base.split('-');
    if (parts.size() < 5) return -1;  // need <name>-<id>-<v>-<v>-<ts> minimum
    bool ok = false;
    const int candidate = parts[1].toInt(&ok);
    return ok ? candidate : -1;
}

// Look up a "pending" row that's already waiting for this archive.  A row
// qualifies if it's not-yet-installed (status 0 or 2), has a Nexus URL with
// the same mod ID as the archive filename, and belongs to the current game
// profile.  Used by installLocalArchive to route a manual drag-drop back to
// the row the user clicked Install on (or that came down from a synced
// modlist), instead of creating a duplicate entry.
static QListWidgetItem *findPendingRowForModId(QListWidget *list, int modId,
                                                const QString &gameId)
{
    if (!list || modId <= 0) return nullptr;
    const QString gameLc = gameId.toLower();
    for (int i = 0; i < list->count(); ++i) {
        auto *it = list->item(i);
        if (!it) continue;
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        const int status = it->data(ModRole::InstallStatus).toInt();
        if (status == 1) continue;                // already installed - don't clobber
        const QString url = it->data(ModRole::NexusUrl).toString();
        if (url.isEmpty()) continue;
        const auto ref = parseNexusModUrl(url);
        if (!ref) continue;
        if (!gameLc.isEmpty() && ref->game != gameLc) continue;
        if (ref->modId == modId) return it;
    }
    return nullptr;
}

void MainWindow::installLocalArchive(const QString &archivePath)
{
    QFileInfo srcFi(archivePath);
    if (!srcFi.exists() || !srcFi.isFile()) return;

    // extractAndAdd deletes the archive after success.  To avoid nuking the
    // user's source file, copy it into m_modsDir under a non-colliding name
    // and hand that copy off.  If the source already IS inside m_modsDir
    // (e.g. user downloaded manually and dropped from there), we use it in
    // place - same net effect.
    QString workingArchive;
    QString modsRootAbs = QFileInfo(m_modsDir).absoluteFilePath();
    if (srcFi.absoluteFilePath().startsWith(modsRootAbs + "/")) {
        workingArchive = srcFi.absoluteFilePath();
    } else {
        QDir().mkpath(m_modsDir);
        QString candidate = QDir(m_modsDir).filePath(srcFi.fileName());
        int suffix = 1;
        while (QFileInfo::exists(candidate)) {
            // Keep the extension only when there is one - an extensionless
            // drop (a bare CDN token) would otherwise become "name_2." with a
            // dangling dot.
            const QString ext = srcFi.suffix();
            const QString rebuilt = ext.isEmpty()
                ? QString("%1_%2").arg(srcFi.completeBaseName()).arg(++suffix)
                : QString("%1_%2.%3").arg(srcFi.completeBaseName())
                                     .arg(++suffix).arg(ext);
            candidate = QDir(m_modsDir).filePath(rebuilt);
        }
        if (!QFile::copy(srcFi.absoluteFilePath(), candidate)) {
            ui::warn(this, T("file_error_title"), T("file_error_write").arg(candidate));
            return;
        }
        workingArchive = candidate;
    }

    // Match against an existing row when the archive filename embeds a Nexus
    // mod ID.  This is how manual-download mods (e.g. True Type Fonts for
    // OpenMW) rejoin the row that was waiting for them - the user right-
    // clicks Install, sees the "manual download required" dialog, downloads
    // from Nexus, drags the archive onto Nerevarine, and the waiting row
    // gets adopted as the placeholder instead of a stranger duplicate
    // appearing at the bottom of the list.
    const QString gameId = m_profiles->isEmpty() ? QString() : currentProfile().id;
    const int archiveModId = modIdFromArchiveName(srcFi.fileName());
    QListWidgetItem *placeholder =
        findPendingRowForModId(m_modList, archiveModId, gameId);

    if (placeholder) {
        // Adopt the existing row.  Switch it into "installing" state so the
        // delegate paints it with the spinner, preserve CustomName and URL.
        QString existingName = placeholder->data(ModRole::CustomName).toString();
        if (existingName.isEmpty()) existingName = placeholder->text();
        placeholder->setText(QString("⠋ %1 (%2)").arg(
            T("status_installing_label"), existingName));
        placeholder->setData(ModRole::InstallStatus, 2);
        placeholder_state::setBusyFlags(placeholder);
        m_modList->scrollToItem(placeholder);
    } else {
        // No matching row - fresh local install, build a placeholder from
        // the archive's base name as we always did.
        placeholder = new QListWidgetItem(
            QString("⠋ %1 (%2)").arg(T("status_installing_label"),
                                      srcFi.completeBaseName()));
        placeholder->setData(ModRole::ItemType,      ItemType::Mod);
        placeholder->setData(ModRole::InstallStatus, 2);
        placeholder->setData(ModRole::DateAdded,     QDateTime::currentDateTime());
        placeholder->setCheckState(Qt::Checked);
        placeholder_state::setBusyFlags(placeholder);
        m_modList->addItem(placeholder);
        m_modList->scrollToItem(placeholder);
    }
    saveModList();

    statusBar()->showMessage(
        T("status_downloaded_extracting").arg(srcFi.fileName()), 4000);
    if (const auto r = extractAndAdd(workingArchive, placeholder); !r) {
        qCWarning(logging::lcInstall)
            << "extractAndAdd for dropped/reinstalled archive failed:" << r.error();
    }
}
