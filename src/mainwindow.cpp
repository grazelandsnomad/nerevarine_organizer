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
#ifdef HAVE_QTKEYCHAIN
#  include <qt6keychain/keychain.h>
#endif
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
// From src/pluginparser.cpp.
using plugins::collectDataFolders;
using plugins::readTes3Masters;

static QString     detectLootBinary();

// By value so the wizard doesn't need BuiltinGameDef visible here.
static QList<firstrun::GameChoice> builtinGameChoices();
#include <QDropEvent>
#include <QMimeData>

// Drop onto a separator lands just after it (mod becomes the section's first
// entry) instead of Qt's above/below-by-y default.

// .wabbajack and MO2 modlist.txt go to the list-import flow, not archive-install.


// ConflictScanner lives in loadordercontroller.{h,cpp}. MainWindow keeps the
// debounce timer, the snapshot slot feeding it, and onConflictsScanned (writes
// roles back).

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(T("window_title"));
    setMinimumSize(700, 500);
    setAcceptDrops(true); // for drag-and-drop mod archives

    // restoreGeometry() carries the maximized flag where the platform encodes it
    // (X11/Wayland/Windows); the "window/maximized" key is a first-run fallback.
    {
        QByteArray geo = Settings::windowGeometry();
        bool restored = !geo.isEmpty() && restoreGeometry(geo);
        if (!restored) {
            resize(950, 620);
            if (auto *screen = QGuiApplication::primaryScreen()) {
                QRect avail = screen->availableGeometry();
                move(avail.center().x() - width()  / 2,
                     avail.center().y() - height() / 2);
            }
        }
        if (Settings::windowMaximized())
            QTimer::singleShot(0, this,
                [this]{ setWindowState(windowState() | Qt::WindowMaximized); });
    }

    loadApiKey(); // populates m_apiKey (keychain where available, else settings)

    m_profiles = new GameProfileRegistry(this);
    m_profiles->load();
    applyCurrentProfileToMirrors();

    // Groundcover-approved mod paths (user confirmed at install time).
    {
        const QStringList gc = Settings::groundcoverApproved();
        m_groundcoverApproved = QSet<QString>(gc.begin(), gc.end());
    }

    // Declined-patch keys: user said "no" to an auto-detected patch.
    {
        const QStringList dp = Settings::declinedPatches();
        m_declinedPatches = QSet<QString>(dp.begin(), dp.end());
    }

    m_net = new QNetworkAccessManager(this);
    m_net->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);

    m_nexus = new NexusClient(this);
    m_nexus->setApiKey(m_apiKey);

    m_nexusCtl = new NexusController(m_nexus, this);
    connect(m_nexusCtl, &NexusController::updateFoundForItem,
            this, [this](QListWidgetItem *item) {
        item->setData(ModRole::UpdateAvailable, true);
        m_modList->update(m_modList->indexFromItem(item));
        // Tint the parent separator grey to nudge update-or-delete; clears once
        // every mod in the section is resolved.
        updateSectionCounts();
    });
    connect(m_nexusCtl, &NexusController::checkUpdatesFinished,
            this, &MainWindow::onCheckUpdatesFinished);
    connect(m_nexusCtl, &NexusController::titleFetched,
            this, &MainWindow::onTitleFetched);
    connect(m_nexusCtl, &NexusController::expectedChecksumFetched,
            this, &MainWindow::onExpectedChecksumFetched);
    connect(m_nexusCtl, &NexusController::fileListFetched,
            this, &MainWindow::onFileListFetched);
    connect(m_nexusCtl, &NexusController::fileListFetchFailed,
            this, &MainWindow::onFileListFetchFailed);
    connect(m_nexusCtl, &NexusController::dependenciesScanned,
            this, &MainWindow::onDependenciesScanned);
    connect(m_nexusCtl, &NexusController::dependencyScanFailed,
            this, &MainWindow::onDependencyScanFailed);

    // QListWidget->model decoupling, stage 1. Synced from m_modList via
    // refreshModelFromList; later stages move readers onto the model.
    m_model = new ModlistModel(this);

    m_installCtl = new InstallController(this);
    connect(m_installCtl, &InstallController::verificationStarted,
            this, &MainWindow::onVerificationStarted);
    connect(m_installCtl, &InstallController::verified,
            this, &MainWindow::onArchiveVerified);
    connect(m_installCtl, &InstallController::verificationFailed,
            this, &MainWindow::onArchiveVerificationFailed);
    connect(m_installCtl, &InstallController::extractionSucceeded,
            this, &MainWindow::onExtractionSucceeded);
    connect(m_installCtl, &InstallController::extractionFailed,
            this, &MainWindow::onExtractionFailed);

    m_loadCtl = new LoadOrderController(this);
    connect(m_loadCtl, &LoadOrderController::conflictsScanned,
            this, &MainWindow::onConflictsScanned);
    connect(m_loadCtl, &LoadOrderController::missingMastersScanned,
            this, &MainWindow::onMissingMastersScanned);

    connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::saveModList);

    m_forbidden = new ForbiddenModsRegistry(this);
    reloadForbiddenMods();

    m_backups = new BackupManager(
        [this]{ return modlistPath(); },
        [this]{ saveModList(); },
        this);
    connect(m_backups, &BackupManager::restoredFromDisk,
            this, [this](bool fullSync) {
        loadModList();
        reconcileLoadOrder();
        updateModCount();
        updateSectionCounts();
        scheduleConflictScan();
        if (fullSync) syncGameConfig();
    });
    connect(m_backups, &BackupManager::statusMessage,
            this, [this](const QString &msg, int ms) {
        statusBar()->showMessage(msg, ms);
    });

    // Before setupMenuBar() so the Columns submenu can wire its actions; the
    // widget is parented into the central layout in setupCentralWidget().
    m_columnHeader = new ColumnHeader(this);
    m_tbCustom     = new ToolbarCustomization(this);
    setupMenuBar();
    setupToolbar();
    setupCentralWidget();
    setupDownloadQueue();
    // ScanCoordinator must exist before loadModList(), which calls
    // warmDataFoldersCache() on the worker.
    m_scans = new ScanCoordinator(m_modList, this);
    loadModList();
    loadLoadOrder();
    absorbExternalLoadOrder(); // respect reorders done in OpenMW Launcher
    m_downloadQueue->cleanStaleDownloads();
    repairEmptyModPaths();
    reconcileLoadOrder();  // bring m_loadOrder up-to-date with installed mods
    syncGameConfig();
    scanMissingMasters();
    updateSectionCounts();
    m_mastersScanTimer = new QTimer(this);
    m_mastersScanTimer->setSingleShot(true);
    m_mastersScanTimer->setInterval(200);
    connect(m_mastersScanTimer, &QTimer::timeout,
            this, &MainWindow::runMissingMastersScan);

    // saveModList debounce (see scheduleSaveModList()). closeEvent stops it
    // before the synchronous flush.
    m_saveModListTimer = new QTimer(this);
    m_saveModListTimer->setSingleShot(true);
    m_saveModListTimer->setInterval(150);
    connect(m_saveModListTimer, &QTimer::timeout,
            this, [this]() { saveModList(); });

    QTimer::singleShot(0, m_scans, &ScanCoordinator::scheduleSizeScan);

    // Pre-warm QMessageBox under AppImage: standardPixmap/QIcon::fromTheme/the
    // box itself page in cold off FUSE, adding 200-500 ms to the first dialog.
    // Offscreen box warms the render path too. Deferred 100 ms past first paint.
    QTimer::singleShot(100, this, [this]() {
        for (auto sp : {QStyle::SP_MessageBoxQuestion, QStyle::SP_MessageBoxWarning,
                        QStyle::SP_MessageBoxInformation, QStyle::SP_MessageBoxCritical}) {
            (void)style()->standardPixmap(sp, nullptr, this);
        }
        for (const char *name : {"dialog-question", "dialog-warning",
                                  "dialog-information", "dialog-error"}) {
            (void)QIcon::fromTheme(QLatin1String(name));
        }

        // WA_DontShowOnScreen keeps the WM from seeing it - no flash, no focus
        // theft. Yes/No warm the standard-button path too.
        auto *box = new QMessageBox(this);
        box->setIcon(QMessageBox::Question);
        box->setText(QStringLiteral(" "));
        box->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box->setAttribute(Qt::WA_DontShowOnScreen, true);
        box->show();
        box->repaint();
        box->hide();
        box->deleteLater();
    });

    // Background AppImage warmup: page-cache the bundled Qt plugins, icon themes
    // and shared libs so first use doesn't stall on a cold FUSE read off the
    // squashfs mount. Runs on the pool while the user browses. Skipped on regular
    // installs. Small frequently-faulted dirs (plugin .so's) first, then assets.
    if (!qEnvironmentVariableIsEmpty("APPIMAGE")) {
        QTimer::singleShot(250, this, []() {
            (void)QtConcurrent::run([]() {
                QString appDir = qEnvironmentVariable("APPDIR");
                if (appDir.isEmpty()) {
                    // Derive from applicationDirPath ($APPDIR/usr/bin).
                    appDir = QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("../.."));
                    appDir = QDir(appDir).canonicalPath();
                }
                if (appDir.isEmpty() || !QDir(appDir).exists()) return;

                static const QStringList warmupSubdirs = {
                    QStringLiteral("usr/plugins/imageformats"),
                    QStringLiteral("usr/plugins/styles"),
                    QStringLiteral("usr/plugins/platformthemes"),
                    QStringLiteral("usr/plugins/iconengines"),
                    QStringLiteral("usr/plugins/tls"),
                    QStringLiteral("usr/plugins/platforminputcontexts"),
                    QStringLiteral("usr/plugins/xcbglintegrations"),
                    QStringLiteral("usr/lib"),
                    QStringLiteral("usr/share/icons"),
                };

                QByteArray scratch(64 * 1024, '\0');
                for (const QString &sub : warmupSubdirs) {
                    const QString root = QDir(appDir).filePath(sub);
                    if (!QDir(root).exists()) continue;
                    QDirIterator it(root, QDir::Files | QDir::NoSymLinks,
                                    QDirIterator::Subdirectories);
                    while (it.hasNext()) {
                        QFile f(it.next());
                        if (!f.open(QIODevice::ReadOnly)) continue;
                        while (!f.atEnd()
                               && f.read(scratch.data(), scratch.size()) > 0)
                        { /* drain into page cache */ }
                    }
                }
            });
        });
    }

    // One-time reminder if LOOT is missing, else "auto-sort skipped" is silent.
    QTimer::singleShot(800, this, &MainWindow::maybeShowLootMissingBanner);
    // First-run welcome. Deferred so the window paints before the modal grabs focus.
    QTimer::singleShot(300, this, &MainWindow::maybeShowFirstRunWizard);
    // After that window: nag once if the archive extractors are missing (the
    // helper self-gates on wizard-completed + a persisted "don't show again").
    QTimer::singleShot(1200, this, &MainWindow::checkExtractorsAvailable);

    // Animation timer for "installing" spinner (120 ms ≈ 8 fps)
    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(120);
    connect(m_animTimer, &QTimer::timeout, this, &MainWindow::onAnimTick);
    m_animTimer->start();

    // Conflict scan - debounce timer (400 ms single-shot)
    m_conflictTimer = new QTimer(this);
    m_conflictTimer->setSingleShot(true);
    m_conflictTimer->setInterval(400);
    connect(m_conflictTimer, &QTimer::timeout, this, &MainWindow::runConflictScan);
    scheduleConflictScan();

    // Drip-feed: one onInstallFromNexus at a time so the Nexus rate-limiter and
    // the modal file pickers don't pile up.
    m_bulkInstall = new BulkInstallQueue(m_modList,
        [this](QListWidgetItem *item) { onInstallFromNexus(item); }, this);
    connect(m_bulkInstall, &BulkInstallQueue::statusMessage,
            this, [this](const QString &msg, int ms) {
        statusBar()->showMessage(msg, ms);
    });

    statusBar()->showMessage(T("status_ready"));

    QTimer::singleShot(200, this, &MainWindow::checkNxmHandlerRegistration);
    QTimer::singleShot(400, this, &MainWindow::checkDesktopShortcut);

    // Undo / redo shortcuts
    auto *undoSc = new QShortcut(QKeySequence::Undo, this);
    connect(undoSc, &QShortcut::activated, m_undoStack, &UndoStack::performUndo);
    auto *redoSc = new QShortcut(QKeySequence::Redo, this);
    connect(redoSc, &QShortcut::activated, m_undoStack, &UndoStack::performRedo);

    // Ctrl+Home/End: jump to first/last row even with no selection. Qt's
    // built-in handlers need a current item and fail silently on a freshly
    // loaded list where nothing is highlighted, so do it by hand.
    auto jumpTo = [this](int row) {
        if (!m_modList || m_modList->count() == 0) return;
        row = qBound(0, row, m_modList->count() - 1);
        auto *target = m_modList->item(row);
        if (!target) return;
        m_modList->setCurrentItem(target);
        m_modList->scrollToItem(target, QAbstractItemView::PositionAtCenter);
    };
    auto *homeSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Home), this);
    connect(homeSc, &QShortcut::activated, this, [jumpTo]{ jumpTo(0); });
    auto *endSc  = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_End),  this);
    connect(endSc,  &QShortcut::activated, this,
        [this, jumpTo]{ jumpTo(m_modList ? m_modList->count() - 1 : 0); });

    // Ctrl+F: focus the filter bar so the user can start typing immediately.
    auto *findSc = new QShortcut(QKeySequence::Find, this);
    connect(findSc, &QShortcut::activated, this, [this]{
        m_filterBar->focus();
    });

    // Ctrl+W: close() not qApp->quit(), so QCloseEvent + aboutToQuit fire and the
    // shutdown hooks (saveModList etc.) still run.
    auto *closeSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this);
    connect(closeSc, &QShortcut::activated, this, &MainWindow::close);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_modList && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Delete && !m_modList->selectedItems().isEmpty()) {
            onRemoveSelected();
            return true;
        }
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
                && !m_modList->selectedItems().isEmpty()) {
            onItemDoubleClicked(m_modList->selectedItems().first());
            return true;
        }
    }
    if (obj == m_modList->viewport() && event->type() == QEvent::Wheel) {
        auto *we = static_cast<QWheelEvent *>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            if (we->angleDelta().y() > 0)
                m_zoom->zoomIn();
            else if (we->angleDelta().y() < 0)
                m_zoom->zoomOut();
            return true; // don't scroll
        }
    }
    // Push undo before a checkbox toggle: left-press in the checkbox area
    // (leftmost ~22 px of the item).
    if (!m_undoStack->isApplyingState() && obj == m_modList->viewport()
            && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            auto *item = m_modList->itemAt(me->pos());
            if (item && item->data(ModRole::ItemType).toString() == ItemType::Mod
                     && (item->flags() & Qt::ItemIsUserCheckable)) {
                QRect r = m_modList->visualItemRect(item);
                if (me->pos().x() <= r.left() + 22)
                    m_undoStack->pushUndo();
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}


void MainWindow::maybeShowFirstRunWizard()
{
    if (Settings::wizardCompleted()) return;

    // Users from before the wizard existed already have profiles/modlists/API
    // keys. Don't show them "pick your game" - detect prior setup and mark
    // completed. The completion flag alone isn't enough; probe for state.
    bool hasExistingModlist = false;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() == ItemType::Mod) {
            hasExistingModlist = true;
            break;
        }
    }
    // Probe every game's on-disk modlist - the current profile might not be the
    // one with mods. Via resolveUserStatePath so AppImage hits AppDataLocation,
    // not the read-only squashfs mount.
    if (!hasExistingModlist) {
        for (const GameProfile &gp : m_profiles->games()) {
            const QString filename = "modlist_" + gp.id + ".txt";
            const QString resolved = resolveUserStatePath(filename);
            QFileInfo fi(resolved);
            if (fi.exists() && fi.size() > 0) {
                hasExistingModlist = true;
                break;
            }
            // Legacy fallback: older builds wrote state alongside the binary,
            // before AppDataLocation became the primary path.
            for (const QString &dir : {QCoreApplication::applicationDirPath() + "/",
                                        QCoreApplication::applicationDirPath() + "/../"})
            {
                QFileInfo fb(dir + filename);
                if (fb.exists() && fb.size() > 0) {
                    hasExistingModlist = true;
                    break;
                }
            }
            if (hasExistingModlist) break;
        }
    }
    // Other signals of prior use: non-default game list, an API key (keychain or
    // legacy QSettings), or a saved mods-dir override.
    bool hasExistingSettings =
        !m_apiKey.isEmpty() ||
        !Settings::nexusApiKey().isEmpty() ||
        !Settings::gameIds().isEmpty();

    if (hasExistingModlist || hasExistingSettings) {
        Settings::setWizardCompleted(true);
        return;
    }

    firstrun::Result r;
    if (!firstrun::runWizard(this, builtinGameChoices(), r)) {
        // Cancelled. Don't mark completed (they'll see it next launch); app still
        // works with defaults.
        return;
    }

    // Game profile: switchToGame by matching index in the registry.
    for (int i = 0; i < m_profiles->size(); ++i) {
        if (m_profiles->games()[i].id == r.gameId) { switchToGame(i); break; }
    }

    // Mods directory. Create it if missing.
    if (!r.modsDir.isEmpty()) {
        QDir().mkpath(r.modsDir);
        m_modsDir = r.modsDir;
        if (m_downloadQueue) m_downloadQueue->setModsDir(m_modsDir);
        // Go through the active profile so the per-profile modsDir stays in sync.
        m_profiles->setActiveModsDir(r.modsDir);
        saveModList();
    }

    // API key (optional - empty means user skipped)
    if (!r.apiKey.isEmpty()) {
        m_apiKey = r.apiKey;
        saveApiKey(r.apiKey);
        if (m_nexus) m_nexus->setApiKey(m_apiKey);
    }

    // nxm:// handler - force a re-check; the logic inside does the real work.
    if (r.registerNxm) checkNxmHandlerRegistration();

    Settings::setWizardCompleted(true);
    statusBar()->showMessage(T("wizard_done_status"), 5000);
}



