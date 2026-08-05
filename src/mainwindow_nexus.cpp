// mainwindow_nexus - Nexus API key, nxm:// handling, download-queue wiring.
// Split out of mainwindow.cpp for parallel compilation; same class, own TU.

#include "mainwindow.h"
#include "mainwindow_internal.h"

#ifdef HAVE_QTKEYCHAIN
#  include <qt6keychain/keychain.h>
#endif

// API-key storage constants (used only by the keychain-backed API-key slots).
static constexpr const char *kKeychainService = "NerevarineOrganizer";
static constexpr const char *kKeychainKey     = "nexus_api_key";
#include "settings.h"
#include "separatordialog.h"
#include "modroles.h"
#include "translator.h"
#include "placeholder_state.h"
#include "fomodwizard.h"
#include "bainwizard.h"
#include "install_layout.h"
#include "modlist_model.h"
#include "modlist_model_widget_bridge.h"
#include "modlist_summary_dialog.h"
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
#include "conflict_inspector.h"
#include "report_dialog.h"
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
#include "firstrunwizard.h"
#include "fs_utils.h"
#include "pluginparser.h"
#include "log_triage_dialog.h"
#include <QFutureWatcher>
using plugins::collectDataFolders;
using plugins::readTes3Masters;
#include <QDropEvent>
#include <QMimeData>
using fsutils::sanitizeFolderName;
#include "mainwindow_internal.h"

void MainWindow::checkNxmHandlerRegistration()
{
    // KDE/KIO needs a per-scheme .protocol file whose `exec=` points at the
    // current binary. If the binary moves (rebuild path, packaging, Flatpak
    // update) a stale `exec=` makes KIO fail "Unknown protocol: nxms" though the
    // file is present. So every launch: rebuild expected content, compare byte-
    // for-byte, rewrite+rebuild-sycoca only on a diff. DO NOT re-add an early-
    // return that skips re-checking exec path - that broke this before.
    // Under AppImage, applicationFilePath() is the per-launch /tmp/.mount_*
    // path, gone by the next nxm:// click. The runtime exports APPIMAGE pointing
    // at the stable .AppImage file - use that so the handler survives launches.
    QString execPath = qEnvironmentVariable("APPIMAGE");
    if (execPath.isEmpty())
        execPath = QCoreApplication::applicationFilePath();

    // Write to both kservices5 and kservices6 - deployments pick one or the
    // other and writing both is harmless.
    const QStringList kserviceDirs = {
        QDir::homePath() + "/.local/share/kservices5",
        QDir::homePath() + "/.local/share/kservices6",
        QDir::homePath() + "/.local/share/kio/protocols",
    };
    auto protoContent = [&](const QString &scheme) {
        return QString(
            "[Protocol]\n"
            "exec=%1 %u\n"
            "protocol=%2\n"
            "input=none\n"
            "output=none\n"
            "helper=true\n"
            "listing=false\n"
            "reading=false\n"
            "writing=false\n"
            "makedir=false\n"
            "deleting=false\n").arg(execPath, scheme);
    };

    // Read a file into a QByteArray, returning an empty array on failure.
    auto readFile = [](const QString &path) -> QByteArray {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return {};
        return f.readAll();
    };
    auto writeFile = [](const QString &path, const QByteArray &data) -> bool {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        return f.write(data) == data.size();
    };

    bool changed = false;
    for (const QString &scheme : {QStringLiteral("nxm"), QStringLiteral("nxms")}) {
        QByteArray wantBytes = protoContent(scheme).toUtf8();
        for (const QString &dir : kserviceDirs) {
            QString path = dir + "/" + scheme + ".protocol";
            if (readFile(path) != wantBytes) {
                if (writeFile(path, wantBytes)) changed = true;
            }
        }
    }

    // Refresh the .desktop file the same way (checks MimeType includes BOTH
    // nxm and nxms, and that Exec matches the current binary).
    QString appDir = QStandardPaths::writableLocation(
                         QStandardPaths::ApplicationsLocation);
    QString desktopPath = appDir + "/nerevarine_organizer.desktop";
    QByteArray wantDesktop = QString(
        "[Desktop Entry]\n"
        "Name=Nerevarine Organizer\n"
        "Comment=Mod manager for Morrowind / OpenMW\n"
        "Exec=%1 %u\n"
        "Icon=nerevarine_organizer\n"
        "Type=Application\n"
        "Categories=Game;Utility;\n"
        "MimeType=x-scheme-handler/nxm;x-scheme-handler/nxms;\n"
        "StartupWMClass=nerevarine_organizer\n").arg(execPath).toUtf8();

    if (readFile(desktopPath) != wantDesktop) {
        if (!writeFile(desktopPath, wantDesktop)) {
            ui::warn(this, T("registration_failed_title"), T("registration_failed_body").arg(desktopPath));
            return;
        }
        changed = true;
    }

    if (!changed) return; // everything already correct - skip the expensive
                          // xdg-mime / sycoca rebuild

    // -- Re-register the mime type association and refresh the caches ---
    QProcess reg;
    subprocess::applyEnv(reg);
    reg.start("xdg-mime",
        {"default", "nerevarine_organizer.desktop", "x-scheme-handler/nxm"});
    reg.waitForFinished(3000);
    reg.start("xdg-mime",
        {"default", "nerevarine_organizer.desktop", "x-scheme-handler/nxms"});
    reg.waitForFinished(3000);

    subprocess::execute("update-desktop-database", {appDir});

    // Rebuild both sycoca versions (KF5/KF6 coexist mid-transition). Silent
    // failure - missing tool means that KDE version isn't installed here.
    subprocess::execute("kbuildsycoca6", {"--noincremental"});
    subprocess::execute("kbuildsycoca5", {"--noincremental"});

    statusBar()->showMessage(T("status_registered_nxm"), 4000);
}