void MainWindow::updateThemeButton()
{
    if (!m_themeBtn) return;
    // Label names the theme the click will switch TO.
    m_themeBtn->setText(Settings::uiDarkMode() ? T("toolbar_light_mode")
                                               : T("toolbar_dark_mode"));
}




void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange) return;
    bool nowMax = isMaximized();
    if (nowMax == m_windowMaximized) return;
    m_windowMaximized = nowMax;
    if (m_columnHeader) m_columnHeader->onWindowStateChanged(nowMax);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // Any geometry change can shift the viewport's left/right edges (frame
    // thickness, scrollbar appearance, manual resize, snap-to-side gestures
    // on tiling WMs).
    if (m_columnHeader) m_columnHeader->updateScrollMargin();
}

// Mod count + collapsible separators

void MainWindow::updateModCount()
{
    // First reader to consume m_model directly instead of walking m_modList.
    // m_model stays in sync via connectAutoSync, so the count is current on any
    // toggle/drag. Null-check: this can fire before m_model is allocated during
    // first setup - fall back to the widget walk in that window.
    if (m_model) {
        const auto c = m_model->modCounts();
        m_modCountLabel->setText(T("status_mod_count").arg(c.active).arg(c.total));
        return;
    }
    int total = 0, active = 0;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        ++total;
        if (it->checkState() == Qt::Checked) ++active;
    }
    m_modCountLabel->setText(T("status_mod_count").arg(active).arg(total));
}



// NXM handler registration. Nexus uses two schemes: nxm:// (standard) and
// nxms:// (premium/CDN). Both must be registered or KDE/KIO throws "Unknown
// protocol: nxms" and nothing downloads.

// Forbidden mods


void MainWindow::checkDesktopShortcut()
{
    if (Settings::skipDesktopCheck())
        return;

    QString desktopDir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (desktopDir.isEmpty())
        return;

    QString shortcutPath = desktopDir + "/nerevarine_organizer.desktop";
    if (QFile::exists(shortcutPath))
        return;

    QMessageBox dlg(this);
    dlg.setWindowTitle(T("desktop_shortcut_title"));
    dlg.setIcon(QMessageBox::Question);
    dlg.setText(T("desktop_shortcut_question"));

    auto *dontAsk = new QCheckBox(T("desktop_shortcut_dont_ask"), &dlg);
    dlg.setCheckBox(dontAsk);
    dlg.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    dlg.setDefaultButton(QMessageBox::Yes);

    int result = dlg.exec();

    if (dontAsk->isChecked())
        Settings::setSkipDesktopCheck(true);

    if (result != QMessageBox::Yes)
        return;

    // Install icon to ~/.local/share/icons so the compositor can find it
    QString iconDir = QDir::homePath() + "/.local/share/icons/hicolor/256x256/apps";
    QDir().mkpath(iconDir);
    QString iconDest = iconDir + "/nerevarine_organizer.png";
    if (!QFile::exists(iconDest))
        QFile::copy(":/assets/icons/cystal_full_0.png", iconDest);
    subprocess::execute("gtk-update-icon-cache",
                      {"-f", "-t", QDir::homePath() + "/.local/share/icons/hicolor"});

    // Write .desktop file
    QFile f(shortcutPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ui::warn(this, T("desktop_shortcut_title"), T("desktop_shortcut_failed").arg(shortcutPath));
        return;
    }
    QString shortcutExec = qEnvironmentVariable("APPIMAGE");
    if (shortcutExec.isEmpty())
        shortcutExec = QCoreApplication::applicationFilePath();
    QTextStream out(&f);
    out << "[Desktop Entry]\n"
        << "Name=Nerevarine Organizer\n"
        << "Comment=Mod manager for Morrowind / OpenMW\n"
        << "Exec=" << shortcutExec << "\n"
        << "Icon=nerevarine_organizer\n"
        << "Terminal=false\n"
        << "Type=Application\n"
        << "Categories=Game;Utility;\n"
        << "StartupWMClass=nerevarine_organizer\n";
    f.close();

    // Mark it as executable (required by some desktop environments)
    f.setPermissions(f.permissions()
                     | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);

    statusBar()->showMessage(T("desktop_shortcut_created"), 4000);
}

// NXM URL → download


// Download queue - implementation lives in src/downloadqueue.cpp

// sanitizeFolderName - moved to include/fs_utils.h (fsutils::sanitizeFolderName).
// The `using` declaration below keeps existing call sites short.
using fsutils::sanitizeFolderName;


std::expected<void, QString>
MainWindow::verifyAndExtract(const QString &archivePath, QListWidgetItem *placeholder)
{
    // Pre-flight the two invariants the controller silently relied on.
    // Previously a null placeholder was a segfault and a missing archive
    // was a misleading "size mismatch (actual: 0)" downstream.
    if (!placeholder) {
        qCWarning(logging::lcInstall)
            << "verifyAndExtract: null placeholder for" << archivePath;
        return std::unexpected(QStringLiteral("null-placeholder"));
    }
    if (archivePath.isEmpty() || !QFileInfo::exists(archivePath)) {
        qCWarning(logging::lcInstall)
            << "verifyAndExtract: archive missing:" << archivePath;
        statusBar()->showMessage(
            T("status_extraction_failed"), 4000);
        return std::unexpected(QStringLiteral("archive-missing"));
    }

    // Expectations off the placeholder. Absent -> controller short-circuits and
    // signals verified() immediately (local-archive drops, NXM flows whose
    // metadata fetch failed).
    const QString expectedMd5  = placeholder->data(ModRole::ExpectedMd5).toString()
                                      .trimmed().toLower();
    const qint64  expectedSize = placeholder->data(ModRole::ExpectedSize).toLongLong();
    // Tokens minted in prepareItemForInstall(); a path without one is an older
    // placeholder - backfill so InstallController signals can match it.
    QUuid token = placeholder->data(ModRole::InstallToken).toUuid();
    if (token.isNull()) {
        token = QUuid::createUuid();
        placeholder->setData(ModRole::InstallToken, token);
    }
    m_installCtl->verifyArchive(archivePath, token, expectedMd5, expectedSize);
    return {};
}

void MainWindow::onVerificationStarted(const QString &archivePath)
{
    statusBar()->showMessage(
        T("status_verifying").arg(QFileInfo(archivePath).fileName()));
}

void MainWindow::onArchiveVerified(const QString &archivePath, const QUuid &installToken)
{
    // Look up by token. May be in m_modList (active profile), m_strandedInstalls
    // (parked across a profile switch), or gone (row removed mid-verify, restart).
    QString profileKey;
    QListWidgetItem *placeholder = findPlaceholderByToken(installToken, &profileKey);
    if (!placeholder) {
        QFile::remove(archivePath);
        return;
    }
    // Nothing to verify, or verify succeeded. MD5 path shows a transient
    // "verified OK" so the user sees the hash ran; empty path says nothing.
    if (!placeholder->data(ModRole::ExpectedMd5).toString().trimmed().isEmpty())
        statusBar()->showMessage(T("verify_status_ok"), 3000);
    // Clear expectation roles now that they've served their purpose.
    placeholder->setData(ModRole::ExpectedMd5,  QVariant());
    placeholder->setData(ModRole::ExpectedSize, QVariant());
    if (const auto r = extractAndAdd(archivePath, placeholder); !r) {
        qCWarning(logging::lcInstall)
            << "extractAndAdd after verify failed precondition:" << r.error();
    }
}


std::expected<void, QString>
MainWindow::extractAndAdd(const QString &archivePath, QListWidgetItem *placeholder)
{
    // Pre-flight what the controller used to assume. A stale/missing archive
    // reached QProcess and surfaced as "unzip exit code 9"; an unset mods dir
    // unpacked into the cwd.
    if (!placeholder) {
        qCWarning(logging::lcInstall)
            << "extractAndAdd: null placeholder for" << archivePath;
        return std::unexpected(QStringLiteral("null-placeholder"));
    }
    if (archivePath.isEmpty() || !QFileInfo::exists(archivePath)) {
        qCWarning(logging::lcInstall)
            << "extractAndAdd: archive missing:" << archivePath;
        statusBar()->showMessage(
            T("status_extraction_failed"), 4000);
        return std::unexpected(QStringLiteral("archive-missing"));
    }
    if (m_modsDir.isEmpty()) {
        // Active profile hasn't picked a mods dir yet (new profile from the
        // toolbar or WJ test flow). Prompt before bailing - the "clean modlist,
        // where do you want mods?" hand-off.
        if (!ensureModsDirForActiveProfile()) {
            qCWarning(logging::lcInstall)
                << "extractAndAdd: user cancelled the mods-dir prompt";
            statusBar()->showMessage(
                T("status_extraction_failed"), 4000);
            return std::unexpected(QStringLiteral("mods-dir-unset"));
        }
    }

    // QProcess + extension-dispatch live in InstallController; FOMOD wizard and
    // addModFromPath run from onExtractionSucceeded below.
    //
    // The placeholder's ModPath is passed as a "reuse hint" so the controller
    // extracts back into the existing wrapper dir on reinstall/cross-machine
    // sync. Without it each install coins a fresh "_<ts>" wrapper and the path
    // drifts.
    //
    // Exception: a merge-in-progress row still points ModPath at the folder
    // we're about to overlay. Reusing it would make resolveReuseWrapper wipe
    // those files first (if the optional's basename matches). Force a fresh
    // extract dir; the overlay onto MergeTargetPath happens in
    // onExtractionSucceeded.
    QString reuseHint;
    if (placeholder->data(ModRole::MergeTargetPath).toString().isEmpty()) {
        reuseHint = placeholder->data(ModRole::ModPath).toString();
        // Copy-on-write fork: if the folder is shared with another profile,
        // never reuse/overwrite it. Extract fresh so this reinstall/update lands
        // in THIS profile's copy while the other keeps the original (sibling-
        // dedup + prevModPath deletes are already shared-aware).
        if (!reuseHint.isEmpty()
            && modPathReferencedByOtherProfile(mod_sharing::cleanModPath(reuseHint))) {
            statusBar()->showMessage(T("share_fork_on_reinstall"), 6000);
            reuseHint.clear();
        }
    }
    QUuid token = placeholder->data(ModRole::InstallToken).toUuid();
    if (token.isNull()) {
        token = QUuid::createUuid();
        placeholder->setData(ModRole::InstallToken, token);
    }
    // Name hint used only when the download turns out to be a bare loose file
    // (not an archive) whose name carries no usable extension - so it lands as
    // "<mod title>.esp" rather than a bare download id.
    QString looseHint = placeholder->data(ModRole::NexusTitle).toString().trimmed();
    if (looseHint.isEmpty())
        looseHint = placeholder->data(ModRole::CustomName).toString().trimmed();

    // A CDN download can land under a bare, extensionless id. Give it its real
    // name before extracting so the mod FOLDER (named after the archive
    // basename) comes out proper and stable - routing itself is already
    // magic-based and correct either way. Only touch a file we staged inside
    // modsDir (never a user's dragged-in file elsewhere), prefer the
    // authoritative Nexus files.json name, and keep it best-effort: a rename
    // must never block an otherwise-good install.
    QString effectivePath = archivePath;
    if (QFileInfo(archivePath).absolutePath() == QDir(m_modsDir).absolutePath()) {
        QByteArray header;
        if (QFile hf(archivePath); hf.open(QIODevice::ReadOnly))
            header = hf.read(16);
        const QString currentName = QFileInfo(archivePath).fileName();
        const QString corrected = archive_magic::archiveFileName(
            currentName,
            placeholder->data(ModRole::NexusFileName).toString().trimmed(),
            archive_magic::sniff(header));
        if (corrected != currentName) {
            const QString target = QDir(m_modsDir).filePath(corrected);
            if (!QFileInfo::exists(target) && QFile::rename(archivePath, target))
                effectivePath = target;
        }
    }
    placeholder->setData(ModRole::NexusFileName, QVariant());  // consumed

    m_installCtl->extractArchive(effectivePath, m_modsDir, token, reuseHint, looseHint);
    return {};
}



void MainWindow::resetPlaceholderAfterInstallCancel(QListWidgetItem *placeholder,
                                                     const QString &archivePath)
{
    // Row may have been removed mid-install.  Bail silently if so.
    if (!placeholder || !m_modList->indexFromItem(placeholder).isValid())
        return;

    // Merge-in-progress cancel (FOMOD/BAIN wizard dismissed): this row is an
    // existing installed mod we were about to overlay onto, and its folder was
    // never touched.  Restore it to "installed" at the merge target instead of
    // wiping it back to "not installed", which would orphan the live folder.
    const QString mergeTarget = placeholder->data(ModRole::MergeTargetPath).toString();
    placeholder->setData(ModRole::MergeTargetPath, QVariant());   // consume either way
    if (!mergeTarget.isEmpty() && QDir(mergeTarget).exists()) {
        placeholder_state::markInstalled(placeholder, mergeTarget);
        saveModList();
        return;
    }

    placeholder_state::resetToNotInstalled(
        placeholder, QFileInfo(archivePath).completeBaseName());
    saveModList();
}

void MainWindow::prepareItemForInstall(QListWidgetItem *item)
{
    QString name = item->data(ModRole::CustomName).toString();
    if (name.isEmpty()) name = item->text();
    item->setText(QString("⠋ %1 (%2)").arg(T("status_installing_label"), name));
    item->setData(ModRole::InstallStatus, 2);
    // Mint a stable per-install identity so InstallController signals can
    // route back to this row even after a profile switch parks it in
    // m_strandedInstalls.  Reuse an existing token if one is already
    // present (re-install of a row that still carries a pending token).
    if (item->data(ModRole::InstallToken).toUuid().isNull())
        item->setData(ModRole::InstallToken, QUuid::createUuid());
    placeholder_state::setBusyFlags(item);
}

void MainWindow::applyInstalledStateToStrandedPlaceholder(
    QListWidgetItem *placeholder, const QString &modPath)
{
    // The cross-profile completion case is exactly the "mark installed" role
    // transition - no m_modList iteration / load-order / openmw.cfg sync (those
    // belong to the active profile); the caller persists via saveModListFor.
    placeholder_state::markInstalled(placeholder, modPath);
}






void MainWindow::onExpectedChecksumFetched(QListWidgetItem *item, const QString &fileName,
                                            const QString &md5, qint64 sizeBytes)
{
    if (!m_modList->indexFromItem(item).isValid()) return;
    if (!fileName.isEmpty()) item->setData(ModRole::NexusFileName, fileName);
    if (!md5.isEmpty())      item->setData(ModRole::ExpectedMd5,  md5);
    if (sizeBytes > 0)       item->setData(ModRole::ExpectedSize, sizeBytes);
}

// Removes not-installed, path-less placeholders whose NexusUrl or CustomName
// matches the given installed item.  Called from addModFromPath and again from
// onTitleFetched to handle the async title-arrives-after-install race.
void MainWindow::purgeDuplicatePlaceholders(QListWidgetItem *installed)
{
    if (!installed) return;
    const QString installedUrl  = installed->data(ModRole::NexusUrl).toString();
    QString installedName = installed->data(ModRole::CustomName).toString().trimmed().toLower();
    if (installedName.isEmpty())
        installedName = installed->data(ModRole::NexusTitle).toString().trimmed().toLower();
    if (installedUrl.isEmpty() && installedName.isEmpty()) return;

    bool removed = false;
    for (int r = m_modList->count() - 1; r >= 0; --r) {
        auto *cand = m_modList->item(r);
        if (cand == installed) continue;
        if (cand->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (cand->data(ModRole::InstallStatus).toInt() != 0) continue;
        if (!cand->data(ModRole::ModPath).toString().isEmpty()) continue;
        const bool sameUrl  = !installedUrl.isEmpty()
                              && cand->data(ModRole::NexusUrl).toString() == installedUrl;
        const bool sameName = !installedName.isEmpty()
                              && cand->data(ModRole::CustomName).toString().trimmed().toLower()
                                 == installedName;
        if (sameUrl || sameName) {
            delete m_modList->takeItem(r);
            removed = true;
        }
    }
    if (removed) saveModList();
}

void MainWindow::onTitleFetched(QListWidgetItem *item, const QString &name)
{
    // The controller doesn't know whether the row has since been removed.
    if (!m_modList->indexFromItem(item).isValid()) {
        m_titleSetsCustomName.remove(item);
        return;
    }
    item->setData(ModRole::NexusTitle, name);
    if (m_titleSetsCustomName.remove(item)
        && item->data(ModRole::CustomName).toString().isEmpty()) {
        item->setData(ModRole::CustomName, name);
        item->setText(name);
        saveModList();
    }
    // Second-pass dedup: if the title arrived after addModFromPath already ran
    // (CustomName was empty at install time), purge any matching placeholder now.
    if (item->data(ModRole::InstallStatus).toInt() == 1)
        purgeDuplicatePlaceholders(item);
}

bool MainWindow::confirmNotForbidden(const QString &game, int modId)
{
    const ForbiddenMod *f = m_forbidden->find(game, modId);
    if (!f) return true;   // not forbidden - clear to proceed

    // Custom button set (Manage + OK) and clickedButton inspection, so this
    // stays a raw QMessageBox rather than a ui:: helper (see prompts.h scope).
    QMessageBox warn(this);
    warn.setWindowTitle(T("forbidden_warn_title"));
    warn.setIcon(QMessageBox::Critical);
    warn.setText(T("forbidden_warn_body").arg(f->name, f->annotation));
    auto *manageBtn = warn.addButton(T("forbidden_open_manager"), QMessageBox::ActionRole);
    warn.addButton(QMessageBox::Ok);
    warn.setDefaultButton(QMessageBox::Ok);
    warn.exec();
    if (warn.clickedButton() == manageBtn)
        m_forbidden->showManageDialog(this);
    return false;          // forbidden - hard block, no install-anyway escape
}

MainWindow::ReinstallChoice
MainWindow::confirmReinstallIfInstalled(const QString &game, int modId,
                                         QListWidgetItem *except, bool allowMerge)
{
    // Find an already-installed row for this mod page through the typed model
    // (Stage 2 of the QListWidget->ModlistModel migration) instead of walking
    // m_modList and re-parsing every NexusUrl by hand.  m_model is kept in sync
    // synchronously via modlist::connectAutoSync, so its row indices line up
    // with m_modList and the scan sees current state.
    if (!m_model) return ReinstallChoice::NotInstalled;
    const int exceptRow = except ? m_modList->row(except) : -1;
    const int row = m_model->findInstalledByModId(game, modId, exceptRow);
    if (row < 0) return ReinstallChoice::NotInstalled;

    {
        const QString existingName = m_model->at(row).effectiveName();

        // Four-way disambiguation: a single Nexus mod page can ship multiple
        // distinct optional files (Wretched + Sage's Backgrounds on mod
        // 58704), the same modId also identifies "the new version of <mod>"
        // in Nexus's update flow, and some pages ship optional downloads
        // meant to OVERRIDE the main download's files (OAAB Data optionals).
        //   · Replace  - treat as an update; old folder removed after install.
        //   · Separate - sibling file; install in its own folder.
        //   · Merge    - overlay the new files on top of the existing folder
        //                (last-writer-wins); MO2's "merge".
        // The old OK/Cancel prompt treated every match as Replace, silently
        // overwriting the prior install when the user wanted a sibling. Default
        // focus to Separate - non-destructive and the common case for pages
        // bundling complementary content.
        QMessageBox box(this);
        box.setWindowTitle(T("reinstall_warn_title"));
        box.setIcon(QMessageBox::Question);
        box.setText(T("reinstall_warn_body").arg(existingName));
        auto *replaceBtn  = box.addButton(T("reinstall_choice_replace"),
                                          QMessageBox::AcceptRole);
        QPushButton *mergeBtn = allowMerge
            ? box.addButton(T("reinstall_choice_merge"), QMessageBox::ActionRole)
            : nullptr;
        auto *separateBtn = box.addButton(T("reinstall_choice_separate"),
                                          QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(separateBtn);
        box.exec();
        if (box.clickedButton() == replaceBtn)        return ReinstallChoice::Replace;
        if (mergeBtn && box.clickedButton() == mergeBtn) return ReinstallChoice::Merge;
        if (box.clickedButton() == separateBtn)       return ReinstallChoice::Separate;
        return ReinstallChoice::Cancel;
    }
}

void MainWindow::checkModDependencies(const QString &game, int modId, QListWidgetItem *item)
{
    // Heuristic: Nexus has no structured "requirements" endpoint in the public
    // v1 API, so we fetch the mod info and scan the description for Nexus mod
    // URLs matching the current game.  Any mod URL that isn't already in the
    // user's list is flagged as a *possible* requirement.  User can still
    // proceed if the hit is a false positive (optional patch, translation,
    // addon mentioned in the description, etc.).
    statusBar()->showMessage(T("status_checking_deps").arg(modId));

    // Build id→url map for installed mods of this game so the controller's
    // scan can bucket present vs. missing without ever touching the
    // QListWidget.
    QMap<int, QString> idToUrl;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        const QString u = it->data(ModRole::NexusUrl).toString();
        if (u.isEmpty()) continue;
        const auto ref = parseNexusModUrl(u);
        if (!ref || ref->game != game) continue;
        idToUrl[ref->modId] = u;
    }
    m_nexusCtl->scanDependencies(item, game, modId, idToUrl);
}

void MainWindow::onDependencyScanFailed(QListWidgetItem *item,
                                        const QString &game, int modId)
{
    // Network or API error - don't block install, just continue.
    fetchModFiles(game, modId, item);
}


// Same-modpage auto-link. A Nexus modpage's MAIN, UPDATE, and PATCH/OPTIONAL
// files all share NexusUrl, so installing them produces multiple rows. Without
// linking them a patch can be enabled with its base disabled, a frequent
// crash. Rule: MAIN or UPDATE adopts siblings as dependents; anything else
// becomes a dependent of the shared URL. DependsOn dedups so reinstalls are
// idempotent.
void MainWindow::autoLinkSameModpage(QListWidgetItem *item, const QString &categoryHint)
{
    // Snapshot the list, find the new entry's idx, hand off to
    // deps::autoLinkSameModpage for the decision, apply the returned
    // mutations here. Idempotency check stays at the widget boundary.
    QList<deps::ModEntry> snap;
    int newIdx = -1;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;

        deps::ModEntry e;
        e.idx       = i;
        e.nexusUrl  = it->data(ModRole::NexusUrl).toString();
        e.enabled   = (it->checkState() == Qt::Checked);
        e.installed = (it->data(ModRole::InstallStatus).toInt() == 1);
        e.dependsOn = it->data(ModRole::DependsOn).toStringList();
        snap.append(e);

        if (it == item) newIdx = i;
    }
    if (newIdx < 0) return;

    deps::ModEntry newMod;
    for (const auto &e : snap) if (e.idx == newIdx) { newMod = e; break; }

    const auto actions = deps::autoLinkSameModpage(newMod, snap, categoryHint);

    for (const auto &a : actions) {
        auto *target = m_modList->item(a.targetIdx);
        if (!target) continue;
        QStringList curr = target->data(ModRole::DependsOn).toStringList();
        if (curr.contains(a.urlToAppend)) continue; // idempotent
        curr << a.urlToAppend;
        target->setData(ModRole::DependsOn, curr);
    }
}

void MainWindow::fetchModFiles(const QString &game, int modId, QListWidgetItem *item,
                                bool autoPickMain)
{
    statusBar()->showMessage(T("status_fetching_files").arg(modId));
    if (autoPickMain)
        m_autoPickMainItems.insert(item);
    m_nexusCtl->fetchFileList(item, game, modId);
}

void MainWindow::onFileListFetchFailed(QListWidgetItem *item, const QString &reason, int httpStatus)
{
    m_autoPickMainItems.remove(item);
    const QString msg =
          (httpStatus == 401) ? T("nexus_api_error_link_401")
        : (httpStatus == 403) ? T("nexus_api_error_link_403")
        :                        T("nexus_api_error_link").arg(reason);
    ui::warn(this, T("nexus_api_error_title"), msg);
    statusBar()->showMessage(T("status_download_failed"), 4000);
}