void MainWindow::handleNxmUrl(const QString &url)
{
    auto parsed = parseNxmUrl(url);
    if (!parsed) {
        // Map machine-readable errors back onto the existing i18n bodies.
        // invalid-scheme and invalid-path share the "bad URL" message;
        // invalid-ids gets the dedicated "bad ID" message.
        const QString &reason = parsed.error();
        const QString body = (reason == QStringLiteral("invalid-ids"))
            ? T("nxm_invalid_ids_body").arg(url)
            : T("nxm_invalid_url_body").arg(url);
        ui::warn(this, T("nxm_invalid_url_title"), body);
        return;
    }
    const QString  game    = parsed->game;
    const int      modId   = parsed->modId;
    const int      fileId  = parsed->fileId;
    const QString &key     = parsed->key;
    const QString &expires = parsed->expires;

    // Forbidden mod check - hard block, no install-anyway escape.
    if (!confirmNotForbidden(game, modId)) return;

    // Already-installed guard. Replace: reuse the row (deletes the prior folder
    // via PrevModPath after install). Separate: fresh placeholder, new download
    // lands beside the existing entry. Merge: reuse the row but overlay new
    // files onto the existing folder (MergeTargetPath) instead of deleting.
    const auto reinstallChoice = confirmReinstallIfInstalled(game, modId);
    if (reinstallChoice == ReinstallChoice::Cancel) return;
    const bool forceSeparate = (reinstallChoice == ReinstallChoice::Separate);
    const bool forceMerge    = (reinstallChoice == ReinstallChoice::Merge);

    if (m_apiKey.isEmpty()) {
        ui::info(this, T("nxm_api_key_required_title"), T("nxm_api_key_required_body"));
        onSetApiKey();
        if (m_apiKey.isEmpty()) return;
    }

    raise();
    activateWindow();
    statusBar()->showMessage(
        T("status_fetching_download").arg(modId).arg(fileId));

    // Reuse an existing row for this mod to avoid a duplicate. Match by
    // game + modId from the stored NexusUrl. Pending placeholders (status=0,
    // e.g. MO2 import or an unfinished download) always qualify - same in-flight
    // slot. Installed rows (status=1) qualify only on Replace; Separate forces a
    // fresh placeholder so the download lands beside the existing entry.
    QString nexusPageUrl = nexusModUrl(game, modId);
    QListWidgetItem *placeholder = nullptr;
    QListWidgetItem *installedMatch = nullptr;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        const int status = it->data(ModRole::InstallStatus).toInt();
        if (status != 0 && status != 1) continue;
        const QString storedUrl = it->data(ModRole::NexusUrl).toString();
        if (storedUrl.isEmpty()) continue;
        const auto ref = parseNexusModUrl(storedUrl);
        if (!ref || ref->modId != modId) continue;
        if (ref->game.compare(game, Qt::CaseInsensitive) != 0) continue;
        if (status == 0) { placeholder = it; break; }
        if (!installedMatch) installedMatch = it;
    }
    if (!placeholder && !forceSeparate) placeholder = installedMatch;
    // Merge always targets the installed row; a pending download placeholder
    // for the same modId (if any) has nothing to merge into.
    if (forceMerge && installedMatch) placeholder = installedMatch;

    if (placeholder) {
        // For an installed match, decide what happens to the current folder.
        if (placeholder == installedMatch) {
            const QString currentPath = placeholder->data(ModRole::ModPath).toString();
            if (forceMerge) {
                // Merge: keep the folder, overlay new files (last-writer-wins).
                // applyPendingMerge consumes this once the archive extracts.
                if (!currentPath.isEmpty())
                    placeholder->setData(ModRole::MergeTargetPath, currentPath);
            } else if (!currentPath.isEmpty()
                       && placeholder->data(ModRole::PrevModPath).toString().isEmpty()) {
                // Replace: stash the folder so addModFromPath can purge it once
                // the install lands. Skip if a prev-path is already stashed
                // (rare double-fire) so we don't lose the original.
                placeholder->setData(ModRole::PrevModPath, currentPath);
            }
        }
        placeholder->setText(QString("⠋ %1 (mod %2)").arg(T("status_installing_label")).arg(modId));
        placeholder->setData(ModRole::InstallStatus, 2);
        placeholder_state::setBusyFlags(placeholder);
    } else {
        placeholder = new QListWidgetItem(
            QString("⠋ %1 (mod %2)").arg(T("status_installing_label")).arg(modId));
        placeholder->setData(ModRole::ItemType,      ItemType::Mod);
        placeholder->setData(ModRole::InstallStatus, 2);
        placeholder->setData(ModRole::NexusUrl,      nexusPageUrl);
        placeholder->setData(ModRole::DateAdded,     QDateTime::currentDateTime());
        placeholder->setCheckState(Qt::Checked);
        placeholder_state::setBusyFlags(placeholder);
        m_modList->addItem(placeholder);
    }
    m_modList->scrollToItem(placeholder);
    saveModList(); // persist URL immediately so it survives a crash or cancel

    // Title fetch in parallel - arrives before the download finishes; the FOMOD
    // path uses it to name the installed folder.
    fetchNexusTitle(game, modId, placeholder);

    // Pull md5 + size_in_bytes from files.json for post-download verification.
    // Runs alongside fetchDownloadLink; only consulted at finish-time.
    m_nexusCtl->fetchExpectedChecksum(placeholder, game, modId, fileId);

    m_downloadQueue->fetchDownloadLink(game, modId, fileId, key, expires, placeholder);
}