void MainWindow::runGroundcoverHelper(QListWidgetItem *item, const QString &modRoot)
{
    // If the mod looks like a groundcover/grass mod, ask the user every install
    // whether Nerevarine should manage it as groundcover.  The answer sets OR
    // clears the approval for both the mod path and the Nexus URL, so changing
    // one's mind on a later reinstall downgrades it to regular content=.
    if (m_profiles->isEmpty() || currentProfile().id != "morrowind") return;

    QString displayName = item->data(ModRole::CustomName).toString();
    if (displayName.isEmpty()) displayName = item->text();
    if (!post_install::looksLikeGroundcover(modRoot, displayName)) return;

    const QString nexusUrl = item->data(ModRole::NexusUrl).toString();

    QMessageBox box(this);
    box.setWindowTitle(T("groundcover_assist_title"));
    box.setText(T("groundcover_assist_body").arg(displayName));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::Yes);
    // Paint a grass emoji as the dialog icon.
    {
        const int sz = box.style()->pixelMetric(QStyle::PM_MessageBoxIconSize, nullptr, &box);
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QFont ef;
        ef.setPixelSize(sz * 3 / 4);
        QPainter p(&pm);
        p.setFont(ef);
        p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("☘"));
        p.end();
        box.setIconPixmap(pm);
    }
    const bool userYes = (box.exec() == QMessageBox::Yes);

    // Re-apply the latest answer unconditionally.  The mod path changes on
    // every reinstall (timestamp suffix), so even a repeat "yes" may add a
    // fresh path entry; a persisted save + saveModList() keep openmw.cfg in
    // sync either way.
    if (userYes) {
        m_groundcoverApproved.insert(modRoot);
        if (!nexusUrl.isEmpty())
            m_groundcoverApproved.insert(nexusUrl);
    } else {
        m_groundcoverApproved.remove(modRoot);
        if (!nexusUrl.isEmpty())
            m_groundcoverApproved.remove(nexusUrl);
    }
    Settings::setGroundcoverApproved(
        QStringList(m_groundcoverApproved.begin(), m_groundcoverApproved.end()));
    saveModList();   // re-sync cfg with updated groundcover= lines
}

void MainWindow::runSplashScreenHelper(const QString &modRoot)
{
    // If the newly added mod ships a Splash/ directory (splash replacer), offer
    // to delete the default Morrowind splash screens so only the mod's show.
    if (m_profiles->isEmpty() || currentProfile().id != "morrowind") return;

    const QString splashDir = post_install::findSplashDir(modRoot);
    if (splashDir.isEmpty()) return;

    // Find the base game's Splash/ directory from external data= in openmw.cfg.
    QString baseGameSplash;
    {
        QFile cfg(QDir::homePath() + "/.config/openmw/openmw.cfg");
        if (cfg.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QStringList externals =
                openmw::externalDataPaths(QString::fromUtf8(cfg.readAll()));
            static const QStringList globs = {"*.tga", "*.bmp", "*.png", "*.jpg"};
            for (const QString &ext : externals) {
                QDir d(ext);
                for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                    if (sub.compare("splash", Qt::CaseInsensitive) != 0) continue;
                    QDir sd(d.filePath(sub));
                    if (!sd.entryList(globs, QDir::Files).isEmpty()) {
                        baseGameSplash = sd.absolutePath();
                        break;
                    }
                }
                if (!baseGameSplash.isEmpty()) break;
            }
        }
    }
    if (baseGameSplash.isEmpty()) return;

    QDir sd(baseGameSplash);
    const QStringList defaultSplash =
        sd.entryList({"*.tga", "*.bmp", "*.png", "*.jpg"}, QDir::Files);
    if (defaultSplash.isEmpty()) return;

    const int ret = QMessageBox::question(this,
        T("splash_delete_title"),
        T("splash_delete_body").arg(defaultSplash.size()).arg(baseGameSplash),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes);
    if (ret != QMessageBox::Yes) return;

    int removed = 0;
    for (const QString &f : defaultSplash)
        if (QFile::remove(sd.filePath(f))) ++removed;
    statusBar()->showMessage(T("splash_deleted_status").arg(removed), 5000);
}

void MainWindow::offerBundledPatchReenable(QListWidgetItem *item)
{
    // Offer to re-enable patches for this mod that are bundled in OTHER mods as
    // "<N> ... for <ThisMod>" subfolders.  syncOpenMWConfig auto-skips those
    // while their target is absent; when the target finally installs the user
    // may want the patch back - but may also prefer it stay off.  Ask once; the
    // declined set persists the "no" answer across sessions.
    const QString newModName = item->text().trimmed();
    if (newModName.isEmpty()) return;
    const QString newNormalized = post_install::normalizeModName(newModName);
    if (newNormalized.length() < 4) return;

    // Each hit = "<hostModDisplayName>\t<hostModPath>\t<subfolderName>"
    QStringList hits;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *other = m_modList->item(i);
        if (other == item) continue;
        if (other->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (other->data(ModRole::InstallStatus).toInt() != 1) continue;
        const QString hostPath = other->data(ModRole::ModPath).toString();
        if (hostPath.isEmpty()) continue;
        QDir host(hostPath);
        const QStringList subs = host.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &sub : subs) {
            if (post_install::bundledPatchMatchesMod(sub, newNormalized))
                hits << other->text() + '\t' + hostPath + '\t' + sub;
        }
    }
    if (hits.isEmpty()) return;

    QStringList bulletList;
    bulletList.reserve(hits.size());
    for (const QString &h : hits) {
        const QStringList parts = h.split('\t');
        bulletList << "  • " + parts.value(0) + " → " + parts.value(2);
    }
    QMessageBox box(this);
    box.setWindowTitle(T("patch_prompt_title"));
    box.setIcon(QMessageBox::Question);
    box.setText(T("patch_prompt_body").arg(newModName).arg(hits.size()));
    box.setInformativeText(bulletList.join('\n'));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::Yes);
    const int ret = box.exec();

    bool changed = false;
    for (const QString &h : hits) {
        const QStringList parts = h.split('\t');
        if (parts.size() != 3) continue;
        const QString key = parts.value(1) + '\t' + parts.value(2);
        if (ret == QMessageBox::Yes) {
            if (m_declinedPatches.remove(key)) changed = true;
        } else {
            if (!m_declinedPatches.contains(key)) {
                m_declinedPatches.insert(key);
                changed = true;
            }
        }
    }
    if (changed) {
        Settings::setDeclinedPatches(
            QStringList(m_declinedPatches.begin(), m_declinedPatches.end()));
        syncOpenMWConfig();
    }
}

// Existing slots



void MainWindow::onAddMod()
{
    QString path = QFileDialog::getExistingDirectory(
        this, T("add_mod_dialog_title"));
    if (path.isEmpty()) return;

    m_undoStack->pushUndo();
    QFileInfo fi(path);
    auto *item = new QListWidgetItem(fi.fileName());
    item->setData(ModRole::ItemType,      ItemType::Mod);
    item->setData(ModRole::ModPath,       path);
    item->setData(ModRole::InstallStatus, 1); // user browsed to it - it exists
    item->setData(ModRole::DateAdded,     QDateTime::currentDateTime());
    item->setCheckState(Qt::Checked);
    item->setToolTip(path);

    int row = m_modList->currentRow();
    m_modList->insertItem(row < 0 ? m_modList->count() : row + 1, item);
    m_modList->setCurrentItem(item);
    statusBar()->showMessage(T("status_mod_added").arg(fi.fileName()), 2000);
    saveModList();
}


void MainWindow::onMoveUp()
{
    int row = m_modList->currentRow();
    if (row <= 0) return;
    dropViewSortKeepingOrder();   // a manual move commits the current display as saved order
    m_undoStack->pushUndo();
    auto *item = m_modList->takeItem(row);
    m_modList->insertItem(row - 1, item);
    m_modList->setCurrentRow(row - 1);
    scheduleSaveModList();   // debounced - hold Alt+↑ rapid-fires moves
}

void MainWindow::onMoveDown()
{
    int row = m_modList->currentRow();
    if (row < 0 || row >= m_modList->count() - 1) return;
    dropViewSortKeepingOrder();   // a manual move commits the current display as saved order
    m_undoStack->pushUndo();
    auto *item = m_modList->takeItem(row);
    m_modList->insertItem(row + 1, item);
    m_modList->setCurrentRow(row + 1);
    scheduleSaveModList();   // debounced - hold Alt+↓ rapid-fires moves
}

void MainWindow::onCheckUpdates()
{
    if (m_apiKey.isEmpty()) {
        ui::info(this, T("nxm_api_key_required_title"), T("nxm_api_key_required_body"));
        onSetApiKey();
        if (m_apiKey.isEmpty()) return;
    }

    // Collect installed mods that have a NexusUrl.  Widget-side work: pulling
    // ModRole fields + parsing the stored Nexus page URL lives here so the
    // controller never has to know about QListWidget or ModRole.
    QList<NexusController::CheckTarget> toCheck;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 1) continue;
        const QString nexusUrl = item->data(ModRole::NexusUrl).toString();
        if (nexusUrl.isEmpty()) continue;

        // path: /{game}/mods/{modId}
        const auto ref = parseNexusModUrl(nexusUrl);
        if (!ref) continue;

        // Clear any stale flag from a previous check
        item->setData(ModRole::UpdateAvailable, false);
        toCheck.append({item, ref->game, ref->modId});
    }

    if (toCheck.isEmpty()) {
        statusBar()->showMessage(T("check_updates_no_mods"), 3000);
        return;
    }

    statusBar()->showMessage(T("check_updates_checking").arg(toCheck.size()));
    m_nexusCtl->checkForUpdates(toCheck, [](QListWidgetItem *item) {
        return item->data(ModRole::DateAdded).toDateTime();
    });
}

void MainWindow::onCheckUpdatesFinished(int foundCount)
{
    // Final sweep once the batch of per-mod checks has drained: covers the
    // edge case where onCheckUpdates cleared every stale UpdateAvailable flag
    // and the new batch found nothing, so no updateFoundForItem fired to
    // retrigger the recompute.  Without this, a separator that was grey
    // before the check stays grey even after all pending updates are gone.
    updateSectionCounts();

    if (foundCount == 0) {
        statusBar()->showMessage(T("check_updates_none"), 4000);
        m_notify->show(T("check_updates_none"), "#1a6fa8");
        subprocess::startDetached("notify-send",
            {"-i", "dialog-information",
             "-t", "6000",
             T("window_title"),
             T("check_updates_none")});
    } else {
        const QString msg = T("check_updates_found").arg(foundCount);
        statusBar()->showMessage(msg, 5000);
        subprocess::startDetached("notify-send",
            {"-i", "software-update-available",
             "-t", "6000",
             T("window_title"),
             msg});
    }
}

// Batch-update review screen
//
// Collects every mod the last onCheckUpdates pass flagged with
// UpdateAvailable=true, shows them in a checklist, updates a subset or "Update
// All". Selected rows take the same path as the green-triangle arrow:
// prepare-for-install + fetchModFiles, with autoPickMain so the per-mod file
// picker is skipped (15 back-to-back modal pickers is worse than taking the
// first MAIN/UPDATE file).
//
// Single-slot queue (kMaxConcurrentDownloads=1) serialises the network I/O, so
// just loop and kick each off - they queue and drain one at a time.
void MainWindow::onReviewUpdates()
{
    if (m_apiKey.isEmpty()) {
        ui::info(this, T("nxm_api_key_required_title"), T("nxm_api_key_required_body"));
        onSetApiKey();
        if (m_apiKey.isEmpty()) return;
    }

    // Snapshot the current UpdateAvailable rows.
    QList<ReviewUpdates::Candidate> candidates;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (!it->data(ModRole::UpdateAvailable).toBool())            continue;

        const QString url = it->data(ModRole::NexusUrl).toString();
        const auto ref = parseNexusModUrl(url);
        if (!ref) continue;

        QString name = it->data(ModRole::CustomName).toString();
        if (name.isEmpty()) name = it->text();
        candidates.append({it, name, ref->game, ref->modId, url});
    }

    if (candidates.isEmpty()) {
        ui::info(this, T("review_updates_title"), T("review_updates_nothing"));
        return;
    }

    const auto picked = ReviewUpdates::showDialog(this, candidates);
    if (picked.isEmpty()) return;

    // Single-slot download queue (kMaxConcurrentDownloads=1) serialises the
    // actual network I/O, so we just loop and kick each one off.
    statusBar()->showMessage(T("review_updates_kicking_off").arg(picked.size()));
    for (const auto &c : picked) {
        // Re-validate: user could have removed a row between check-updates
        // and the dialog exec.
        if (!m_modList->indexFromItem(c.item).isValid()) continue;
        prepareItemForInstall(c.item);
        fetchModFiles(c.game, c.modId, c.item, /*autoPickMain=*/true);
    }
}



void MainWindow::openSendToDialog()
{
    if (auto *sep = send_to_dialog::pickSeparator(this, m_modList))
        sendSelectedToSeparator(sep);
}


void MainWindow::onCurrentModChanged(QListWidgetItem *current, QListWidgetItem * /*previous*/)
{
    // Clear all highlights first.
    for (int i = 0; i < m_modList->count(); ++i)
        m_modList->item(i)->setData(ModRole::HighlightRole, 0);

    if (!current || current->data(ModRole::ItemType).toString() != ItemType::Mod) {
        m_modList->viewport()->update();
        return;
    }

    // Build a ModEntry snapshot and delegate the Dep/User decision to the
    // pure helper in deps_resolver.  Non-mod rows (separators) are skipped
    // but still get an entry with empty URL + no deps so the output index
    // space lines up with m_modList row indices.
    QList<deps::ModEntry> snap;
    snap.reserve(m_modList->count());
    int selectedIdx = -1;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        deps::ModEntry e;
        e.idx = i;
        if (it->data(ModRole::ItemType).toString() == ItemType::Mod) {
            e.nexusUrl  = it->data(ModRole::NexusUrl).toString();
            e.dependsOn = it->data(ModRole::DependsOn).toStringList();
            e.isUtility = it->data(ModRole::IsUtility).toBool();
        }
        snap.append(e);
        if (it == current) selectedIdx = i;
    }

    const auto hl = deps::computeSelectionHighlights(snap, selectedIdx);
    for (int i = 0; i < hl.size(); ++i)
        m_modList->item(i)->setData(ModRole::HighlightRole, static_cast<int>(hl[i]));

    m_modList->viewport()->update();
}