void MainWindow::setupDownloadQueue()
{
    m_downloadQueue = new DownloadQueue(m_modList, m_net, m_nexus, this, this);

    // Adapter lambda: verifyAndExtract is [[nodiscard]], so the PMF connect form
    // would warn. It already logs + shows a status on failure, so discard here.
    connect(m_downloadQueue, &DownloadQueue::extractionRequested, this,
            [this](const QString &archivePath, QListWidgetItem *placeholder) {
                (void)verifyAndExtract(archivePath, placeholder);
            });
    connect(m_downloadQueue, &DownloadQueue::saveRequested,
            this, &MainWindow::saveModList);
    connect(m_downloadQueue, &DownloadQueue::statusMessage,
            this, [this](const QString &msg, int t){
                statusBar()->showMessage(msg, t);
            });
    // 401 on a download-link request means the apikey is bad/expired (not a
    // premium wall). Offer to open the key dialog instead of the "need Premium"
    // loop the user would otherwise be stuck in.
    connect(m_downloadQueue, &DownloadQueue::apiKeyRejected, this, [this]{
        if (ui::confirm(this, T("api_key_invalid_title"), T("api_key_invalid_body")))
            onSetApiKey();
    });

    m_downloadQueue->setModsDir(m_modsDir);
    // Persist download-integrity diagnostics to a writable file so a corrupt-
    // download recurrence is inspectable (qWarning only hits stderr, which an
    // AppImage/desktop launch discards).
    m_downloadQueue->setDiagLogPath(
        resolveUserStatePath(QStringLiteral("download_diag.log")));

    QAction *toggleAct = m_downloadQueue->setup(this);

    // Insert the visibility toggle into the Settings menu.
    if (menuBar()) {
        QMenu *settingsMenu = nullptr;
        for (QAction *a : menuBar()->actions()) {
            if (a->menu() && a->text().contains("ettings")) {
                settingsMenu = a->menu();
                break;
            }
        }
        if (settingsMenu) {
            settingsMenu->addSeparator();
            settingsMenu->addAction(toggleAct);
        }
    }
}