// API-key storage - prefers QKeychain (libsecret / KWallet / DPAPI),
// transparently migrates away from the old plain-text QSettings value on
// the first launch after the library becomes available.
//
// When built without HAVE_QTKEYCHAIN (library missing at configure time),
// falls back to QSettings so the app still works - the key lives in
// ~/.config/<vendor>/<app>.conf then, same as before.






// One-time startup nag if the archive extractors are missing, so the user finds
// out before a download fails mid-install with a confusing "could not launch
// '7z'". Gated on the first-run wizard being done (don't stack on it) and a
// persisted "don't show again".
void MainWindow::checkExtractorsAvailable()
{
    if (!Settings::wizardCompleted() || Settings::skipExtractorCheck())
        return;

    // Resolve against the same PATH the real extraction uses: childEnvironment()
    // is the system env outside an AppImage and the restored pre-AppImage env
    // inside one, so a system-installed tool isn't missed under the bundle. An
    // empty list makes findExecutable fall back to $PATH, which is the no-op
    // (non-AppImage) case anyway.
    const QStringList paths =
        subprocess::childEnvironment().value(QStringLiteral("PATH"))
            .split(QDir::listSeparator(), Qt::SkipEmptyParts);

    QStringList missing;
    for (const QString &prog : {QStringLiteral("7z"), QStringLiteral("unzip"),
                                QStringLiteral("unrar")}) {
        if (QStandardPaths::findExecutable(prog, paths).isEmpty())
            missing << prog;
    }
    if (missing.isEmpty()) return;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(T("extractor_missing_title"));
    box.setText(T("extractor_missing_body").arg(missing.join(QStringLiteral(", "))));
    box.addButton(QMessageBox::Ok);
    QPushButton *dontAsk = box.addButton(T("extractor_missing_dismiss"),
                                         QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == dontAsk)
        Settings::setSkipExtractorCheck(true);
}

void MainWindow::onSetModsDir()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, T("mods_dir_dialog_title"), m_modsDir);
    if (!dir.isEmpty()) {
        m_modsDir = dir;
        if (m_downloadQueue) m_downloadQueue->setModsDir(m_modsDir);
        m_profiles->setActiveModsDir(dir);
        statusBar()->showMessage(T("status_mods_dir_set").arg(m_modsDir), 3000);
    }
}

void MainWindow::onSetLanguage(const QString &language)
{
    if (language == Translator::currentLanguage()) return;
    Settings::setUiLanguage(language);
    ui::info(this, T("language_change_title"), T("language_change_body"));
}

// Resolve a per-user state file (modlist / loadorder / forbidden) to either
// the binary-adjacent location (dev / release builds) or AppDataLocation
// (AppImage, where applicationDirPath is a read-only mount). Existing file
// wins so users upgrading don't lose state. Under AppImage AppDataLocation
// is always used; the AppImage runtime sets $APPIMAGE so we key off that.
QString resolveUserStatePath(const QString &filename)
{
    const bool inAppImage = !qEnvironmentVariableIsEmpty("APPIMAGE");
    const QString data = QStandardPaths::writableLocation(
                             QStandardPaths::AppDataLocation) + "/" + filename;

    if (inAppImage) {
        QDir().mkpath(QFileInfo(data).absolutePath());
        return QFileInfo(data).absoluteFilePath();
    }

    const QString next = QCoreApplication::applicationDirPath() + "/" + filename;
    const QString up   = QCoreApplication::applicationDirPath() + "/../" + filename;
    QFileInfo binDir(QCoreApplication::applicationDirPath());

    // One-time migration: an older in-tree dev layout (binary under
    // bin/Release_Linux/, state files in bin/) wrote modlist/loadorder/
    // forbidden one directory up from the binary. The earlier "fall back
    // to up if it exists" lookup made that path sticky forever. Promote
    // any leftover up-file into the binary-adjacent location once, then
    // forget about up.
    if (QFile::exists(up) && !QFile::exists(next) && binDir.isWritable())
        QFile::rename(up, next);

    // Existing file wins so an in-place upgrade keeps the user's state.
    for (const QString &p : {next, data})
        if (QFile::exists(p)) return QFileInfo(p).absoluteFilePath();

    // Fresh-install default: save NEXT TO the binary if writable, otherwise
    // fall through to AppDataLocation so a /opt-style install still works.
    if (binDir.isWritable())
        return QFileInfo(next).absoluteFilePath();
    QDir().mkpath(QFileInfo(data).absolutePath());
    return QFileInfo(data).absoluteFilePath();
}

QString MainWindow::modlistPath() const
{
    // Per-modlist-profile filename so testing a Wabbajack in a separate
    // profile doesn't clobber the user's default modlist.  Falls back to
    // the legacy `modlist_<gameId>.txt` when no profile data is loaded
    // (first run before GameProfileRegistry::load() finishes its
    // migration, or the registry is somehow empty).
    if (m_profiles && !m_profiles->isEmpty()) {
        const QString fn = m_profiles->current().activeModlist().modlistFilename;
        if (!fn.isEmpty()) return resolveUserStatePath(fn);
    }
    return resolveUserStatePath("modlist_" +
        (m_profiles && !m_profiles->isEmpty() ? m_profiles->current().id
                                              : QString("morrowind")) + ".txt");
}

QString MainWindow::forbiddenModsPath() const
{
    // Per-game so the manager and install-time block only see the active game's
    // list (a Morrowind forbidden entry can't match an Oblivion install). Same
    // naming as modlist_<id>.txt; falls back to "morrowind" pre-registry.
    const QString id = (m_profiles && !m_profiles->isEmpty())
        ? m_profiles->current().id : QStringLiteral("morrowind");
    return resolveUserStatePath(QStringLiteral("forbidden_mods_") + id
                                + QStringLiteral(".txt"));
}

void MainWindow::reloadForbiddenMods()
{
    if (!m_forbidden) return;
    const QString id = (m_profiles && !m_profiles->isEmpty())
        ? m_profiles->current().id : QStringLiteral("morrowind");
    // Legacy single-file list folds into the morrowind game on first load.
    m_forbidden->reload(forbiddenModsPath(), id,
                        resolveUserStatePath(QStringLiteral("forbidden_mods.txt")));
}

QString MainWindow::loadOrderPath() const
{
    if (m_profiles && !m_profiles->isEmpty()) {
        const QString fn = m_profiles->current().activeModlist().loadOrderFilename;
        if (!fn.isEmpty()) return resolveUserStatePath(fn);
    }
    return resolveUserStatePath("loadorder_" +
        (m_profiles && !m_profiles->isEmpty() ? m_profiles->current().id
                                              : QString("morrowind")) + ".txt");
}






// Sort via LOOT (https://github.com/loot/loot)
//
// We delegate plugin sorting to LOOT because:
//   · community-curated masterlist covers cases pure MAST-based topo sort
//     can't (group ordering, late-loaders, known compat quirks);
//   · it already supports OpenMW (https://github.com/loot/loot - OpenMW
//     handler since 0.19.x).
//
// License posture: we never link libloot. LOOT is GPL-3.0; invoking its CLI
// as a subprocess does not make Nerevarine Organizer a derived work.
// Requirement: the user has LOOT installed separately (distro package,
// Flatpak, or built locally) and on PATH.




// The AppImage Qt-environment scrub (which keeps a foreign Qt child like
// LOOT or the OpenMW Launcher from loading our bundled Qt and dying with
// "QIBusPlatformInputContext: invalid portal bus") moved to
// subprocess::childEnvironment() (include/subprocess.h), so every external
// launch shares it and it can't be forgotten at a new call site.

// Maps our per-profile ID to the game-folder name LOOT uses on its CLI
// (`--game <name>`).  The slug now lives on each game's GameAdapter
// (src/game_adapters.cpp) so adding a new LOOT-supported game is a
// one-line override on the adapter, not an edit here too.  Empty
// string for profiles LOOT doesn't support keeps the politely-refuse
// behaviour intact.
QString lootGameFor(const QString &profileId)
{
    if (const GameAdapter *a = GameAdapterRegistry::find(profileId))
        return a->lootSlug();
    return {};
}


void MainWindow::scheduleSaveModList()
{
    // Restarts the 150ms single-shot.  Multiple rapid mutations within
    // the window collapse into one saveModList() call when the timer
    // fires.  Safe to call before m_saveModListTimer is constructed
    // (very early in MainWindow ctor) - just no-ops.
    if (m_saveModListTimer) m_saveModListTimer->start();
}

void MainWindow::onAsyncWriteFailed(const QString &filePath, const QString &reason)
{
    // Always lands on the UI thread (workers route through
    // QMetaObject::invokeMethod with Qt::QueuedConnection).  Show a
    // 7-second red banner so a silent disk-full / read-only-mount
    // doesn't get lost in log.txt.  Status bar gets the same message
    // as a fallback for users who dismissed the banner.
    qCWarning(logging::lcModList).nospace()
        << "async write failed: " << filePath << " (" << reason << ")";
    const QString fileName = QFileInfo(filePath).fileName();
    const QString msg = T("notify_write_failed").arg(fileName).arg(reason);
    if (m_notify) m_notify->show(msg, QStringLiteral("#a13838"));
    if (statusBar()) statusBar()->showMessage(msg, 8000);
}




// Drag-and-drop local archives onto the main window → install
//
// No download step: copy the dragged file into m_modsDir, make a
// placeholder row, then hand off to the same extractAndAdd pipeline that
// NXM downloads use (so FOMOD wizards, conflict scans, nexus-title rename
// etc. all fire the same way).  Refuses archives whose suffix we don't
// know how to extract.

bool isInstallableArchiveSuffix(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == "7z" || s == "zip" || s == "rar" || s == "fomod"
        || s == "tar" || s == "gz"  || s == "bz2" || s == "xz";
}

bool isImportFileSuffix(const QString &path)
{
    const QFileInfo fi(path);
    // .wabbajack files are WJ modlists; modlist.txt is MO2's modlist file.
    const QString suf = fi.suffix().toLower();
    return suf == QLatin1String("wabbajack")
        || suf == QLatin1String("txt");
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl &u : event->mimeData()->urls()) {
        if (!u.isLocalFile()) continue;
        const QString local = u.toLocalFile();
        if (isInstallableArchiveSuffix(local) || isImportFileSuffix(local)) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls()) return;

    // Collect the archive paths first; DO NOT call installLocalArchive
    // synchronously from the drop handler.  installLocalArchive mutates
    // m_modList (addItem + scrollToItem + saveModList → syncGameConfig,
    // scan scheduling, etc.) - and m_modList is the exact widget whose
    // dropEvent is still unwinding.  That re-entrancy has crashed the app
    // on drag-drop of e.g. the True Type Fonts archive.
    //
    // Defer via QueuedConnection so the drop fully completes, the list
    // widget restores its internal state, and THEN the install pipeline
    // runs on the next event-loop tick.
    QStringList toInstall;
    QStringList toImport;
    for (const QUrl &u : event->mimeData()->urls()) {
        if (!u.isLocalFile()) continue;
        QString local = u.toLocalFile();
        if (isInstallableArchiveSuffix(local))
            toInstall << local;
        else if (isImportFileSuffix(local))
            toImport << local;
    }
    event->acceptProposedAction();

    QPointer<MainWindow> self(this);
    if (!toInstall.isEmpty()) {
        QMetaObject::invokeMethod(this, [self, toInstall]{
            if (!self) return;
            for (const QString &p : toInstall) self->installLocalArchive(p);
        }, Qt::QueuedConnection);
    }
    for (const QString &p : toImport) {
        QMetaObject::invokeMethod(this, [self, p]{
            if (!self) return;
            self->handleDroppedImportFile(p);
        }, Qt::QueuedConnection);
    }
}




// Modlist Summary
//
// Shows total disk footprint of the current modlist and the OpenMW binary
// location. Linux-first (OpenMW is the main Morrowind engine on Linux), but
// also knows Windows install locations.