void MainWindow::fetchNexusTitle(const QString &game, int modId, QListWidgetItem *item,
                                  bool setAsCustomName)
{
    // The "also promote the name to CustomName / item text" flag is a
    // UI-policy decision at call time.  Record the intent for this item
    // and let the controller emit back with just (item, name); the
    // titleFetched slot consults the set.
    if (setAsCustomName)
        m_titleSetsCustomName.insert(item);
    m_nexusCtl->fetchModTitle(item, game, modId);
}

void MainWindow::onDependenciesScanned(QListWidgetItem *item,
                                       const QString &game, int modId,
                                       const QString &title,
                                       const QStringList &presentDeps,
                                       const QList<int> &missing)
{
    // Cache the Nexus mod-page title for later use (e.g. naming a FOMOD
    // install's output folder something meaningful instead of "fomod_install").
    if (!title.isEmpty())
        item->setData(ModRole::NexusTitle, title);

    // Record which installed mods this one depends on.
    if (!presentDeps.isEmpty())
        item->setData(ModRole::DependsOn, presentDeps);

    statusBar()->clearMessage();

    if (missing.isEmpty()) {
        fetchModFiles(game, modId, item);
        return;
    }

    // Custom dialog: one row per missing mod with a human-readable name + a
    // "Visit" button.  The previous QMessageBox::setDetailedText approach
    // dumped raw URLs, which forced the user to copy-paste each one into a
    // browser to find out what was being flagged.
    QDialog box(this);
    box.setWindowTitle(T("deps_warn_title"));
    box.setMinimumWidth(520);
    auto *v = new QVBoxLayout(&box);

    auto *header = new QLabel(T("deps_warn_body").arg(missing.size()), &box);
    header->setWordWrap(true);
    v->addWidget(header);

    // Rows go into a scroll area so long lists don't overflow the screen.
    auto *scrollContainer = new QWidget;
    auto *scrollLayout    = new QVBoxLayout(scrollContainer);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->setSpacing(2);

    for (int id : missing) {
        const QString depUrl = nexusModUrl(game, id);

        auto *row = new QWidget(scrollContainer);
        auto *h   = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);

        auto *nameLbl = new QLabel(T("deps_warn_loading_name").arg(id), row);
        nameLbl->setToolTip(depUrl);
        nameLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        h->addWidget(nameLbl, 1);

        auto *visitBtn = new QPushButton(T("deps_warn_visit"), row);
        connect(visitBtn, &QPushButton::clicked, this,
                [depUrl] { QDesktopServices::openUrl(QUrl(depUrl)); });
        h->addWidget(visitBtn);

        scrollLayout->addWidget(row);

        // Fetch the mod's Nexus "name" so the label becomes readable.
        // QPointer guards against callbacks firing after the dialog is
        // dismissed - the label would otherwise be a dangling pointer.
        // This one stays a direct NexusClient call: the target is a QLabel,
        // not a QListWidgetItem, so the controller's item-keyed signals
        // don't fit.  Acceptable because the dialog's lifetime is bounded
        // and QPointer already handles the dangling case.
        QPointer<QLabel> safeLbl(nameLbl);
        QNetworkReply *nrep = m_nexus->requestModInfo(game, id);
        connect(nrep, &QNetworkReply::finished, this,
                [nrep, safeLbl, id, depUrl]() {
            nrep->deleteLater();
            if (!safeLbl) return;
            QString name;
            if (nrep->error() == QNetworkReply::NoError) {
                const auto info = NexusClient::parseModInfo(nrep->readAll());
                if (info) name = info->name;
            }
            if (name.isEmpty())
                name = QString("Mod #%1").arg(id);
            safeLbl->setText(name);
            safeLbl->setToolTip(depUrl);
        });
    }
    scrollLayout->addStretch();

    auto *scrollArea = new QScrollArea(&box);
    scrollArea->setWidget(scrollContainer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::StyledPanel);
    scrollArea->setMaximumHeight(320);
    v->addWidget(scrollArea, 1);

    auto *btns = new QDialogButtonBox(&box);
    btns->addButton(T("deps_install_anyway"), QDialogButtonBox::AcceptRole);
    btns->addButton(QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    v->addWidget(btns);

    const bool install = (box.exec() == QDialog::Accepted);
    if (install)
        fetchModFiles(game, modId, item);
}