// Move the entire mod library to another location. Refuses if downloads
// are active, free space is under 1.10x the footprint, dest is inside src
// or vice versa, or a same-named subfolder exists at the destination.
// Same-FS uses QDir::rename; cross-FS streams per-file with size verify.
// Saves the modlist incrementally so a kill mid-move loses no state.
// Clears the undo stack afterward.
void MainWindow::onMoveModsDir()
{
    // -- Pre-flight: downloads must be idle ---
    if (!m_downloadQueue->isEmpty()) {
        ui::warn(this, T("move_mods_title"), T("move_mods_err_downloads"));
        return;
    }

    // Refresh sizes so the "X GB will be moved" figure is honest.
    m_scans->scheduleSizeScan();

    // -- Gather the set of mod folders to move ---
    struct Candidate {
        QListWidgetItem *item;
        QString oldPath;
        QString folderName;
        qint64  sizeBytes;
    };
    QList<Candidate> candidates;
    qint64 totalBytes = 0;

    const QString modsRoot = QFileInfo(m_modsDir).absoluteFilePath();

    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (it->data(ModRole::InstallStatus).toInt() != 1)           continue;
        QString path = it->data(ModRole::ModPath).toString();
        if (path.isEmpty() || !QFileInfo(path).isDir())              continue;

        QString abs = QFileInfo(path).absoluteFilePath();
        // Only move entries that actually live under the active mods dir.
        // Mods pointing somewhere else (user symlinked, manually added from
        // /opt/…, etc.) are left alone to avoid surprises.
        if (!abs.startsWith(modsRoot + "/") && abs != modsRoot)      continue;

        QString folderName = QDir(modsRoot).relativeFilePath(abs);
        // We only know how to move direct children of modsRoot; anything
        // nested (moved via repair, for instance) we skip with a warning.
        if (folderName.contains('/')) continue;

        Candidate c;
        c.item       = it;
        c.oldPath    = abs;
        c.folderName = folderName;
        c.sizeBytes  = it->data(ModRole::ModSize).toLongLong();
        if (c.sizeBytes < 0) c.sizeBytes = 0;
        candidates.append(c);
        totalBytes += c.sizeBytes;
    }

    if (candidates.isEmpty()) {
        ui::info(this, T("move_mods_title"), T("move_mods_err_nothing"));
        return;
    }

    // -- Ask for the destination ---
    QString dest = QFileDialog::getExistingDirectory(
        this, T("move_mods_pick_dir"),
        QFileInfo(m_modsDir).absolutePath());
    if (dest.isEmpty()) return;
    dest = QFileInfo(dest).absoluteFilePath();

    // -- Destination validation ---
    if (!QFileInfo(dest).isDir()) {
        ui::warn(this, T("move_mods_title"), T("move_mods_err_dest_not_dir").arg(dest));
        return;
    }
    if (dest == modsRoot) {
        ui::warn(this, T("move_mods_title"), T("move_mods_err_dest_same"));
        return;
    }
    // Reject nesting in either direction - catching someone picking a sub
    // folder of the current mods dir (or accidentally a parent that contains
    // the current mods dir).
    if (dest.startsWith(modsRoot + "/") || modsRoot.startsWith(dest + "/")) {
        ui::warn(this, T("move_mods_title"), T("move_mods_err_dest_nested"));
        return;
    }

    // Writable test: create + delete a stamp file.
    {
        QString stamp = QDir(dest).filePath(".nerev_write_test");
        QFile probe(stamp);
        if (!probe.open(QIODevice::WriteOnly)) {
            ui::warn(this, T("move_mods_title"), T("move_mods_err_dest_not_writable").arg(dest));
            return;
        }
        probe.close();
        QFile::remove(stamp);
    }

    // -- Free-space check (× 1.10 safety margin) ---
    QStorageInfo dstStorage(dest);
    qint64 required = qint64(double(totalBytes) * 1.10);
    if (dstStorage.bytesAvailable() < required) {
        auto fmt = [](qint64 b) {
            const double GB = 1024.0 * 1024.0 * 1024.0;
            return QString::number(b / GB, 'f', 2) + " GB";
        };
        ui::warn(this, T("move_mods_title"), T("move_mods_err_no_space")
                .arg(fmt(dstStorage.bytesAvailable()))
                .arg(fmt(required)));
        return;
    }

    // -- Collision check ---
    QStringList collisions;
    for (const Candidate &c : candidates) {
        if (QFileInfo::exists(QDir(dest).filePath(c.folderName)))
            collisions << c.folderName;
    }
    if (!collisions.isEmpty()) {
        QString list = collisions.mid(0, 10).join("\n  • ");
        if (collisions.size() > 10)
            list += "\n  … (+" + QString::number(collisions.size() - 10) + ")";
        ui::warn(this, T("move_mods_title"), T("move_mods_err_collision").arg(list));
        return;
    }

    // -- Final confirmation ---
    auto fmtBytes = [](qint64 b) {
        const double MB = 1024.0 * 1024.0;
        const double GB = MB * 1024.0;
        if (b >= GB) return QString::number(b / GB, 'f', 2) + " GB";
        return QString::number(b / MB, 'f', 1) + " MB";
    };
    QMessageBox confirm(this);
    confirm.setWindowTitle(T("move_mods_title"));
    confirm.setIcon(QMessageBox::Warning);
    confirm.setText(T("move_mods_confirm_text")
                        .arg(candidates.size())
                        .arg(fmtBytes(totalBytes))
                        .arg(dest));
    confirm.setInformativeText(T("move_mods_confirm_info"));
    auto *yes = confirm.addButton(T("move_mods_confirm_yes"),
                                   QMessageBox::DestructiveRole);
    confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.exec();
    if (confirm.clickedButton() != yes) return;

    // -- Do the move ---
    QProgressDialog progress(T("move_mods_progress").arg(candidates.size()),
                              T("move_mods_cancel"), 0, candidates.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    int movedOk = 0;
    QStringList failures;

    for (int i = 0; i < candidates.size(); ++i) {
        if (progress.wasCanceled()) break;
        const Candidate &c = candidates[i];

        progress.setLabelText(T("move_mods_progress_detail")
                                .arg(i + 1)
                                .arg(candidates.size())
                                .arg(c.folderName));
        QCoreApplication::processEvents();

        QString newPath = QDir(dest).filePath(c.folderName);

        // Fast path: same-filesystem atomic rename.  If source and
        // destination live on different filesystems, QDir::rename fails
        // with EXDEV and we fall back to a streaming copy+verify+rmtree.
        bool ok = QDir().rename(c.oldPath, newPath);
        QString err;
        if (!ok) {
            auto res = safefs::copyTreeVerified(c.oldPath, newPath,
                    [&]{ return progress.wasCanceled(); });
            if (res) {
                if (safefs::forceRemoveRecursively(c.oldPath)) {
                    ok = true;
                } else {
                    // Destination is a verified copy, but the source dir
                    // can't be removed (locked file, permissions…).  Roll
                    // back the copy so we're never left with two live
                    // copies of the same mod; caller can retry manually.
                    safefs::forceRemoveRecursively(newPath);
                    err = QStringLiteral("copied ok, but could not remove original");
                }
            } else {
                err = res.error();
            }
        }

        if (!ok) {
            failures.append(c.folderName
                            + (err.isEmpty() ? QString() : QStringLiteral(" - ") + err));
        } else {
            ++movedOk;
            c.item->setData(ModRole::ModPath, newPath);
            c.item->setToolTip(newPath);
            // Incremental save: every success is durable even if the process
            // dies halfway through.
            saveModList();
        }
        progress.setValue(i + 1);
    }
    progress.setValue(candidates.size());

    // -- Flip mods dir pointer if anything moved ---
    if (movedOk > 0) {
        m_modsDir = dest;
        if (m_downloadQueue) m_downloadQueue->setModsDir(m_modsDir);
        m_profiles->setActiveModsDir(dest);
        saveModList();
        syncGameConfig();
        scanMissingMasters();
        m_scans->scheduleSizeScan();
        // File moves can't be undone via the snapshot stack; clear it so the
        // user isn't tempted into a half-reverted state.
        m_undoStack->clear();
    }

    // -- Report ---
    QMessageBox summary(this);
    summary.setWindowTitle(T("move_mods_title"));
    summary.setIcon(failures.isEmpty() ? QMessageBox::Information
                                        : QMessageBox::Warning);
    QString body = T("move_mods_done_ok").arg(movedOk);
    if (!failures.isEmpty()) {
        QString flist = failures.mid(0, 15).join("\n  • ");
        if (failures.size() > 15)
            flist += "\n  … (+" + QString::number(failures.size() - 15) + ")";
        body += "\n\n" + T("move_mods_done_failures")
                            .arg(failures.size()) + "\n  • " + flist;
    }
    summary.setText(body);
    summary.exec();
}


// Consolidate any mods physically living OUTSIDE the active profile's
// modsDir into it.  Runs after a clone (the cloned profile inherits the
// source's mod paths, so its mods sit in the SOURCE profile's modsDir
// until consolidated).  The destination is forced to m_modsDir so two
// profiles can never end up sharing the same physical mod folder; the
// user already chose this dir via ensureModsDirForActiveProfile().
//
// Same flow as onMoveModsDir with one inverted filter (outside-of instead of
// inside-of) and no destination picker. Free-space, collision, and confirm
// gates stay identical on both paths.
void MainWindow::onConsolidateModsIntoActiveProfile()
{
    if (!m_downloadQueue->isEmpty()) {
        ui::warn(this, T("consolidate_mods_title"), T("move_mods_err_downloads"));
        return;
    }

    if (!ensureModsDirForActiveProfile()) return;
    if (m_modsDir.isEmpty()) return;

    m_scans->scheduleSizeScan();

    struct Candidate {
        QListWidgetItem *item;
        QString oldPath;
        QString folderName;
        qint64  sizeBytes;
    };
    QList<Candidate> candidates;
    qint64 totalBytes = 0;

    const QString modsRoot = QFileInfo(m_modsDir).absoluteFilePath();

    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (it->data(ModRole::InstallStatus).toInt() != 1)           continue;
        QString path = it->data(ModRole::ModPath).toString();
        if (path.isEmpty() || !QFileInfo(path).isDir())              continue;

        const QString abs = QFileInfo(path).absoluteFilePath();
        // Skip mods that are already inside the active profile's modsDir
        // (or AT modsRoot, though that's an unusual layout).
        if (abs == modsRoot || abs.startsWith(modsRoot + "/"))       continue;

        Candidate c;
        c.item       = it;
        c.oldPath    = abs;
        c.folderName = QFileInfo(abs).fileName();
        c.sizeBytes  = it->data(ModRole::ModSize).toLongLong();
        if (c.sizeBytes < 0) c.sizeBytes = 0;
        candidates.append(c);
        totalBytes += c.sizeBytes;
    }

    if (candidates.isEmpty()) {
        ui::info(this, T("consolidate_mods_title"), T("consolidate_mods_nothing"));
        return;
    }

    // Free-space check (× 1.10 safety margin).  Cross-FS moves stream
    // through copy+delete, so we need temporary headroom for the copy.
    QStorageInfo dstStorage(modsRoot);
    qint64 required = qint64(double(totalBytes) * 1.10);
    if (dstStorage.bytesAvailable() < required) {
        auto fmt = [](qint64 b) {
            const double GB = 1024.0 * 1024.0 * 1024.0;
            return QString::number(b / GB, 'f', 2) + " GB";
        };
        ui::warn(this, T("consolidate_mods_title"), T("move_mods_err_no_space")
                .arg(fmt(dstStorage.bytesAvailable()))
                .arg(fmt(required)));
        return;
    }

    // Collision: a folder of the same name already living in m_modsDir
    // (e.g. a different mod with a colliding folder name from a fresh
    // install in the active profile).  Bail rather than pick a winner.
    QStringList collisions;
    for (const Candidate &c : candidates) {
        if (QFileInfo::exists(QDir(modsRoot).filePath(c.folderName)))
            collisions << c.folderName;
    }
    if (!collisions.isEmpty()) {
        QString list = collisions.mid(0, 10).join("\n  • ");
        if (collisions.size() > 10)
            list += "\n  … (+" + QString::number(collisions.size() - 10) + ")";
        ui::warn(this, T("consolidate_mods_title"), T("move_mods_err_collision").arg(list));
        return;
    }

    auto fmtBytes = [](qint64 b) {
        const double MB = 1024.0 * 1024.0;
        const double GB = MB * 1024.0;
        if (b >= GB) return QString::number(b / GB, 'f', 2) + " GB";
        return QString::number(b / MB, 'f', 1) + " MB";
    };

    // Confirmation: list the source roots so the user sees WHICH external
    // dirs are about to be drained.  Showing the source tells them what
    // "the other profile's modsDir will keep working" looks like.
    QSet<QString> sourceRoots;
    for (const Candidate &c : candidates)
        sourceRoots.insert(QFileInfo(c.oldPath).absolutePath());
    QStringList rootsList(sourceRoots.begin(), sourceRoots.end());
    rootsList.sort();
    const QString rootsText = rootsList.mid(0, 5).join("\n  • ")
        + (rootsList.size() > 5
              ? QStringLiteral("\n  … (+%1)").arg(rootsList.size() - 5)
              : QString());

    QMessageBox confirm(this);
    confirm.setWindowTitle(T("consolidate_mods_title"));
    confirm.setIcon(QMessageBox::Warning);
    confirm.setText(T("consolidate_mods_confirm_text")
                        .arg(candidates.size())
                        .arg(fmtBytes(totalBytes))
                        .arg(QDir::toNativeSeparators(modsRoot)));
    confirm.setInformativeText(T("consolidate_mods_confirm_info").arg(rootsText));
    auto *yes = confirm.addButton(T("move_mods_confirm_yes"),
                                   QMessageBox::DestructiveRole);
    confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.exec();
    if (confirm.clickedButton() != yes) return;

    QProgressDialog progress(T("move_mods_progress").arg(candidates.size()),
                              T("move_mods_cancel"), 0, candidates.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    int movedOk = 0;
    QStringList failures;

    for (int i = 0; i < candidates.size(); ++i) {
        if (progress.wasCanceled()) break;
        const Candidate &c = candidates[i];

        progress.setLabelText(T("move_mods_progress_detail")
                                .arg(i + 1)
                                .arg(candidates.size())
                                .arg(c.folderName));
        QCoreApplication::processEvents();

        const QString newPath = QDir(modsRoot).filePath(c.folderName);

        // Same-FS atomic rename when possible; fall back to streaming
        // copy+verify+rmtree on cross-FS. Same as onMoveModsDir's inner loop -
        // keep the two in sync.
        bool ok = QDir().rename(c.oldPath, newPath);
        QString err;
        if (!ok) {
            auto res = safefs::copyTreeVerified(c.oldPath, newPath,
                    [&]{ return progress.wasCanceled(); });
            if (res) {
                if (safefs::forceRemoveRecursively(c.oldPath)) {
                    ok = true;
                } else {
                    safefs::forceRemoveRecursively(newPath);
                    err = QStringLiteral("copied ok, but could not remove original");
                }
            } else {
                err = res.error();
            }
        }

        if (!ok) {
            failures.append(c.folderName
                            + (err.isEmpty() ? QString() : QStringLiteral(" - ") + err));
        } else {
            ++movedOk;
            c.item->setData(ModRole::ModPath, newPath);
            c.item->setToolTip(newPath);
            saveModList();
        }
        progress.setValue(i + 1);
    }
    progress.setValue(candidates.size());

    if (movedOk > 0) {
        saveModList();
        syncGameConfig();
        scanMissingMasters();
        m_scans->scheduleSizeScan();
        m_undoStack->clear();
    }

    QMessageBox summary(this);
    summary.setWindowTitle(T("consolidate_mods_title"));
    summary.setIcon(failures.isEmpty() ? QMessageBox::Information
                                        : QMessageBox::Warning);
    QString body = T("move_mods_done_ok").arg(movedOk);
    if (!failures.isEmpty()) {
        QString flist = failures.mid(0, 15).join("\n  • ");
        if (failures.size() > 15)
            flist += "\n  … (+" + QString::number(failures.size() - 15) + ")";
        body += "\n\n" + T("move_mods_done_failures")
                            .arg(failures.size()) + "\n  • " + flist;
    }
    summary.setText(body);
    summary.exec();
}


// File Conflict Inspector - MO2-style overwrite view for OpenMW's VFS.
//
// Walks every enabled+installed mod in modlist order, resolving its data=
// roots the same way syncOpenMWConfig does (plugin subdirs if any, else
// resource roots as fallback).  For each root, enumerates files recursively
// AND reads the TOC of any .bsa it finds (BSA-packed assets participate in
// OpenMW's VFS precedence, so the inspector must see them too - otherwise
// a BSA retexture silently beats a loose-file mod and the user has no way
// to tell why).  Any relPath provided by more than one mod is a shadow:
// OpenMW's VFS is LATER-WINS, so the mod that appears LAST in the modlist
// is the file that actually loads; every earlier entry is overwritten.
//
// Scan runs on a worker thread (QtConcurrent::run): a heavily modded setup can
// have 100k+ loose files + a dozen multi-GB BSAs. A QProgressDialog keeps the
// UI responsive with a Cancel button; an atomic<bool> polled in the scan loop
// makes cancellation prompt.
void MainWindow::onInspectConflicts()
{
    // --- Phase 1: snapshot mod list on the UI thread ---
    //
    // The worker must not touch m_modList or QListWidgetItem - those aren't
    // thread-safe.  Build a POD vector here, hand it to QtConcurrent::run by
    // value.
    QList<ConflictScanInput> snapshot;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->checkState() != Qt::Checked) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 1) continue;

        ConflictScanInput e;
        e.modPath  = item->data(ModRole::ModPath).toString();
        e.modLabel = item->data(ModRole::CustomName).toString();
        if (e.modLabel.isEmpty()) e.modLabel = item->text();
        snapshot.append(e);
    }

    // Phase 2+3 (scan off-thread behind a progress dialog, then render the
    // provider tree) live in conflict_inspector so this TU keeps only the
    // snapshot that has to happen on the UI thread.
    conflict_inspector::show(this, snapshot);
}















// (defined later, near onLaunchGame)


// OpenMW Log Triage - parse ~/.config/openmw/openmw.log and surface known
// error shapes grouped by the mod owning the offending plugin. Does what you'd
// do by hand: read the log, spot "asks for parent file" / "Failed loading
// X.esp", chase X.esp back to the mod. The triage logic lives in log_triage.cpp;
// this slot stays thin: read file, build TriageMod list, render.
void MainWindow::onTriageOpenMWLog()
{
    const QString logPath = QDir::homePath() + "/.config/openmw/openmw.log";
    QFile lf(logPath);
    if (!lf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ui::info(this, T("log_triage_title"), T("log_triage_no_log").arg(logPath));
        return;
    }
    const QString logText = QString::fromUtf8(lf.readAll());
    lf.close();

    // Build the plugin-owner index from the current modlist - we only need
    // the display name and the filenames each mod ships, so the walk is
    // trivially cheap compared to onInspectOpenMWSetup's per-mod FS scan.
    static const QStringList contentExts{".esp", ".esm", ".omwaddon", ".omwscripts"};
    QList<openmw::TriageMod> triageMods;
    triageMods.reserve(m_modList->count());
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 1) continue;

        openmw::TriageMod tm;
        tm.displayName = item->data(ModRole::CustomName).toString();
        if (tm.displayName.isEmpty()) tm.displayName = item->text();

        const QString modPath = item->data(ModRole::ModPath).toString();
        for (const auto &p : collectDataFolders(modPath, contentExts))
            for (const QString &f : p.second) tm.plugins << f;

        if (!tm.plugins.isEmpty()) triageMods << tm;
    }

    const openmw::LogTriageReport report =
        openmw::triageOpenMWLog(logText, triageMods);

    // Grouping + rendering live in log_triage_dialog; this slot keeps only
    // the parts that need MainWindow state (reading the log, indexing the
    // mod list into TriageMod rows).
    openmw::showTriageDialog(this, report, logPath);
}



// --- Temporary "view sort" (Sort by Size / Date) ------------------------------
// These sorts reorder only what's on screen; the saved load order is preserved.
// Each row's saved position is stamped into ModRole::SortAnchor when the lens
// opens, and every persistence walk (saveModList, syncOpenMWConfig,
// reconcileLoadOrder, exportModList) iterates rowOrderForPersist() so the disk
// state always reflects the saved order, not the sorted display.

QList<int> MainWindow::rowOrderForPersist() const
{
    // Source of truth is the per-row SortAnchor stamp, NOT m_viewSortActive, so
    // persistence stays correct even if the flag is briefly stale (e.g. a list
    // rebuild after a profile switch left it set). No stamps present -> identity,
    // the common case with no view sort active.
    const int n = m_modList->count();
    QList<qint64> anchors;
    anchors.reserve(n);
    bool anyStamped = false;
    for (int i = 0; i < n; ++i) {
        const QVariant v = m_modList->item(i)->data(ModRole::SortAnchor);
        if (v.isValid()) { anchors.append(v.toLongLong()); anyStamped = true; }
        else             { anchors.append(-1); }
    }
    if (!anyStamped) {
        QList<int> identity;
        identity.reserve(n);
        for (int i = 0; i < n; ++i) identity.append(i);
        return identity;
    }
    return canonicalOrderFromAnchors(anchors);
}


QList<ModEntry> MainWindow::snapshotEntriesForPersist() const
{
    const QList<int> order = rowOrderForPersist();
    QList<ModEntry> out;
    out.reserve(order.size());
    for (int idx : order)
        if (const auto *it = m_modList->item(idx))
            out.append(ModEntry::fromItem(it));
    return out;
}




void MainWindow::checkpointBeforeSort()
{
    if (!m_backups) return;
    // Capture the current in-memory canonical order (snapshotEntriesForPersist
    // walks rows in saved order - identity when no view sort is active, and
    // un-sorted via rowOrderForPersist when a lens is). Serializing in memory
    // keeps this cheap and race-free; deliberately NOT saveModList(), which also
    // resyncs openmw.cfg/launcher.cfg and runs the master/dependency scans.
    const QByteArray content =
        modlist_serializer::serializeModlist(snapshotEntriesForPersist()).toUtf8();
    m_backups->writePreSortCheckpoint(content);
}



// readTes3Masters + collectDataFolders - moved to include/pluginparser.h
// (plugins::readTes3Masters, plugins::collectDataFolders). See the `using`
// declarations at the top of this file.




void MainWindow::onMissingMastersScanned(
    const QHash<QString, QPair<bool, QStringList>> &byModPath)
{
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        const QString path = item->data(ModRole::ModPath).toString();
        auto it = byModPath.constFind(path);
        if (it == byModPath.constEnd()) {
            item->setData(ModRole::HasMissingMaster, false);
            item->setData(ModRole::MissingMasters, QStringList());
            continue;
        }
        item->setData(ModRole::HasMissingMaster, it.value().first);
        item->setData(ModRole::MissingMasters,   it.value().second);
    }
    m_modList->viewport()->update();
}



// Launch OpenMW



bool MainWindow::refuseLaunchIfRebootPending()
{
    return launch_warnings::refuseIfRebootPending(this);
}



// Launch non-Morrowind games (via Steam URL or auto-detected executable)
















// Asks the user to confirm replacing the current mod list.
// If any mod folders exist on disk, also offers to delete them.
// Returns true if the import should proceed (caller should then
// populate the list fresh); false if the user cancelled.
bool MainWindow::confirmReplaceModList()
{
    if (m_modList->count() == 0) return true;

    if (!ui::confirm(this, T("import_title"), T("import_confirm")))
        return false;

    // Collect paths of installed mods that actually exist on disk.
    QStringList installedPaths;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        const QString p = item->data(ModRole::ModPath).toString();
        if (!p.isEmpty() && QDir(p).exists()) installedPaths << p;
    }

    if (!installedPaths.isEmpty()) {
        auto *dlg = new QMessageBox(this);
        dlg->setWindowTitle(T("import_title"));
        dlg->setText(T("import_delete_from_disk_prompt").arg(installedPaths.size()));
        dlg->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        dlg->setDefaultButton(QMessageBox::No);
        if (dlg->exec() == QMessageBox::Yes) {
            for (const QString &p : installedPaths)
                QDir(p).removeRecursively();
        }
    }

    return true;
}

// Returns the common parent directory stored in a Nerevarine modlist.txt, or an
// empty string if the paths already exist on this machine (no remap needed) or
// if they are inconsistent (can't determine a single base).
QString MainWindow::detectModsBaseFromFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&f);
    QString detectedBase;
    bool allMissing = true;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.size() < 2) continue;
        if (line[0] != '+' && line[0] != '-') continue;
        const QStringList parts = line.mid(2).split('\t');
        if (parts.isEmpty()) continue;
        const QString modPath = parts[0];
        if (!modPath.startsWith('/')) continue; // not an absolute path

        if (QDir(modPath).exists()) {
            allMissing = false;
            continue; // this path works locally, don't count it as foreign
        }

        const QString parent = QFileInfo(modPath).absolutePath();
        if (detectedBase.isEmpty()) {
            detectedBase = parent;
        } else if (detectedBase != parent) {
            // Paths have different parents - fall back to longest common prefix
            int i = 0;
            while (i < detectedBase.size() && i < parent.size()
                   && detectedBase[i] == parent[i])
                ++i;
            detectedBase = detectedBase.left(i);
            if (detectedBase.isEmpty()) return {};
        }
    }
    // If every path exists locally, no remap is needed.
    if (!allMissing && detectedBase.isEmpty()) return {};
    return detectedBase;
}





void MainWindow::onNewModList()
{
    const QString currentPath = modlistPath();
    const QString msg =
        T("menu_new_modlist_confirm_body").arg(QFileInfo(currentPath).fileName());

    if (!ui::confirm(this, T("menu_new_modlist_confirm_title"), msg))
        return;

    QFile::remove(currentPath);
    clearViewSortState();
    m_modList->clear();
    updateModCount();
    updateSectionCounts();
    statusBar()->showMessage(T("status_new_modlist"), 3000);
}

// Sort by date added


// Animation tick for the "installing" spinner

void MainWindow::onAnimTick()
{
    m_animFrame = (m_animFrame + 1) % 10;
    bool anyInstalling = false;
    for (int i = 0; i < m_modList->count(); ++i) {
        if (m_modList->item(i)->data(ModRole::InstallStatus).toInt() == 2) {
            anyInstalling = true;
            break;
        }
    }
    if (anyInstalling) {
        m_delegate->setAnimFrame(m_animFrame);
        m_modList->viewport()->update();
    }
}

// Game profile management

// Built-in games the app knows about out of the box
// -- Featured modlists ---
// Each entry appears in the "Featured Modlists" toolbar dropdown when the
// matching game is active.  Set url to the modlist's web page; leave it empty
// to use a placeholder message until the real URL is filled in.
struct FeaturedModlist { QString name; QString gameId; QString url; };
static const QList<FeaturedModlist> kFeaturedModlists = {
    // Featured modlists are on hold - the whole dropdown currently shows a
    // "work in progress" message instead of routing to the individual entries
    // below. Leaving the data here (commented) so when the feature is
    // reinstated the list is ready to re-populate.
    // TODO: replace the empty string with the actual URL once published
    // {"calazzo OpenMW modlist", "morrowind", ""},
};

// First-run wizard chooser data.  The set of "builtin" games -- those
// the wizard offers on first launch -- is now derived from the
// GameAdapter registry (each adapter sets builtin() = true for its
// class), so the kBuiltinGames table has no separate existence.
static QList<firstrun::GameChoice> builtinGameChoices()
{
    QList<firstrun::GameChoice> out;
    const auto adapters = GameAdapterRegistry::builtin();
    out.reserve(adapters.size());
    for (const GameAdapter *a : adapters)
        out.append({a->id(), a->displayName(), a->defaultModsDirName()});
    return out;
}

GameProfile&       MainWindow::currentProfile()       { return m_profiles->current(); }
const GameProfile& MainWindow::currentProfile() const { return m_profiles->current(); }



QString MainWindow::currentProfileKey() const
{
    if (!m_profiles || m_profiles->isEmpty()) return {};
    const GameProfile &gp = m_profiles->current();
    return gp.id + QStringLiteral("__") + gp.activeModlist().name;
}

QString MainWindow::modlistPathFor(const QString &profileKey) const
{
    if (profileKey.isEmpty() || !m_profiles || m_profiles->isEmpty()) return {};
    // Walk every game's modlist profiles looking for the matching key.
    // Profiles outside the active game still have well-defined modlist
    // filenames, so this works even when the user is on a different game.
    for (const GameProfile &gp : m_profiles->games()) {
        for (const ModlistProfile &mp : gp.modlistProfiles) {
            const QString k = gp.id + QStringLiteral("__") + mp.name;
            if (k != profileKey) continue;
            if (!mp.modlistFilename.isEmpty())
                return resolveUserStatePath(mp.modlistFilename);
            return resolveUserStatePath(QStringLiteral("modlist_") + gp.id +
                                        QStringLiteral(".txt"));
        }
    }
    return {};
}

QListWidgetItem *MainWindow::findPlaceholderByToken(
    const QUuid &installToken, QString *outProfileKey) const
{
    if (outProfileKey) outProfileKey->clear();
    if (installToken.isNull() || !m_modList) return nullptr;
    // Active list first - covers the common "user stayed on this profile
    // through the install" case in O(N) over the visible rows.
    for (int i = 0; i < m_modList->count(); ++i) {
        QListWidgetItem *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (it->data(ModRole::InstallToken).toUuid() == installToken)
            return it;
    }
    // Then the parking lot - rows here belong to other profiles whose
    // modlist file is the persistence target for this completion.
    for (auto it = m_strandedInstalls.constBegin();
         it != m_strandedInstalls.constEnd(); ++it) {
        for (QListWidgetItem *si : it.value()) {
            if (si->data(ModRole::InstallToken).toUuid() == installToken) {
                if (outProfileKey) *outProfileKey = it.key();
                return si;
            }
        }
    }
    return nullptr;
}