void MainWindow::onFileListFetched(QListWidgetItem *item,
                                   const QString &game, int modId,
                                   const QList<NexusClient::FileEntry> &allFiles)
{
    const bool autoPickMain = m_autoPickMainItems.remove(item);

    // Prefer MAIN + UPDATE files; fall back to everything if none found.
    QList<NexusClient::FileEntry> files;
    for (const auto &f : allFiles)
        if (f.category == "MAIN" || f.category == "UPDATE") files.append(f);
    if (files.isEmpty()) files = allFiles;

    if (files.isEmpty()) {
        ui::info(this, T("install_pick_file_title"), T("install_no_files"));
        statusBar()->clearMessage();
        return;
    }

    auto stashChecksum = [](QListWidgetItem *ph, const NexusClient::FileEntry &f) {
        if (!f.name.isEmpty()) ph->setData(ModRole::NexusFileName, f.name);
        if (!f.md5.isEmpty())  ph->setData(ModRole::ExpectedMd5,  f.md5);
        if (f.sizeBytes > 0)   ph->setData(ModRole::ExpectedSize, f.sizeBytes);
    };

    // Engine-aware default: Nexus mods often ship parallel MWSE/MGE XE and
    // OpenMW variants of the same file.  When the active profile is OpenMW
    // (our "morrowind" profile id), prefer files whose name advertises
    // OpenMW compatibility and avoid ones that advertise MWSE/MGE XE, so
    // the picker and "Update All" don't quietly pick an MWSE build for an
    // OpenMW user.  Non-morrowind profiles score everything equally.
    auto scoreForProfile = [this](const NexusClient::FileEntry &f) -> int {
        if (m_profiles->isEmpty() || currentProfile().id != "morrowind") return 0;
        const QString n = f.name.toLower();
        const bool openmw = n.contains("openmw");
        const bool mwse   = n.contains("mwse");
        const bool mge    = n.contains("mge xe") || n.contains("mgexe");
        if (openmw && !mwse && !mge) return 2;   // clearly OpenMW
        if (mwse || mge)             return -1;  // clearly MWSE/MGE → deprioritize
        return 1;                                // engine-neutral
    };

    int bestIdx = 0;
    int bestScore = scoreForProfile(files.first());
    for (int i = 1; i < files.size(); ++i) {
        int s = scoreForProfile(files[i]);
        if (s > bestScore) { bestScore = s; bestIdx = i; }
    }

    // Dependency-variant recommendation.  Some Morrowind mods ship parallel
    // MAIN files for "OAAB" (uses OAAB Data assets) vs "No OAAB" - e.g. Sixth
    // House Minor Bases Refit.  When the picker holds such a pair, recommend
    // and default-select the build matching the modlist: the OAAB build when
    // OAAB Data (Nexus mod 49042) is installed, else the No-OAAB build - while
    // leaving the other freely selectable.  Folded into bestIdx so the
    // no-picker / "Update All" path auto-picks the right variant too.
    int     recommendIdx = -1;
    QString recommendNote;
    {
        using install_layout::OaabVariant;
        QList<OaabVariant> variants;
        variants.reserve(files.size());
        bool hasOaab = false, hasNoOaab = false;
        for (const auto &f : files) {
            const OaabVariant v = install_layout::classifyOaabVariant(f.name);
            variants.append(v);
            if (v == OaabVariant::NoOaab)         hasNoOaab = true;
            else if (v == OaabVariant::NeedsOaab) hasOaab   = true;
        }
        if (hasOaab && hasNoOaab) {
            const bool oaabPresent =
                m_model && m_model->findInstalledByModId(game, 49042) >= 0;
            const OaabVariant want =
                oaabPresent ? OaabVariant::NeedsOaab : OaabVariant::NoOaab;
            // Among the wanted variant, keep the engine-best file (handles a
            // mod that also forks OpenMW/MWSE builds within each variant).
            for (int i = 0; i < files.size(); ++i) {
                if (variants[i] != want) continue;
                if (recommendIdx < 0
                    || scoreForProfile(files[i]) > scoreForProfile(files[recommendIdx]))
                    recommendIdx = i;
            }
            if (recommendIdx >= 0) {
                bestIdx = recommendIdx;
                recommendNote = QStringLiteral("  ")
                    + (oaabPresent ? T("install_pick_recommended_oaab")
                                   : T("install_pick_recommended_no_oaab"));
            }
        }
    }

    // Single main file - skip the picker.  Batch-update flow passes
    // autoPickMain=true to skip the picker even with multiple files, since
    // iterating 20 pickers for "Update All" is worse UX than just taking
    // the first MAIN/UPDATE entry (which is what the user almost always
    // picks manually anyway).  In either no-picker case, use the engine-
    // scored best match rather than files.first().
    if (files.size() == 1 || autoPickMain) {
        const auto &f = files.at(bestIdx);
        prepareItemForInstall(item);
        stashChecksum(item, f);
        autoLinkSameModpage(item, f.category);
        m_downloadQueue->fetchDownloadLink(game, modId, f.fileId, "", "", item);
        return;
    }

    // Multiple files - show picker dialog.
    QDialog dlg(this);
    dlg.setWindowTitle(T("install_pick_file_title"));
    dlg.setMinimumWidth(540);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(T("install_pick_file_label")));

    auto *fileList = new QListWidget(&dlg);
    for (int i = 0; i < files.size(); ++i) {
        const auto &f = files[i];
        const double mb = f.sizeKb / 1024.0;
        QString label = QString("%1  [v%2]  %3  -  %4 MB")
                            .arg(f.name, f.version, f.category)
                            .arg(mb, 0, 'f', 1);
        if (i == recommendIdx) label += recommendNote;
        auto *li = new QListWidgetItem(label, fileList);
        li->setData(Qt::UserRole,       f.fileId);
        li->setData(Qt::UserRole + 1,   f.md5);
        li->setData(Qt::UserRole + 2,   f.sizeBytes);
        li->setData(Qt::UserRole + 3,   f.category);
    }
    fileList->setCurrentRow(bestIdx);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(fileList, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    layout->addWidget(fileList);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) {
        statusBar()->clearMessage();
        return;
    }

    auto *sel = fileList->currentItem();
    if (!sel) return;
    const int     fileId = sel->data(Qt::UserRole).toInt();
    const QString md5    = sel->data(Qt::UserRole + 1).toString();
    const qint64  sz     = sel->data(Qt::UserRole + 2).toLongLong();
    const QString cat    = sel->data(Qt::UserRole + 3).toString();
    prepareItemForInstall(item);
    if (!md5.isEmpty()) item->setData(ModRole::ExpectedMd5,  md5);
    if (sz > 0)         item->setData(ModRole::ExpectedSize, sz);
    autoLinkSameModpage(item, cat);
    m_downloadQueue->fetchDownloadLink(game, modId, fileId, "", "", item);
}

void MainWindow::onSetApiKey()
{
    bool ok;
    const QString raw = QInputDialog::getText(
        this, T("api_key_dialog_title"), T("api_key_dialog_prompt"),
        QLineEdit::Password, m_apiKey, &ok);
    if (!ok) return;   // Cancel: leave the stored key untouched.

    const QString key = raw.trimmed();
    if (key.isEmpty()) {
        // Empty field + OK == delete. (This used to be silently ignored, so the
        // key could never be cleared - it resurrected on every reopen, forcing
        // users to overwrite it with gibberish.)
        if (m_apiKey.isEmpty()) return;   // nothing to remove
        if (!ui::confirm(this, T("api_key_delete_title"), T("api_key_delete_confirm")))
            return;
        m_apiKey.clear();
        deleteApiKey();
        if (m_nexus) m_nexus->setApiKey(QString());
        statusBar()->showMessage(T("status_api_key_removed"), 3000);
        return;
    }

    m_apiKey = key;
    saveApiKey(m_apiKey);
    if (m_nexus) m_nexus->setApiKey(m_apiKey);
    statusBar()->showMessage(T("status_api_key_saved"), 3000);
    validateApiKeyAndReport();   // confirm Nexus accepts it and report the tier
}

void MainWindow::loadApiKey()
{
#ifdef HAVE_QTKEYCHAIN
    auto *job = new QKeychain::ReadPasswordJob(kKeychainService, this);
    job->setKey(kKeychainKey);
    // Synchronous-ish: we wait via a local event loop so m_apiKey is
    // populated before the first Nexus request could use it.  The read is
    // fast (keyring daemons are local IPC) - typically a few ms.
    QEventLoop loop;
    connect(job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job->start();
    loop.exec();

    if (job->error() == QKeychain::NoError) {
        m_apiKey = job->textData();
    } else if (job->error() == QKeychain::EntryNotFound) {
        // Migrate from old plain-text QSettings storage, if present.
        QString legacy = Settings::nexusApiKey();
        if (!legacy.isEmpty()) {
            m_apiKey = legacy;
            saveApiKey(legacy);          // writes to keychain
            Settings::removeNexusApiKey(); // then scrub plain-text copy
        }
    } else {
        // Backend error (no available service, user denied access, …).
        // Fall back to QSettings so the app still works this session.
        qCWarning(logging::lcApp, "Keychain read failed: %s",
                  qUtf8Printable(job->errorString()));
        m_apiKey = Settings::nexusApiKey();
    }
    job->deleteLater();
#else
    m_apiKey = Settings::nexusApiKey();
#endif
}

void MainWindow::saveApiKey(const QString &key)
{
#ifdef HAVE_QTKEYCHAIN
    auto *job = new QKeychain::WritePasswordJob(kKeychainService, this);
    job->setKey(kKeychainKey);
    job->setTextData(key);
    // Fire-and-forget; the write is asynchronous but idempotent.
    // If it fails we log and fall back to QSettings so the key isn't lost.
    connect(job, &QKeychain::Job::finished, this, [job, key]{
        if (job->error() != QKeychain::NoError) {
            qCWarning(logging::lcApp, "Keychain write failed: %s",
                      qUtf8Printable(job->errorString()));
            Settings::setNexusApiKey(key);
        }
        job->deleteLater();
    });
    job->start();
    // Scrub any stale plain-text copy from QSettings immediately.
    Settings::removeNexusApiKey();
#else
    Settings::setNexusApiKey(key);
#endif
}

void MainWindow::deleteApiKey()
{
#ifdef HAVE_QTKEYCHAIN
    auto *job = new QKeychain::DeletePasswordJob(kKeychainService, this);
    job->setKey(kKeychainKey);
    connect(job, &QKeychain::Job::finished, this, [job]{
        // EntryNotFound just means there was nothing stored - not a failure.
        if (job->error() != QKeychain::NoError
            && job->error() != QKeychain::EntryNotFound) {
            qCWarning(logging::lcApp, "Keychain delete failed: %s",
                      qUtf8Printable(job->errorString()));
        }
        job->deleteLater();
    });
    job->start();
#endif
    // Always scrub the plain-text / legacy copy too: loadApiKey() re-migrates a
    // leftover QSettings value into the keychain on the next launch, which would
    // resurrect the key we just deleted.
    Settings::removeNexusApiKey();
}

// Confirm the saved key is accepted and surface the account tier. Free accounts
// are fine - they just have to use the website "Mod Manager Download" button -
// so a free result is reported, not warned about. A direct NexusClient call
// (not via NexusController): validate-user is account-global, with no
// QListWidgetItem to key the controller's item-scoped signals on. Same
// precedent as the dependency title-fetch above.
void MainWindow::validateApiKeyAndReport()
{
    if (!m_nexus || m_apiKey.isEmpty()) return;
    QNetworkReply *reply = m_nexus->requestValidateUser();
    connect(reply, &QNetworkReply::finished, this, [this, reply]{
        reply->deleteLater();
        const int http =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            if (http == 401)
                ui::warn(this, T("api_key_invalid_title"), T("api_key_invalid_body"));
            else   // offline / transient: the key is saved, don't alarm
                statusBar()->showMessage(T("status_api_key_unverified"), 4000);
            return;
        }
        const auto u = NexusClient::parseValidateUser(reply->readAll());
        if (!u) {
            statusBar()->showMessage(T("status_api_key_unverified"), 4000);
            return;
        }
        statusBar()->showMessage(u->isPremium ? T("status_api_key_verified_premium")
                                              : T("status_api_key_verified_free"),
                                 6000);
    });
}