bool MainWindow::shareModIntoProfile(const ModEntry &source,
                                     const QString &targetProfileKey,
                                     bool copyConfig)
{
    const QString file = modlistPathFor(targetProfileKey);
    if (file.isEmpty()) return false;

    QList<ModEntry> entries;
    {
        QFile in(file);
        if (in.open(QIODevice::ReadOnly | QIODevice::Text))
            entries = modlist_serializer::parseModlist(
                          QString::fromUtf8(in.readAll()));
        // Missing file is fine - a fresh one is written with just this row.
    }

    const ModEntry shared = mod_sharing::makeSharedRow(source, copyConfig);
    auto result = mod_sharing::appendSharedRow(std::move(entries), shared);

    const QString targetName = targetProfileKey.section(QStringLiteral("__"), 1, -1);
    if (!result.added) {
        // Idempotent: the target already references this exact mod.
        statusBar()->showMessage(T("share_already_present").arg(targetName), 5000);
        return true;
    }

    // Synchronous write to a NON-active profile's file (same rationale as
    // saveModListFor); failures route through the shared async-write banner.
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        onAsyncWriteFailed(file, out.errorString());
        return false;
    }
    QTextStream ts(&out);
    ts << modlist_serializer::serializeModlist(result.entries);
    out.close();

    statusBar()->showMessage(T("share_done").arg(targetName), 5000);
    return true;
}

bool MainWindow::modPathReferencedByOtherProfile(const QString &cleanPath) const
{
    if (cleanPath.isEmpty() || !m_profiles || m_profiles->isEmpty()) return false;
    const QString activeKey = currentProfileKey();

    // Parse every OTHER profile's modlist file and let the pure scan decide.
    // Only called on delete paths (cold), so the per-profile file reads are fine.
    QList<QPair<QString, QList<ModEntry>>> others;
    for (const GameProfile &gp : m_profiles->games()) {
        for (const ModlistProfile &mp : gp.modlistProfiles) {
            const QString key = gp.id + QStringLiteral("__") + mp.name;
            if (key == activeKey) continue;
            const QString file = modlistPathFor(key);
            if (file.isEmpty()) continue;
            QFile in(file);
            if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            others.append({ key, modlist_serializer::parseModlist(
                                     QString::fromUtf8(in.readAll())) });
        }
    }
    return mod_sharing::pathReferencedIn(mod_sharing::cleanModPath(cleanPath), others);
}

void MainWindow::strandInflightInstalls()
{
    if (!m_modList) return;
    const QString key = currentProfileKey();
    if (key.isEmpty()) return;

    // Walk back-to-front so takeItem() index shifts don't break iteration.
    // Items go into the parking lot in their original visual order (we
    // prepend) so restoreStrandedInstalls can put them back at matching
    // rows without sorting them later.
    QList<QListWidgetItem*> stranded;
    for (int i = m_modList->count() - 1; i >= 0; --i) {
        QListWidgetItem *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (it->data(ModRole::InstallStatus).toInt() != 2)           continue;
        stranded.prepend(m_modList->takeItem(i));
    }
    if (!stranded.isEmpty())
        m_strandedInstalls[key] += stranded;
}

void MainWindow::restoreStrandedInstalls()
{
    const QString key = currentProfileKey();
    if (key.isEmpty()) return;
    const QList<QListWidgetItem*> stranded = m_strandedInstalls.take(key);
    if (stranded.isEmpty()) return;

    // The newly-loaded modlist usually has a status=2 row mirroring the
    // stranded install (saveModList wrote it before the switch).  Match
    // by NexusUrl when present (the most stable identifier across saves)
    // and fall back to display text otherwise. On a match the loaded row is
    // dropped and the stranded one (still holding live in-flight extraction
    // state) takes its place. No match means the stranded install pre-dated
    // saveModList; append it so the install isn't lost.
    for (QListWidgetItem *si : stranded) {
        const QString sUrl  = si->data(ModRole::NexusUrl).toString();
        const QString sName = si->text();
        int matchRow = -1;
        for (int i = 0; i < m_modList->count(); ++i) {
            QListWidgetItem *it = m_modList->item(i);
            if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
            const QString iUrl = it->data(ModRole::NexusUrl).toString();
            if (!sUrl.isEmpty() && !iUrl.isEmpty() && sUrl == iUrl) {
                matchRow = i; break;
            }
            if (sUrl.isEmpty() && it->text() == sName) {
                matchRow = i; break;
            }
        }
        if (matchRow >= 0) {
            delete m_modList->takeItem(matchRow);
            m_modList->insertItem(matchRow, si);
        } else {
            m_modList->addItem(si);
        }
    }
}


void MainWindow::switchToModlistProfile(int idx)
{
    if (m_profiles->isEmpty()) return;
    GameProfile &gp = m_profiles->current();
    if (idx < 0 || idx >= gp.modlistProfiles.size() || idx == gp.activeModlistIdx) return;

    // Persist current modlist + load order to the OLD profile's files
    // BEFORE flipping the active index - paths are derived from the active
    // profile, so the writes have to happen first.
    saveModList();
    saveLoadOrder();

    // Same stranding dance as switchToGame: keep in-flight placeholders
    // alive across the clear() so background extractions don't get
    // silently aborted by a profile switch.
    strandInflightInstalls();

    m_profiles->setActiveModlistIndex(idx);
    applyCurrentProfileToMirrors();

    // Reload from the new profile's state files.
    m_modList->clear();
    loadModList();
    restoreStrandedInstalls();
    // openmw.cfg now needs to reflect a different mod set on disk; sync.
    syncGameConfig();
    updateProfileButton();

    statusBar()->showMessage(
        T("status_switched_profile").arg(gp.activeModlist().name), 3000);
}

bool MainWindow::ensureModsDirForActiveProfile()
{
    if (!m_modsDir.isEmpty()) return true;

    // Suggested default lives under XDG_DATA_HOME so a fresh-profile drop
    // doesn't write into the user's home root.  Each profile gets its own
    // subdir so two profiles' mods can never share disk.
    QString suggested;
    if (!m_profiles->isEmpty()) {
        const GameProfile &gp = m_profiles->current();
        const QString stateRoot = QStandardPaths::writableLocation(
                                      QStandardPaths::AppDataLocation);
        suggested = stateRoot + "/" + gp.id + "/" +
                    gp.activeModlist().name + "/mods";
    } else {
        suggested = QDir::homePath() + "/Games/nerevarine_mods";
    }

    const QString profileName = m_profiles->isEmpty()
        ? QString()
        : m_profiles->current().activeModlist().name;

    QMessageBox box(this);
    box.setWindowTitle(T("mods_dir_prompt_title"));
    box.setIcon(QMessageBox::Question);
    box.setText(T("mods_dir_prompt_body")
                  .arg(profileName)
                  .arg(QDir::toNativeSeparators(suggested)));
    auto *useBtn  = box.addButton(T("mods_dir_prompt_btn_use_suggested"),
                                  QMessageBox::AcceptRole);
    auto *pickBtn = box.addButton(T("mods_dir_prompt_btn_pick"),
                                  QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(useBtn);
    box.exec();

    QString picked;
    if (box.clickedButton() == useBtn) {
        picked = suggested;
    } else if (box.clickedButton() == pickBtn) {
        picked = QFileDialog::getExistingDirectory(
            this, T("mods_dir_dialog_title"), suggested);
    } else {
        return false;
    }
    if (picked.isEmpty()) return false;

    QDir().mkpath(picked);
    m_modsDir = picked;
    if (m_downloadQueue) m_downloadQueue->setModsDir(m_modsDir);
    m_profiles->setActiveModsDir(picked);
    statusBar()->showMessage(T("status_mods_dir_set").arg(m_modsDir), 3000);
    return true;
}

int MainWindow::createNewModlistProfile(const QString &suggestedName)
{
    if (m_profiles->isEmpty()) return -1;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        T("profile_new_title"),
        T("profile_new_prompt"),
        QLineEdit::Normal,
        suggestedName,
        &ok).trimmed();
    if (!ok || name.isEmpty()) return -1;

    const int idx = m_profiles->addModlistProfile(name);
    if (idx < 0) {
        ui::warn(this, T("profile_new_title"), T("profile_new_collision").arg(name));
        return -1;
    }
    return idx;
}



void MainWindow::onAddGame()
{
    // Disable adding other games in this release - OpenMW only
    ui::info(this, T("add_game_title"), "Only OpenMW (Morrowind) is supported in this release.");
}

void MainWindow::addAndDetectGame(const QString &gameId, const QString &displayName)
{
    // Already configured? Just switch.
    for (int i = 0; i < m_profiles->size(); ++i) {
        if (m_profiles->games()[i].id == gameId) {
            switchToGame(i);
            return;
        }
    }

    // Detect: Heroic/GOG → Steam → Lutris.
    QString exe = GameProfileRegistry::findGogGameExe(gameId, /*wantLauncher=*/false);
    if (exe.isEmpty() || !QFile::exists(exe))
        exe = GameProfileRegistry::findSteamGameExe(gameId);
    if (exe.isEmpty() || !QFile::exists(exe))
        exe = GameProfileRegistry::findLutrisGameExe(gameId);

    if (exe.isEmpty() || !QFile::exists(exe)) {
        // Ask the user.
        ui::info(this, QString("Locate %1").arg(displayName), QString("%1 was not found in Steam, Heroic, or Lutris.\n"
                    "Please point to the game's executable.").arg(displayName));
        const QString picked = QFileDialog::getOpenFileName(this,
            QString("Locate %1 executable").arg(displayName),
            QDir::homePath(),
            "Executables (*.exe);;All files (*)");
        if (picked.isEmpty()) return;
        exe = picked;
    }

    // Confirm to the user where it was found.
    ui::info(this, displayName, QString("%1 detected at:\n%2").arg(displayName, QDir::toNativeSeparators(exe)));

    // Per-game mods directory: each profile gets its own root so users can
    // park heavy installs (FNV, Skyrim, etc) on a different mount than OpenMW.
    const QString defaultModsDir = QDir::homePath() + "/Games/" + gameId + "_mods";
    QString modsDir = QFileDialog::getExistingDirectory(this,
        QString("Choose mods directory for %1").arg(displayName),
        defaultModsDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontConfirmOverwrite);
    if (modsDir.isEmpty()) modsDir = defaultModsDir;
    QDir().mkpath(modsDir);

    // Create the profile and switch. Every new game starts with a "Default"
    // modlist profile; filenames use the new scheme (no legacy state to migrate).
    GameProfile gp;
    gp.id          = gameId;
    gp.displayName = displayName;
    gp.modsDir     = modsDir;
    ModlistProfile def;
    def.name              = QStringLiteral("Default");
    def.modsDir           = modsDir;
    def.modlistFilename   = QStringLiteral("modlist_")   + gameId + QStringLiteral("__Default.txt");
    def.loadOrderFilename = QStringLiteral("loadorder_") + gameId + QStringLiteral("__Default.txt");
    gp.modlistProfiles.append(def);
    gp.activeModlistIdx = 0;
    m_profiles->games().append(gp);
    m_profiles->save();
    switchToGame(m_profiles->size() - 1);
}

// Conflict detection

void MainWindow::scheduleConflictScan()
{
    if (m_conflictTimer)
        m_conflictTimer->start(); // restarts the 400ms debounce window
}

void MainWindow::runConflictScan()
{
    // Snapshot enabled + installed mods on the UI thread, then hand off to
    // the controller.  The controller drops the call if a previous scan is
    // still running -- the debounce timer will retrigger on the next edit.
    QHash<QString, QString> modPaths;   // modPath -> display name
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (it->checkState() != Qt::Checked) continue;
        if (it->data(ModRole::InstallStatus).toInt() != 1) continue;
        const QString path = it->data(ModRole::ModPath).toString();
        if (path.isEmpty() || !QDir(path).exists()) continue;
        modPaths.insert(path, it->text());
    }
    m_loadCtl->scanConflicts(modPaths);
}

void MainWindow::onConflictsScanned(const QHash<QString, QStringList> &res)
{
    int conflictCount = 0;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        const QString path = it->data(ModRole::ModPath).toString();
        if (res.contains(path)) {
            it->setData(ModRole::HasConflict,   true);
            it->setData(ModRole::ConflictsWith, res.value(path));
            ++conflictCount;
        } else {
            it->setData(ModRole::HasConflict,   false);
            it->setData(ModRole::ConflictsWith, QStringList());
        }
    }

    m_modList->viewport()->update();
    if (conflictCount > 0)
        statusBar()->showMessage(tr("%1 mod(s) have file conflicts").arg(conflictCount), 4000);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QElapsedTimer closeTimer;
    closeTimer.start();

    // Stop any in-flight debounce timers so they don't fire after we've
    // already started teardown (which would race the destructor and
    // post Qt warnings about "QObject::startTimer: Timers cannot be
    // started from another thread").  saveModList() below also stops
    // m_saveModListTimer at entry; this is the belt-and-braces version
    // for any extra timers added later.
    if (m_conflictTimer)     m_conflictTimer->stop();
    if (m_mastersScanTimer)  m_mastersScanTimer->stop();
    if (m_saveModListTimer)  m_saveModListTimer->stop();

    saveModList();
    const qint64 ms_saveSched = closeTimer.elapsed();

    // saveModList moved its file writes onto a worker thread. The actual
    // bytes have to land on disk before the process exits or Alt+F4
    // would silently lose the user's edits.  Wait here.
    if (m_lastSaveFuture.isRunning())
        m_lastSaveFuture.waitForFinished();
    const qint64 ms_flush = closeTimer.elapsed() - ms_saveSched;

    Settings::setWindowGeometry(saveGeometry());
    Settings::setWindowMaximized(isMaximized());

    qCInfo(logging::lcApp).nospace()
        << "closeEvent ms total=" << closeTimer.elapsed()
        << " save=" << ms_saveSched
        << " flush=" << ms_flush;

    QMainWindow::closeEvent(event);
}
