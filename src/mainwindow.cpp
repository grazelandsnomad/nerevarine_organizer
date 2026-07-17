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
static QString     lootGameFor(const QString &profileId);

// By value so the wizard doesn't need BuiltinGameDef visible here.
static QList<firstrun::GameChoice> builtinGameChoices();
#include <QDropEvent>
#include <QMimeData>

// Drop onto a separator lands just after it (mod becomes the section's first
// entry) instead of Qt's above/below-by-y default.

static bool isInstallableArchiveSuffix(const QString &path);
// .wabbajack and MO2 modlist.txt go to the list-import flow, not archive-install.
static bool isImportFileSuffix(const QString &path);

class ModListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    // dragEnter/dragMove must acceptProposedAction or the drop never fires.
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->source() != this && event->mimeData()->hasUrls()) {
            for (const QUrl &u : event->mimeData()->urls()) {
                if (u.isLocalFile()) {
                    event->acceptProposedAction();
                    return;
                }
            }
        }
        QListWidget::dragEnterEvent(event);
    }
    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (event->source() != this && event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
            return;
        }
        QListWidget::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        // Handle archive paths here, don't forward the drop up to MainWindow:
        // Qt re-delivers by cursor position to the deepest widget (this same
        // ModListWidget), recursing until the stack overflows and SIGSEGVs in
        // QInternalMimeData::formats (Wayland, TTF-for-OpenMW drop). Accept now,
        // defer the install one event-loop tick.
        if (event->source() != this && event->mimeData()->hasUrls()) {
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

            QPointer<QWidget> topGuard(window());
            if (!toInstall.isEmpty()) {
                QMetaObject::invokeMethod(window(), [topGuard, toInstall]{
                    if (!topGuard) return;
                    if (auto *mw = qobject_cast<MainWindow *>(topGuard.data()))
                        for (const QString &p : toInstall)
                            mw->installLocalArchive(p);
                }, Qt::QueuedConnection);
            }
            for (const QString &p : toImport) {
                QMetaObject::invokeMethod(window(), [topGuard, p]{
                    if (!topGuard) return;
                    if (auto *mw = qobject_cast<MainWindow *>(topGuard.data()))
                        mw->handleDroppedImportFile(p);
                }, Qt::QueuedConnection);
            }
            return;
        }

        QPoint pos = event->position().toPoint();
        QListWidgetItem *target = itemAt(pos);
        if (event->source() == this && target &&
            target->data(ModRole::ItemType).toString() == ItemType::Separator)
        {
            int sepRow = row(target);
            QList<int> rows;
            for (auto *sel : selectedItems()) {
                int r = row(sel);
                if (r != sepRow) rows.append(r);
            }
            if (rows.isEmpty()) { event->ignore(); return; }
            std::sort(rows.begin(), rows.end());

            // Take descending so indices stay stable; reinsert top-to-bottom.
            QList<QListWidgetItem *> picked;
            picked.reserve(rows.size());
            for (int i = rows.size() - 1; i >= 0; --i) {
                int r = rows[i];
                picked.prepend(takeItem(r));
                if (r < sepRow) --sepRow;
            }
            for (int i = 0; i < picked.size(); ++i)
                insertItem(sepRow + 1 + i, picked[i]);

            event->acceptProposedAction();
            return;
        }
        QListWidget::dropEvent(event);
    }
};

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

void MainWindow::maybeShowLootMissingBanner()
{
    if (m_profiles->isEmpty()) return;
    // Only nag on profiles where LOOT is actually useful.
    if (lootGameFor(currentProfile().id).isEmpty()) return;
    // Respect the user's "don't remind me" choice (right-click on the banner).
    if (Settings::lootBannerDisabled()) return;
    // Already installed? Nothing to show.
    if (!detectLootBinary().isEmpty()) return;

    // Auto-dismisses after 7s. Left-click opens the LOOT install page; right-click
    // suppresses it persistently.
    m_notify->showWithLink(T("loot_banner_missing"), "#8a4a1a",
                           "https://loot.github.io/", "loot_missing");
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

void MainWindow::setupMenuBar()
{
    auto *fileMenu = menuBar()->addMenu(T("menu_file"));
    fileMenu->addAction(T("menu_new_modlist"), this, &MainWindow::onNewModList);
    fileMenu->addAction(T("menu_export"), this, &MainWindow::exportModList);
    fileMenu->addAction(T("menu_import"), this, &MainWindow::onImportModList);
    fileMenu->addAction(T("menu_import_openmw_cfg"), this,
                        &MainWindow::onImportFromOpenMWConfig);
    fileMenu->addAction(T("menu_import_mo2_profile"), this, &MainWindow::onImportMO2Profile);
    fileMenu->addAction(T("menu_import_mo2"), this, &MainWindow::onImportMO2ModList);
    fileMenu->addAction(T("menu_import_wabbajack"), this, &MainWindow::onImportWabbajack);
    fileMenu->addSeparator();
    fileMenu->addAction(T("menu_quit"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto *modsMenu = menuBar()->addMenu(T("menu_mods"));
    modsMenu->addAction(T("menu_add_separator"),  this, &MainWindow::onAddSeparator);
    modsMenu->addAction(T("menu_add_mod_folder"), this, &MainWindow::onAddMod);
    modsMenu->addSeparator();
    modsMenu->addAction(T("menu_sort_date_asc"),  this, [this]{ m_dateSortAsc = true;  onSortByDate(); });
    modsMenu->addAction(T("menu_sort_date_desc"), this, [this]{ m_dateSortAsc = false; onSortByDate(); });
    modsMenu->addAction(T("menu_sort_size_desc"), this, [this]{ m_sizeSortAsc = false; onSortBySize(); });
    modsMenu->addAction(T("menu_sort_size_asc"),  this, [this]{ m_sizeSortAsc = true;  onSortBySize(); });
    // Undo a temporary Size/Date view sort (no-op when none is active).
    modsMenu->addAction(T("menu_reset_order"), this, [this]{ resetToSavedOrder(); });
    modsMenu->addSeparator();
    // Hidden for profiles LOOT doesn't support, matching the toolbar button's
    // per-profile visibility.
    m_actMenuSortLoot = modsMenu->addAction(T("menu_sort_loot"), this, [this]{
        autoSortLoadOrder();
        saveLoadOrder();
        syncGameConfig();
    });
    m_actMenuSortLoot->setVisible(!lootGameFor(currentProfile().id).isEmpty());
    modsMenu->addSeparator();
    modsMenu->addAction(T("menu_check_updates"),   this, &MainWindow::onCheckUpdates);
    modsMenu->addAction(T("menu_review_updates"),  this, &MainWindow::onReviewUpdates);
    modsMenu->addAction(T("menu_restore_backup"),  this,
                        [this]{ m_backups->showRestoreBackupDialog(this); });
    modsMenu->addAction(T("menu_inspect_openmw"),  this, &MainWindow::onInspectOpenMWSetup);
    modsMenu->addAction(T("menu_conflict_inspector"), this, &MainWindow::onInspectConflicts);
    m_actDeployBethesda = modsMenu->addAction(T("menu_deploy_bethesda"), this, &MainWindow::onDeployBethesda);
    m_actUndeployBethesda = modsMenu->addAction(T("menu_undeploy_bethesda"), this, &MainWindow::onUndeployBethesda);
    m_actInspectDeployment = modsMenu->addAction(T("menu_inspect_deployment"), this, &MainWindow::onInspectDeployment);
    {   // initial gate (the profile-UI refresh keeps these in sync afterwards)
        const GameAdapter *a = GameAdapterRegistry::find(currentProfile().id);
        const bool deployable = a && !a->dataSubdir().isEmpty();
        m_actDeployBethesda->setVisible(deployable);
        m_actUndeployBethesda->setVisible(deployable);
        m_actInspectDeployment->setVisible(deployable);
    }
    modsMenu->addAction(T("menu_log_triage"),      this, &MainWindow::onTriageOpenMWLog);
    modsMenu->addAction(T("menu_diag_bundle"),     this, &MainWindow::onCreateDiagnosticBundle);
    modsMenu->addAction(T("menu_edit_load_order"), this, &MainWindow::onEditLoadOrder);

    auto *settingsMenu = menuBar()->addMenu(T("menu_settings"));
    settingsMenu->addAction(T("menu_api_key"),  this, &MainWindow::onSetApiKey);
    settingsMenu->addAction(T("menu_mods_dir"), this, &MainWindow::onSetModsDir);
    settingsMenu->addSeparator();
    settingsMenu->addAction(T("menu_set_openmw_path"), this, [this]{
        QString path = QFileDialog::getOpenFileName(
            this, T("launch_locate_openmw"), m_openmwPath.isEmpty() ? "/usr/bin" : m_openmwPath);
        if (!path.isEmpty()) {
            m_openmwPath = path;
            currentProfile().openmwPath = path;
            Settings::setOpenmwPath(currentProfile().id, path);
        }
    });
    settingsMenu->addAction(T("menu_set_openmw_launcher_path"), this, [this]{
        QString path = QFileDialog::getOpenFileName(
            this, T("launch_locate_launcher"), m_openmwLauncherPath.isEmpty() ? "/usr/bin" : m_openmwLauncherPath);
        if (!path.isEmpty()) {
            m_openmwLauncherPath = path;
            currentProfile().openmwLauncherPath = path;
            Settings::setOpenmwLauncherPath(currentProfile().id, path);
        }
    });
    settingsMenu->addSeparator();

    // 0.4 pins OpenMW + FNV + Starfield in the dropdown - the tested games this
    // release. This toggle surfaces the untested legacy game list. Off by default.
    {
        auto *act = settingsMenu->addAction(T("menu_show_all_games"));
        act->setCheckable(true);
        act->setChecked(Settings::showAllGames());
        connect(act, &QAction::toggled, this, [this](bool on) {
            Settings::setShowAllGames(on);
            updateGameButton();
        });
    }
    settingsMenu->addSeparator();

    // Columns submenu - actions live on m_columnHeader; we just attach them.
    auto *colMenu = settingsMenu->addMenu(T("menu_columns"));
    colMenu->addAction(m_columnHeader->colStatusAction());
    colMenu->addAction(m_columnHeader->colDateAction());
    colMenu->addAction(m_columnHeader->colRelTimeAction());
    colMenu->addAction(m_columnHeader->colAnnotAction());
    colMenu->addAction(m_columnHeader->colSizeAction());
    colMenu->addAction(m_columnHeader->colVideoReviewAction());
    settingsMenu->addSeparator();

    // UI Scale submenu
    auto *scaleMenu  = settingsMenu->addMenu(T("menu_ui_scale"));
    auto *scaleGroup = new QActionGroup(scaleMenu);
    scaleGroup->setExclusive(true);

    double currentScale = Settings::uiScaleFactor();
    const QList<QPair<QString, double>> scaleOptions = {
        {"1×",    1.0},
        {"1.25×", 1.25},
        {"1.5×",  1.5},
        {"1.75×", 1.75},
        {"2×",    2.0},
        {"2.5×",  2.5},
        {"3×",    3.0},
    };
    for (const auto &[label, factor] : scaleOptions) {
        auto *act = scaleMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(qAbs(currentScale - factor) < 0.01);
        scaleGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, factor]() {
            Settings::setUiScaleFactor(factor);
            ui::info(this, T("ui_scale_restart_title"), T("ui_scale_restart_body"));
        });
    }
    settingsMenu->addSeparator();

    // Language submenu - entries in their native names, radio-button style
    auto *langMenu  = settingsMenu->addMenu(T("menu_language"));
    auto *langGroup = new QActionGroup(langMenu);
    langGroup->setExclusive(true);

    for (const QString &lang : Translator::available()) {
        auto *act = langMenu->addAction(Translator::nativeName(lang));
        act->setCheckable(true);
        act->setChecked(lang == Translator::currentLanguage());
        langGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, lang]() {
            onSetLanguage(lang);
        });
    }

    settingsMenu->addSeparator();
    settingsMenu->addAction(T("menu_customize_toolbar"), this,
                            [this]{ m_tbCustom->showCustomizeDialog(this); });
    settingsMenu->addSeparator();
    // Diagnostics - opens the folder containing log.txt + rotated backups
    // so users handing us a bug report can grab the latest one quickly.
    settingsMenu->addAction(T("menu_open_log_folder"), this, [this]{
        const QString dir = logging::logDirectory();
        if (dir.isEmpty()) return;
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        statusBar()->showMessage(T("status_log_folder_opened").arg(dir), 4000);
    });
    settingsMenu->addSeparator();
    settingsMenu->addAction(T("menu_forbidden_mods"), this,
                            [this]{ m_forbidden->showManageDialog(this); });
}

void MainWindow::setupToolbar()
{
    auto *tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);

    // -- Game selector button (first in the bar) ---
    m_gameBtn = new QToolButton(tb);
    m_gameBtn->setPopupMode(QToolButton::InstantPopup);
    m_gameBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_gameBtn->setStyleSheet(
        "QToolButton {"
        "  font-weight: bold;"
        "  padding: 3px 10px 3px 8px;"
        "  border: 1px solid #555;"
        "  border-radius: 4px;"
        "  background: #2d3a4a;"
        "  color: #cde;"
        "}"
        "QToolButton:hover  { background: #3a4f65; }"
        "QToolButton:pressed{ background: #1e2c3a; }"
        "QToolButton::menu-indicator { image: none; }");
    tb->addWidget(m_gameBtn);

    // "Profile:" label between the game button and the profile dropdown, so
    // the picker reads as a sub-selector of the current game.
    m_profileLbl = new QLabel(T("toolbar_profile_label"), tb);
    // Colour baked in by restyleToolbarTextButtons() (QSS palette() stale-swap
    // reason, as for the buttons).
    tb->addWidget(m_profileLbl);

    // Modlist profile picker. Toned-down chrome (thin border, transparent fill)
    // so it reads as a sub-control of the game button. Same hover/pressed as it.
    m_profileBtn = new QToolButton(tb);
    m_profileBtn->setPopupMode(QToolButton::InstantPopup);
    m_profileBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    // Colour baked in by restyleToolbarTextButtons(), not a `palette(window-text)`
    // QSS expr: QSS resolves+caches that once, a QStyle swap doesn't re-resolve,
    // so the second light pass painted stale invisible text.
    tb->addWidget(m_profileBtn);
    tb->addSeparator();

    // Generic launch buttons for non-Morrowind Steam games
    m_actLaunchGame = tb->addAction(T("toolbar_launch_game"), this, &MainWindow::onLaunchGame);
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(m_actLaunchGame)))
        btn->setStyleSheet("color: #1a8a1a; font-weight: bold;");
    m_actLaunchSteamLauncher = tb->addAction(T("toolbar_launch_launcher"), this, &MainWindow::onLaunchSteamLauncher);

    // Skyrim SE - simple BethINI-style INI tweaker (hidden for other profiles)
    m_actTuneSkyrimIni = tb->addAction(T("toolbar_tune_skyrim_ini"),
                                        this, &MainWindow::onTuneSkyrimIni);
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(m_actTuneSkyrimIni)))
        btn->setStyleSheet("color: #1a6fa8; font-weight: bold;");

    // Launch buttons (OpenMW-specific - shown only for Morrowind)
    m_actLaunchOpenMW = tb->addAction(T("toolbar_launch_openmw"), this, &MainWindow::onLaunchOpenMW);
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(m_actLaunchOpenMW)))
        btn->setStyleSheet("color: #1a8a1a; font-weight: bold;");
    m_actLaunchLauncher = tb->addAction(T("toolbar_launch_launcher"), this, &MainWindow::onLaunchOpenMWLauncher);

    // Sort with LOOT - on-demand; visibility toggled per-profile in
    // updateGameButton() (only shown when lootGameFor() returns non-empty).
    m_actSortLoot = tb->addAction(T("toolbar_sort_loot"), this, [this]{
        autoSortLoadOrder();
        saveLoadOrder();
        syncGameConfig();
    });
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(m_actSortLoot)))
        btn->setStyleSheet("color: #8a4a1a; font-weight: bold;");

    tb->addSeparator();

    auto *actSep = tb->addAction(T("toolbar_add_separator"), this, &MainWindow::onAddSeparator);
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(actSep)))
        btn->setStyleSheet("color: #c0392b; font-weight: bold;");

    auto *actSortSeps = tb->addAction(T("toolbar_sort_separators"),
                                       this, &MainWindow::onSortSeparators);
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(actSortSeps)))
        btn->setStyleSheet("color: #6a1b9a; font-weight: bold;");
    tb->addSeparator();

    auto *actRemove = tb->addAction(T("toolbar_remove"),        this, &MainWindow::onRemoveSelected);
    auto *actReview = tb->addAction(T("toolbar_review_updates"), this, &MainWindow::onReviewUpdates);
    auto *actExport = tb->addAction(T("toolbar_export"), this, &MainWindow::exportModList);
    auto *actImport = tb->addAction(T("toolbar_import"), this, &MainWindow::onImportModList);

    // -- Featured Modlists button ---
    // On hold pending scope. For now opens a "Work in progress" dialog instead
    // of a live dropdown. To revive: restore setPopupMode(InstantPopup) and the
    // per-game menu population in updateGameButton().
    m_featuredModlistsBtn = new QToolButton(tb);
    m_featuredModlistsBtn->setText(QString("\u2605 ") + T("toolbar_featured_modlists"));
    m_featuredModlistsBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_featuredModlistsBtn->setStyleSheet(
        "QToolButton {"
        "  padding: 3px 8px;"
        "  border: 1px solid #555;"
        "  border-radius: 4px;"
        "}");
    connect(m_featuredModlistsBtn, &QToolButton::clicked, this, [this]{
        ui::info(this, T("featured_wip_title"), T("featured_wip_body"));
    });
    tb->addWidget(m_featuredModlistsBtn);

    auto *actAddMod = tb->addAction(T("toolbar_add_mod"), this, &MainWindow::onAddMod);

    // Right-aligned section: expanding spacer pushes later actions to the far
    // end. Forbidden Mods / Check Updates are "status" actions, grouped with
    // Modlist Summary at the right edge to keep the left region for editing.
    auto *tbSpacer = new QWidget(tb);
    tbSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(tbSpacer);

    // -- Light/Dark theme toggle ---
    // Flips the whole-app palette (see theme::) and persists it. Label names
    // the theme it switches TO. Chrome follows the active palette.
    m_themeBtn = new QToolButton(tb);
    m_themeBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    // Stylesheet (incl. text colour) is applied by restyleToolbarTextButtons().
    connect(m_themeBtn, &QToolButton::clicked, this, &MainWindow::onToggleTheme);
    tb->addWidget(m_themeBtn);
    updateThemeButton();
    restyleToolbarTextButtons();   // bake concrete colours from the live palette

    // -- Good States dropdown ---
    // Leftmost of the right-aligned group. Lists user-marked "good" modlist
    // checkpoints; menu rebuilt on each show so add/delete reflects right away.
    auto *goodStatesBtn = new QToolButton(tb);
    goodStatesBtn->setText(QString("◆ ") + T("toolbar_good_states"));
    goodStatesBtn->setPopupMode(QToolButton::InstantPopup);
    goodStatesBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    goodStatesBtn->setStyleSheet("QToolButton { color: #1a8a1a; font-weight: bold; padding: 3px 8px; }");
    auto *goodStatesMenu = new QMenu(goodStatesBtn);
    connect(goodStatesMenu, &QMenu::aboutToShow, this, [this, goodStatesMenu]{
        m_backups->populateGoodStatesMenu(goodStatesMenu, this);
    });
    // Seed once so the button has a sensible initial menu (without a click).
    m_backups->populateGoodStatesMenu(goodStatesMenu, this);
    goodStatesBtn->setMenu(goodStatesMenu);
    auto *actGoodStates = tb->addWidget(goodStatesBtn);

    auto *actForbidden = tb->addAction(QString("⊘ ") + T("menu_forbidden_mods"),
                                       this, [this]{ m_forbidden->showManageDialog(this); });
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(actForbidden)))
        btn->setStyleSheet("color: #c0392b; font-weight: bold;");

    auto *actRestore = tb->addAction(T("toolbar_restore_backup"),
                                      this, [this]{ m_backups->showRestoreBackupDialog(this); });

    auto *actSummary = tb->addAction(T("toolbar_modlist_summary"),
                                      this, &MainWindow::onModlistSummary);
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(actSummary)))
        btn->setStyleSheet("color: #1a6fa8; font-weight: bold;");

    auto *actDiagBundle = tb->addAction(T("toolbar_diag_bundle"),
                                         this, &MainWindow::onCreateDiagnosticBundle);
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(actDiagBundle)))
        btn->setStyleSheet("color: #6a1b9a; font-weight: bold;");

    // Utmost-right slot: Check Updates.  Last widget added to the toolbar
    // before the menu-extension chevron, so on narrow windows it stays
    // visible when other right-aligned buttons collapse into the overflow.
    auto *actCheck = tb->addAction(T("toolbar_check_updates"), this, &MainWindow::onCheckUpdates);
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(actCheck))) {
        btn->setStyleSheet(
            "QToolButton {"
            "  background-color: #1a8a1a;"
            "  color: white;"
            "  font-weight: bold;"
            "  border-radius: 4px;"
            "  padding: 3px 10px;"
            "}"
            "QToolButton:hover {"
            "  background-color: #22a822;"
            "}"
            "QToolButton:pressed {"
            "  background-color: #156815;"
            "}");
    }

    // Register every customizable toolbar entry so the "Customize toolbar…"
    // dialog can show and persist a checkbox per item.  The game selector
    // (m_gameBtn) is NOT registered - it's load-bearing.
    // Profile-gated entries (launch buttons, Tune INI, Sort LOOT) stay gated
    // in updateGameButton(); the user preference is ANDed on top.
    QAction *actFeatured = nullptr;
    for (QAction *a : tb->actions()) {
        if (tb->widgetForAction(a) == m_featuredModlistsBtn) { actFeatured = a; break; }
    }
    m_tbCustom->registerAction("launch_game",           m_actLaunchGame,          T("toolbar_launch_game"));
    m_tbCustom->registerAction("launch_steam_launcher", m_actLaunchSteamLauncher, T("toolbar_launch_launcher"));
    m_tbCustom->registerAction("tune_skyrim_ini",       m_actTuneSkyrimIni,       T("toolbar_tune_skyrim_ini"));
    m_tbCustom->registerAction("launch_openmw",         m_actLaunchOpenMW,        T("toolbar_launch_openmw"));
    m_tbCustom->registerAction("launch_openmw_launcher",m_actLaunchLauncher,      T("toolbar_launch_launcher"));
    m_tbCustom->registerAction("sort_loot",             m_actSortLoot,            T("toolbar_sort_loot"),            /*defaultVisible=*/false);
    m_tbCustom->registerAction("add_separator",         actSep,                   T("toolbar_add_separator"));
    m_tbCustom->registerAction("sort_separators",       actSortSeps,              T("toolbar_sort_separators"),      /*defaultVisible=*/false);
    m_tbCustom->registerAction("forbidden_mods",        actForbidden,             T("menu_forbidden_mods"));
    m_tbCustom->registerAction("remove_mod",            actRemove,                T("toolbar_remove"),               /*defaultVisible=*/false);
    m_tbCustom->registerAction("check_updates",         actCheck,                 T("toolbar_check_updates"));
    m_tbCustom->registerAction("review_updates",        actReview,                T("toolbar_review_updates"),       /*defaultVisible=*/false);
    m_tbCustom->registerAction("restore_backup",        actRestore,               T("toolbar_restore_backup"));
    m_tbCustom->registerAction("good_states",           actGoodStates,            T("toolbar_good_states"));
    m_tbCustom->registerAction("export",                actExport,                T("toolbar_export"),               /*defaultVisible=*/false);
    m_tbCustom->registerAction("import",                actImport,                T("toolbar_import"),               /*defaultVisible=*/false);
    if (actFeatured)
        m_tbCustom->registerAction("featured_modlists", actFeatured,              T("toolbar_featured_modlists"),    /*defaultVisible=*/false);
    m_tbCustom->registerAction("add_mod",               actAddMod,                T("toolbar_add_mod"),              /*defaultVisible=*/false);
    m_tbCustom->registerAction("modlist_summary",       actSummary,               T("toolbar_modlist_summary"));
    m_tbCustom->registerAction("diag_bundle",           actDiagBundle,            T("toolbar_diag_bundle"),          /*defaultVisible=*/false);

    // Must be called last so all toolbar members are initialised
    updateGameButton(); // sets game button text/menu + shows correct launch button(s)
    updateProfileButton();
    m_tbCustom->applyAll();
}

void MainWindow::updateThemeButton()
{
    if (!m_themeBtn) return;
    // Label names the theme the click will switch TO.
    m_themeBtn->setText(Settings::uiDarkMode() ? T("toolbar_light_mode")
                                               : T("toolbar_dark_mode"));
}

void MainWindow::restyleToolbarTextButtons()
{
    // Transparent toolbar widgets (Profile label+picker, theme toggle) sit on
    // the window surface and need contrasting text. Two traps:
    //  1. A QSS `palette(window-text)` expr is cached once, not re-resolved on
    //     a global QStyle swap, so it goes stale.
    //  2. qApp/this->palette() right after theme::applyTheme() is still stale -
    //     setPalette() propagates via events, not synchronously, so during
    //     onToggleTheme() the live palette is the previous theme's. That left
    //     light-grey text on the light toolbar going dark->light.
    // Derive colour from background darkness via theme:: (reads the captured
    // default palette, not the live one). Keying off the real background, not
    // the dark-mode flag, also fixes dark-on-dark when "light mode" runs on a
    // dark desktop theme (Breeze Dark).
    const bool bgDark = theme::backgroundIsDark(Settings::uiDarkMode());
    const QString txt = bgDark ? QStringLiteral("#dcdcdc") : QStringLiteral("#1a1a1a");
    const QString mid = bgDark ? QStringLiteral("#5a5a5a") : QStringLiteral("#888888");

    if (m_profileLbl)
        m_profileLbl->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  color: %1;"
            "  padding: 0 6px 0 8px;"
            "  font-size: 9pt;"
            "  font-weight: bold;"
            "}").arg(txt));

    if (m_profileBtn)
        m_profileBtn->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "  font-weight: bold;"
            "  padding: 3px 10px 3px 8px;"
            "  border: 1px solid #555;"
            "  border-radius: 4px;"
            "  background: transparent;"
            "  color: %1;"
            "}"
            "QToolButton:hover  { background: rgba(127,127,127,0.18); }"
            "QToolButton:pressed{ background: rgba(0,0,0,0.22); }"
            "QToolButton::menu-indicator { image: none; }").arg(txt));

    if (m_themeBtn)
        m_themeBtn->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "  padding: 3px 8px;"
            "  border: 1px solid %1;"
            "  border-radius: 4px;"
            "  color: %2;"
            "}"
            "QToolButton:hover { background: rgba(127,127,127,0.18); }")
            .arg(mid, txt));
}

void MainWindow::onToggleTheme()
{
    const bool dark = !Settings::uiDarkMode();
    Settings::setUiDarkMode(dark);
    theme::applyTheme(dark);
    updateThemeButton();
    restyleToolbarTextButtons();   // re-bake button text colour for the new theme
    // Repaint the list so default-coloured separators pick up the theme-aware
    // default immediately (the delegate keys off the active palette).
    if (m_modList && m_modList->viewport())
        m_modList->viewport()->update();
}

void MainWindow::setupCentralWidget()
{
    m_modList = new ModListWidget(this);

    // Model migration stage 2: m_model tracks m_modList's change signals in
    // real time (checkbox toggles, drag-reorders, insert/remove). Must run
    // after m_modList exists; m_model is allocated earlier, in the ctor.
    if (m_model)
        modlist::connectAutoSync(m_model, m_modList);

    m_zoom = new ZoomController(m_modList, this);
    m_zoom->loadPrefs();
    m_undoStack = new UndoStack(m_modList, this);
    connect(m_undoStack, &UndoStack::requestCollapse,
            this, [this](QListWidgetItem *sep) { collapseSection(sep, true); });
    connect(m_undoStack, &UndoStack::stateApplied,
            this, [this]() {
        updateModCount();
        saveModList();
        scheduleConflictScan();
    });
    connect(m_undoStack, &UndoStack::statusMessage,
            this, [this](const QString &msg, int ms) {
        statusBar()->showMessage(msg, ms);
    });
    m_delegate = new ModListDelegate(m_modList);
    m_modList->setItemDelegate(m_delegate);
    m_modList->setDragDropMode(QAbstractItemView::InternalMove);
    m_modList->setDefaultDropAction(Qt::MoveAction);
    m_modList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_modList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_modList->setSpacing(1);
    m_modList->setAlternatingRowColors(true);
    m_modList->setUniformItemSizes(false);

    connect(m_modList, &QListWidget::customContextMenuRequested,
            this, &MainWindow::onContextMenu);
    connect(m_modList, &QListWidget::itemDoubleClicked,
            this, &MainWindow::onItemDoubleClicked);
    connect(m_modList, &QListWidget::currentItemChanged,
            this, &MainWindow::onCurrentModChanged);

    connect(m_delegate, &ModListDelegate::separatorCollapseToggleClicked, this,
            [this](const QModelIndex &idx){
        auto *sep = m_modList->item(idx.row());
        if (!sep) return;
        m_undoStack->pushUndo();
        collapseSection(sep, !sep->data(ModRole::Collapsed).toBool());
        scheduleSaveModList();   // debounced - rapid collapse/expand coalesces
    });

    // Green update-triangle: mod already installed once, so skip the first-
    // install dep check (it popped "Possible Missing Requirements" on every
    // update) and go straight to the Nexus file picker + download. Forbidden
    // check and reinstall confirm are also redundant here.
    connect(m_delegate, &ModListDelegate::updateArrowClicked, this,
            [this](const QModelIndex &idx){
        auto *item = m_modList->item(idx.row());
        if (!item) return;

        if (m_apiKey.isEmpty()) {
            ui::info(this, T("nxm_api_key_required_title"), T("nxm_api_key_required_body"));
            onSetApiKey();
            if (m_apiKey.isEmpty()) return;
        }

        const QString nexusUrl = item->data(ModRole::NexusUrl).toString();
        const auto ref = parseNexusModUrl(nexusUrl);
        if (!ref) {
            ui::warn(this, T("nexus_api_error_title"), T("install_invalid_url"));
            return;
        }
        const QString game  = ref->game;
        const int     modId = ref->modId;

        QString name = item->data(ModRole::CustomName).toString();
        if (name.isEmpty()) name = item->text();
        if (!ui::confirm(this, T("update_mod_title"),
                T("update_mod_body").arg(name)))
            return;

        prepareItemForInstall(item);
        fetchModFiles(game, modId, item);
    });

    connect(m_delegate, &ModListDelegate::favoriteToggleClicked, this,
            [this](const QModelIndex &idx){
        auto *item = m_modList->item(idx.row());
        if (!item) return;
        m_undoStack->pushUndo();
        bool isFav = item->data(ModRole::IsFavorite).toBool();
        item->setData(ModRole::IsFavorite, !isFav);
        m_modList->update(idx);
        scheduleSaveModList();   // debounced - rapid star toggles coalesce
    });

    connect(m_delegate, &ModListDelegate::videoReviewClicked, this,
            [](const QString &url){
        if (!url.isEmpty())
            QDesktopServices::openUrl(QUrl(url));
    });

    // Enable hover-state delivery so the star shows up on mouse-over
    m_modList->viewport()->setAttribute(Qt::WA_Hover, true);
    m_modList->setMouseTracking(true);
    m_modList->viewport()->setMouseTracking(true);

    // Push undo state before a drag-drop reorder, then save afterwards
    connect(m_modList->model(), &QAbstractItemModel::rowsAboutToBeMoved,
            this, [this]() { if (!m_undoStack->isApplyingState()) m_undoStack->pushUndo(); });
    connect(m_modList->model(), &QAbstractItemModel::rowsMoved,
            this, [this]() {
        // A drag-drop reorder commits the current display as the saved order,
        // ending any temporary view sort.
        dropViewSortKeepingOrder();
        // Debounced - a multi-row drag fires N rowsMoved signals; coalescing
        // avoids the N×waitForFinished stall on the m_lastSaveFuture chain.
        scheduleSaveModList();
        scheduleConflictScan();
    });

    m_modList->viewport()->installEventFilter(this);
    m_modList->installEventFilter(this);

    m_columnHeader->attachListWidget(m_modList);
    connect(m_columnHeader, &ColumnHeader::visibilityChanged,
            this, [this](const ColVisibility &cv) {
        m_delegate->setColVisibility(cv);
        m_modList->viewport()->update();
    });

    // The two sort buttons live here: their clicked signals route to MainWindow
    // sort slots, which also update the button text on direction toggle.
    // ColumnHeader splices them into the layout.
    m_dateSortBtn = new QPushButton(T("col_date_added_asc"), m_columnHeader->widget());
    m_dateSortBtn->setFlat(true);
    m_dateSortBtn->setStyleSheet(
        "font-weight: bold; color: #888; font-size: 9pt;"
        "text-align: left; padding: 0; border: none;");
    m_dateSortBtn->setCursor(Qt::PointingHandCursor);
    connect(m_dateSortBtn, &QPushButton::clicked, this, &MainWindow::onSortByDate);
    m_columnHeader->setDateSortButton(m_dateSortBtn);

    m_sizeSortBtn = new QPushButton(T("col_size_desc"), m_columnHeader->widget());
    m_sizeSortBtn->setFlat(true);
    m_sizeSortBtn->setStyleSheet(
        "font-weight: bold; color: #888; font-size: 9pt;"
        "text-align: left; padding: 0; border: none;");
    m_sizeSortBtn->setCursor(Qt::PointingHandCursor);
    connect(m_sizeSortBtn, &QPushButton::clicked, this, &MainWindow::onSortBySize);
    m_columnHeader->setSizeSortButton(m_sizeSortBtn);

    m_columnHeader->apply();

    m_notify = new NotifyBanner(this);
    connect(m_notify, &NotifyBanner::statusMessage,
            this, [this](const QString &msg, int ms) {
        statusBar()->showMessage(msg, ms);
    });
    // Clicking the "temporary view sort" banner restores the saved order.
    connect(m_notify, &NotifyBanner::stickyClicked,
            this, &MainWindow::resetToSavedOrder);

    m_filterBar = new FilterBar(m_modList, this);

    auto *container  = new QWidget(this);
    auto *vbox       = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(m_columnHeader->widget());
    vbox->addWidget(m_notify->widget());
    vbox->addWidget(m_modList);
    vbox->addWidget(m_filterBar->widget());
    setCentralWidget(container);

    // Mod count label - permanent widget on the right of the status bar
    m_modCountLabel = new QLabel(this);
    m_modCountLabel->setStyleSheet("color: #888; padding: 0 10px;");
    statusBar()->addPermanentWidget(m_modCountLabel);

    // Update count whenever a checkbox is toggled; also re-scan conflicts (enabled set changed)
    connect(m_modList, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (item->data(ModRole::ItemType).toString() == ItemType::Mod) {
            updateModCount();
            updateSectionCounts();
            scheduleConflictScan();
        }
    });

    // Apply saved zoom once the widget exists
    QTimer::singleShot(0, this, [this]() { m_zoom->applyZoom(m_zoom->current()); });
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

void MainWindow::collapseSection(QListWidgetItem *sep, bool collapse)
{
    sep->setData(ModRole::Collapsed, collapse);
    int row = m_modList->row(sep);
    for (int i = row + 1; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() == ItemType::Separator) break;
        it->setHidden(collapse);
    }
    m_modList->update(m_modList->indexFromItem(sep));
    updateModCount();
}

void MainWindow::onEditSeparator(QListWidgetItem *item)
{
    SeparatorDialog dlg(this);
    dlg.prefill(item->text(),
                item->data(ModRole::BgColor).value<QColor>(),
                item->data(ModRole::FgColor).value<QColor>());
    if (dlg.exec() != QDialog::Accepted) return;
    QString name = dlg.separatorName().trimmed();
    if (name.isEmpty()) return;
    m_undoStack->pushUndo();
    item->setText(name);
    item->setData(ModRole::BgColor, dlg.backgroundColor());
    item->setData(ModRole::FgColor, dlg.fontColor());
    m_modList->update(m_modList->indexFromItem(item));
    saveModList();
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

void MainWindow::onAddSeparator()
{
    // Always append at the end of the list.  That's a clean section boundary
    // (a fresh, empty section) and a predictable spot, unlike the old
    // "insert after the last-selected row" behaviour which - when the
    // selection was a collapsed separator - dropped the new separator between
    // it and its still-present hidden children, silently stealing that whole
    // section.  For precise placement, the row context menu offers "Add
    // separator above".
    addSeparatorAtRow(m_modList->count());
}

void MainWindow::addSeparatorAtRow(int targetRow)
{
    SeparatorDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    QString name = dlg.separatorName().trimmed();
    if (name.isEmpty()) return;

    m_undoStack->pushUndo();
    auto *item = new QListWidgetItem(name);
    item->setData(ModRole::ItemType, ItemType::Separator);
    item->setData(ModRole::BgColor,  dlg.backgroundColor());
    item->setData(ModRole::FgColor,  dlg.fontColor());
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);

    m_modList->insertItem(qBound(0, targetRow, m_modList->count()), item);
    m_modList->setCurrentItem(item);
    m_modList->scrollToItem(item);
    statusBar()->showMessage(T("status_separator_added").arg(name), 2000);
    updateSectionCounts();   // new boundary - refresh the (active/total) tallies
    saveModList();
}

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

void MainWindow::onRemoveSelected()
{
    const auto selected = m_modList->selectedItems();
    if (selected.isEmpty()) return;

    QString msg = selected.size() == 1
        ? T("remove_one").arg(selected.first()->text())
        : T("remove_many").arg(selected.size());

    if (!ui::confirm(this, T("remove_title"), msg))
        return;

    // Scan for dependents: other mods whose DependsOn points at any of the
    // URLs we're about to remove.  Guards against the common patch/base
    // split - removing "Interface Reimagined" (base) while leaving its
    // OpenMW 0.50 patch in the list would quietly turn the patch's icon
    // yellow; surface that up front so the user makes the call explicitly.
    QSet<QString> goingAway;
    QSet<QListWidgetItem*> selectedSet;
    for (auto *item : selected) {
        selectedSet.insert(item);
        const QString u = item->data(ModRole::NexusUrl).toString();
        if (!u.isEmpty()) goingAway.insert(u);
    }

    QList<QListWidgetItem*> dependents;
    if (!goingAway.isEmpty()) {
        for (int i = 0; i < m_modList->count(); ++i) {
            auto *cand = m_modList->item(i);
            if (cand->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
            if (selectedSet.contains(cand)) continue;
            const QStringList deps = cand->data(ModRole::DependsOn).toStringList();
            for (const QString &u : deps) {
                if (goingAway.contains(u)) { dependents.append(cand); break; }
            }
        }
    }

    QList<QListWidgetItem*> toRemove = selected;
    if (!dependents.isEmpty()) {
        QStringList names;
        for (auto *d : dependents) {
            QString n = d->data(ModRole::CustomName).toString();
            if (n.isEmpty()) n = d->text();
            names << "  • " + n;
        }
        QMessageBox box(this);
        box.setWindowTitle(T("remove_deps_title"));
        box.setIcon(QMessageBox::Warning);
        box.setText(T("remove_deps_body").arg(dependents.size()));
        box.setInformativeText(names.join('\n'));
        auto *alsoBtn   = box.addButton(T("remove_deps_also"),
                                        QMessageBox::DestructiveRole);
        auto *leaveBtn  = box.addButton(T("remove_deps_leave"),
                                        QMessageBox::AcceptRole);
        auto *cancelBtn = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(leaveBtn);
        box.exec();
        if (box.clickedButton() == cancelBtn)  return;
        if (box.clickedButton() == alsoBtn)    toRemove.append(dependents);
    }

    m_undoStack->pushUndo();

    // Clean up DependsOn references in remaining mods that pointed at any
    // of the URLs we're removing.  Without this, removing a dependency
    // leaves stale entries that later trigger the launch-time "missing
    // dependencies" warning even though the user explicitly removed the mod.
    if (!goingAway.isEmpty()) {
        for (int i = 0; i < m_modList->count(); ++i) {
            auto *cand = m_modList->item(i);
            if (cand->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
            if (selectedSet.contains(cand)) continue;
            QStringList deps = cand->data(ModRole::DependsOn).toStringList();
            int before = deps.size();
            deps.erase(std::remove_if(deps.begin(), deps.end(),
                       [&](const QString &u) { return goingAway.contains(u); }),
                       deps.end());
            if (deps.size() != before)
                cand->setData(ModRole::DependsOn, deps);
        }
    }

    // Clean up groundcover approvals for removed mods.  Drop BOTH the
    // ModPath and the NexusUrl entries - the helper below (line ~3200)
    // stores both so approval survives across timestamp-suffixed reinstalls,
    // but on explicit removal the user's answer is stale and a later re-add
    // should ask again.  Leaving the NexusUrl behind was the reason the
    // grass-helper prompt silently skipped on Fantasia Grass Mod's reinstall
    // after a previous remove.
    {
        bool gcChanged = false;
        for (auto *item : toRemove) {
            const QString mp = item->data(ModRole::ModPath).toString();
            if (!mp.isEmpty() && m_groundcoverApproved.remove(mp))
                gcChanged = true;
            const QString nu = item->data(ModRole::NexusUrl).toString();
            if (!nu.isEmpty() && m_groundcoverApproved.remove(nu))
                gcChanged = true;
        }
        if (gcChanged) {
            Settings::setGroundcoverApproved(
                QStringList(m_groundcoverApproved.begin(),
                            m_groundcoverApproved.end()));
        }
    }

    // Collect the plugin filenames provided by the mods we're about to
    // remove BEFORE the rows disappear from the list - we'll need them in
    // a moment to spot redistributions.  Rationale: sibling mods often
    // bundle copies of other mods' ESPs inside numbered "patch" subfolders
    // (e.g. South Wall_RP ships "01 AM Morrowind" with a copy of
    // Animated_Morrowind - merged.esp).  If the original "Animated
    // Morrowind" mod is uninstalled, those bundled copies keep the plugin
    // loading forever - exactly the "remnant" the user is now reporting.
    static const QStringList kContentExts{".esp", ".esm", ".omwaddon"};
    QSet<QString> removedPluginFilenames;
    QStringList installedPaths;
    for (auto *it : toRemove) {
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        const QString mp = it->data(ModRole::ModPath).toString();
        if (mp.isEmpty()) continue;
        // Use cache - avoids a cold filesystem scan before the "delete from disk?" dialog.
        for (const auto &p : m_scans->cachedDataFolders(mp, kContentExts))
            for (const QString &cf : p.second)
                removedPluginFilenames.insert(cf);
        if (it->data(ModRole::InstallStatus).toInt() == 1 && QDir(mp).exists())
            installedPaths << mp;
    }

    // Ask once whether to also delete from disk (No is default).
    bool deleteDisk = false;
    if (!installedPaths.isEmpty()) {
        QMessageBox box(this);
        box.setWindowTitle(T("remove_title"));
        box.setIcon(QMessageBox::Question);
        box.setText(T("import_delete_from_disk_prompt").arg(installedPaths.size()));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box.setDefaultButton(QMessageBox::No);
        deleteDisk = (box.exec() == QMessageBox::Yes);
    }

    // Invalidate cache for paths we're about to remove so stale entries don't
    // linger (a future re-install of the same folder should re-scan).
    for (auto *item : toRemove) {
        const QString mp = item->data(ModRole::ModPath).toString();
        if (!mp.isEmpty()) m_scans->invalidateDataFoldersCache(mp);
    }

    for (auto *item : toRemove)
        delete m_modList->takeItem(m_modList->row(item));

    if (deleteDisk) {
        const QString modsRootAbs = m_modsDir.isEmpty()
            ? QString()
            : QDir::cleanPath(QDir(m_modsDir).absolutePath());
        int keptShared = 0;
        for (const QString &p : installedPaths) {
            // Shared with another profile? Keep the files on disk - the row has
            // already been removed from this profile (the correct "remove from
            // this profile only" outcome).  Removing the LAST reference falls
            // through naturally (the scan returns false) and deletes.
            if (modPathReferencedByOtherProfile(mod_sharing::cleanModPath(p))) {
                ++keptShared;
                continue;
            }
            QDir(p).removeRecursively();
            // Sweep up empty wrapper directories left between the mods
            // root and the freshly-removed modPath.  Single-subdir
            // archives extract to "<mods>/<basename>/<inner>/…" and
            // store ModPath as the inner; without this loop the outer
            // "<basename>/" dir was left behind as an orphan.  Stops at
            // (but never removes) the mods root, and never touches a
            // directory that still has files in it.
            if (modsRootAbs.isEmpty()) continue;
            QString cur = QDir::cleanPath(QFileInfo(p).absolutePath());
            while (cur != modsRootAbs &&
                   cur.startsWith(modsRootAbs + "/"))
            {
                QDir parent(cur);
                if (!parent.exists()) break;
                if (!parent.entryList(
                        QDir::AllEntries | QDir::Hidden | QDir::System
                        | QDir::NoDotAndDotDot).isEmpty()) break;
                if (!parent.rmdir(cur)) break;
                cur = QDir::cleanPath(QFileInfo(cur).absolutePath());
            }
        }
        if (keptShared > 0)
            ui::info(this, T("remove_title"),
                     T("share_remove_kept_shared").arg(keptShared));
    }

    saveModList();
    updateModCount();
    scheduleConflictScan();

    // Auto-clear the text filter if the deletion left it pointing at nothing.
    // Without this, deleting the last (or only) match leaves the user staring
    // at an empty filtered list and forces them to manually clear the search
    // box before the rest of the modlist reappears.
    if (m_filterBar && m_filterBar->hasText()) {
        bool anyVisibleMod = false;
        for (int i = 0; i < m_modList->count(); ++i) {
            auto *it = m_modList->item(i);
            if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
            if (!it->isHidden()) { anyVisibleMod = true; break; }
        }
        if (!anyVisibleMod)
            m_filterBar->clearText();
    }

    if (removedPluginFilenames.isEmpty()) return;

    // Scan remaining mods' subfolders for redistributions of the plugins we
    // just removed.  Runs on a worker thread so the UI stays responsive while
    // it walks potentially hundreds of mod folders.
    //
    // Snapshot what we need before handing off to the worker:
    //   · redistEntries  - remaining installed mods (name + path)
    //   · cacheSnap      - a value-copy of the data-folders cache so the
    //                      worker can serve warm entries without touching
    //                      the live cache (no locking required)
    struct RedistEntry { QString name; QString path; };
    QList<RedistEntry> redistEntries;
    redistEntries.reserve(m_modList->count());
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *other = m_modList->item(i);
        if (other->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (other->data(ModRole::InstallStatus).toInt() != 1) continue;
        const QString hp = other->data(ModRole::ModPath).toString();
        if (hp.isEmpty()) continue;
        redistEntries.append({other->text(), hp});
    }

    auto cacheSnap = m_scans->dataFoldersSnapshot();   // value copy - safe to read off-thread
    const QStringList scanExts = kContentExts;    // local copy so the lambda can capture it

    auto *watcher = new QFutureWatcher<QList<QStringList>>(this);
    connect(watcher, &QFutureWatcher<QList<QStringList>>::finished, this,
        [this, watcher]() {
            watcher->deleteLater();
            const QList<QStringList> redistHits = watcher->result();
            if (redistHits.isEmpty()) return;

            QStringList bullets;
            bullets.reserve(redistHits.size());
            for (const QStringList &h : redistHits)
                bullets << "  • " + h.value(0) + " → " + h.value(2)
                                  + "  (" + h.value(3) + ")";
            QMessageBox box(this);
            box.setWindowTitle(T("redist_prompt_title"));
            box.setIcon(QMessageBox::Question);
            box.setText(T("redist_prompt_body").arg(redistHits.size()));
            box.setInformativeText(bullets.join('\n'));
            box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            box.setDefaultButton(QMessageBox::Yes);
            if (box.exec() == QMessageBox::Yes) {
                bool changed = false;
                for (const QStringList &h : redistHits) {
                    const QString key = h.value(1) + '\t' + h.value(2);
                    if (!m_declinedPatches.contains(key)) {
                        m_declinedPatches.insert(key);
                        changed = true;
                    }
                }
                if (changed) {
                    Settings::setDeclinedPatches(
                        QStringList(m_declinedPatches.begin(),
                                    m_declinedPatches.end()));
                    syncOpenMWConfig();
                }
            }
        });

    watcher->setFuture(QtConcurrent::run(
        [redistEntries = std::move(redistEntries),
         cacheSnap     = std::move(cacheSnap),
         removedPluginFilenames,
         scanExts]() -> QList<QStringList> {
            QList<QStringList> hits;
            for (const auto &entry : redistEntries) {
                const QString hostClean = QDir::cleanPath(entry.path);

                // Use the cache snapshot when available; fall back to disk scan.
                QList<QPair<QString, QStringList>> folders;
                auto it = cacheSnap.find(entry.path);
                if (it != cacheSnap.end()) {
                    const auto &all = it.value();
                    for (const auto &p : all) {
                        QStringList filtered;
                        for (const QString &f : p.second)
                            for (const QString &e : scanExts)
                                if (f.endsWith(e, Qt::CaseInsensitive)) { filtered << f; break; }
                        if (!filtered.isEmpty()) folders.append({p.first, filtered});
                    }
                } else {
                    folders = plugins::collectDataFolders(entry.path, scanExts);
                }

                for (const auto &p : folders) {
                    const QString dirClean = QDir::cleanPath(p.first);
                    if (dirClean == hostClean) continue;
                    const QString folderKey = QFileInfo(dirClean).fileName();
                    for (const QString &cf : p.second) {
                        if (removedPluginFilenames.contains(cf))
                            hits << QStringList{entry.name, entry.path, folderKey, cf};
                    }
                }
            }
            return hits;
        }));
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

void MainWindow::sendSelectedToSeparator(QListWidgetItem *sep)
{
    if (!sep) return;
    const auto selected = m_modList->selectedItems();
    if (selected.isEmpty()) return;

    int sepRow = m_modList->row(sep);
    QList<int> rows;
    for (auto *sel : selected) {
        if (sel->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        int r = m_modList->row(sel);
        if (r != sepRow) rows.append(r);
    }
    if (rows.isEmpty()) return;
    std::sort(rows.begin(), rows.end());

    m_undoStack->pushUndo();

    // Take in descending order so earlier indices stay valid; prepend so the
    // destination order matches the original top-to-bottom selection order.
    QList<QListWidgetItem *> picked;
    picked.reserve(rows.size());
    for (int k = rows.size() - 1; k >= 0; --k) {
        int r = rows[k];
        picked.prepend(m_modList->takeItem(r));
        if (r < sepRow) --sepRow;
    }

    // Insert at the *end* of the target section - i.e. just before the next
    // separator, or at the end of the list if this is the last section.
    int insertAt = sepRow + 1;
    while (insertAt < m_modList->count() &&
           m_modList->item(insertAt)->data(ModRole::ItemType).toString()
               != ItemType::Separator) {
        ++insertAt;
    }
    for (int k = 0; k < picked.size(); ++k)
        m_modList->insertItem(insertAt + k, picked[k]);

    m_modList->clearSelection();
    for (auto *it : picked) it->setSelected(true);
    if (!picked.isEmpty())
        m_modList->setCurrentItem(picked.first(), QItemSelectionModel::NoUpdate);

    saveModList();
    scheduleConflictScan();
}

void MainWindow::sendSelectedToEdge(bool toBeginning)
{
    const auto selected = m_modList->selectedItems();
    if (selected.isEmpty()) return;

    QList<int> rows;
    for (auto *sel : selected) {
        if (sel->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        rows.append(m_modList->row(sel));
    }
    if (rows.isEmpty()) return;
    std::sort(rows.begin(), rows.end());

    m_undoStack->pushUndo();

    // Take in descending order so earlier indices stay valid, then prepend
    // so the reinserted block keeps the original top-to-bottom order.
    QList<QListWidgetItem *> picked;
    picked.reserve(rows.size());
    for (int k = rows.size() - 1; k >= 0; --k)
        picked.prepend(m_modList->takeItem(rows[k]));

    // "Beginning" = row 0 (ahead of every separator too).  "End" = past the
    // last row.  The loop inserts in forward order starting at insertAt so
    // the block's internal order is preserved.
    int insertAt = toBeginning ? 0 : m_modList->count();
    for (int k = 0; k < picked.size(); ++k)
        m_modList->insertItem(insertAt + k, picked[k]);

    m_modList->clearSelection();
    for (auto *it : picked) it->setSelected(true);
    if (!picked.isEmpty())
        m_modList->setCurrentItem(picked.first(), QItemSelectionModel::NoUpdate);

    saveModList();
    scheduleConflictScan();
}

void MainWindow::openSendToDialog()
{
    if (auto *sep = send_to_dialog::pickSeparator(this, m_modList))
        sendSelectedToSeparator(sep);
}

void MainWindow::onContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_modList->itemAt(pos);
    QMenu menu(this);

    if (item) {
        bool isSep = item->data(ModRole::ItemType).toString() == ItemType::Separator;
        if (isSep) {
            menu.addAction(T("ctx_edit_separator"), this,
                [this, item]{ onEditSeparator(item); });

            bool collapsed = item->data(ModRole::Collapsed).toBool();
            menu.addAction(collapsed ? T("ctx_section_expand") : T("ctx_section_collapse"),
                this, [this, item, collapsed]{
                    m_undoStack->pushUndo();
                    collapseSection(item, !collapsed);
                    saveModList();
                });

            auto toggleSection = [this, item](Qt::CheckState state) {
                m_undoStack->pushUndo();
                int start = m_modList->row(item) + 1;
                for (int i = start; i < m_modList->count(); ++i) {
                    auto *it = m_modList->item(i);
                    if (it->data(ModRole::ItemType).toString() == ItemType::Separator)
                        break;
                    if (it->flags() & Qt::ItemIsUserCheckable)
                        it->setCheckState(state);
                }
                saveModList();
                scheduleConflictScan();
            };
            menu.addAction(T("ctx_section_enable_all"),  this,
                [toggleSection]{ toggleSection(Qt::Checked); });
            menu.addAction(T("ctx_section_disable_all"), this,
                [toggleSection]{ toggleSection(Qt::Unchecked); });
        } else {
            // -- "Send to" submenu ---
            //   · Separator…   (searchable picker, only if any separator exists)
            //   · Top of modlist
            //   · Bottom of modlist
            // Top/Bottom are always available - they're useful regardless of
            // whether the user has set up separators yet.
            {
                bool hasAnySep = false;
                for (int i = 0; i < m_modList->count() && !hasAnySep; ++i)
                    hasAnySep = m_modList->item(i)->data(ModRole::ItemType).toString()
                              == ItemType::Separator;

                auto *sendMenu = menu.addMenu(T("ctx_send_to"));
                if (hasAnySep) {
                    sendMenu->addAction(T("ctx_send_to_separator"), this,
                                        &MainWindow::openSendToDialog);
                    sendMenu->addSeparator();
                }
                sendMenu->addAction(T("ctx_send_to_top"), this,
                                    [this]{ sendSelectedToEdge(true); });
                sendMenu->addAction(T("ctx_send_to_bottom"), this,
                                    [this]{ sendSelectedToEdge(false); });
                menu.addSeparator();
            }

            int installStatus = item->data(ModRole::InstallStatus).toInt();
            if (installStatus == 1) {
                menu.addAction(T("ctx_open_folder"), this, [item]{
                    QString path = item->data(ModRole::ModPath).toString();
                    subprocess::startDetached("xdg-open", {path});
                });

                // Reinstall: only meaningful when we know the Nexus source.
                QString nexusUrl = item->data(ModRole::NexusUrl).toString();
                if (!nexusUrl.isEmpty()) {
                    auto *reinstallAct =
                    menu.addAction(T("ctx_reinstall"), this, [this, item]{
                        QString name = item->data(ModRole::CustomName).toString();
                        if (name.isEmpty()) name = item->text();

                        if (!ui::confirm(this, T("ctx_reinstall"),
                                T("reinstall_confirm").arg(name))) return;

                        item->setData(ModRole::InstallStatus, 0);
                        item->setData(ModRole::ModSize, QVariant());
                        item->setData(ModRole::HasMissingMaster, false);
                        item->setData(ModRole::MissingMasters, QStringList());
                        placeholder_state::restoreInteractiveFlags(item);
                        item->setText(name);
                        saveModList();

                        onInstallFromNexus(item);
                    });
#ifdef Q_OS_LINUX
                    // Freedesktop icon theme - falls back silently on DEs
                    // that lack both names.  Linux-only: Windows / macOS
                    // have no default theme so the lookup returns empty
                    // and the menu ends up with a gap instead of an icon.
                    reinstallAct->setIcon(
                        QIcon::fromTheme(QStringLiteral("view-refresh"),
                        QIcon::fromTheme(QStringLiteral("system-software-update"))));
#else
                    Q_UNUSED(reinstallAct);
#endif
                }

                menu.addAction(T("ctx_uninstall"), this, [this, item]{
                    QString path = item->data(ModRole::ModPath).toString();
                    QString name = item->data(ModRole::CustomName).toString();
                    if (name.isEmpty()) name = item->text();
                    if (!ui::confirm(this, T("ctx_uninstall"),
                        T("uninstall_confirm").arg(name))) return;
                    // Shared with another profile? Drop the row here but KEEP the
                    // files - another profile still points at this folder.
                    if (!path.isEmpty()
                        && modPathReferencedByOtherProfile(mod_sharing::cleanModPath(path))) {
                        delete m_modList->takeItem(m_modList->row(item));
                        saveModList();
                        ui::info(this, T("ctx_uninstall"), T("share_uninstall_kept_shared"));
                        return;
                    }
                    if (!path.isEmpty()) {
                        QDir dir(path);
                        if (dir.exists() && !dir.removeRecursively()) {
                            ui::warn(this, T("uninstall_error_title"), T("uninstall_error_body").arg(path));
                            return;
                        }
                    }
                    delete m_modList->takeItem(m_modList->row(item));
                    saveModList();
                });

                // Share this mod with another modlist profile (no file copy):
                // appends a row to that profile's modlist pointing at the same
                // folder, optionally carrying this profile's config.  Only when
                // the current game has more than one modlist profile.
                if (m_profiles && !m_profiles->isEmpty()
                    && m_profiles->current().modlistProfiles.size() > 1) {
                    auto *shareMenu = menu.addMenu(T("ctx_share_with_profile"));
                    const GameProfile &gp = m_profiles->current();
                    for (int pi = 0; pi < gp.modlistProfiles.size(); ++pi) {
                        if (pi == gp.activeModlistIdx) continue;   // skip the active one
                        const QString targetName = gp.modlistProfiles[pi].name;
                        const QString targetKey  = gp.id + QStringLiteral("__") + targetName;
                        shareMenu->addAction(targetName, this,
                            [this, item, targetKey, targetName]{
                            QString modName = item->data(ModRole::CustomName).toString();
                            if (modName.isEmpty()) modName = item->text();

                            QMessageBox box(this);
                            box.setWindowTitle(T("share_prompt_title"));
                            box.setIcon(QMessageBox::Question);
                            box.setText(T("share_prompt_body").arg(modName, targetName));
                            auto *copyBtn = box.addButton(T("share_prompt_copy"),
                                                          QMessageBox::AcceptRole);
                            auto *defBtn  = box.addButton(T("share_prompt_default"),
                                                          QMessageBox::ActionRole);
                            box.addButton(QMessageBox::Cancel);
                            box.setDefaultButton(copyBtn);
                            box.exec();
                            if (box.clickedButton() != copyBtn
                                && box.clickedButton() != defBtn) return;
                            shareModIntoProfile(ModEntry::fromItem(item), targetKey,
                                                box.clickedButton() == copyBtn);
                        });
                    }
                }
            }
            if (installStatus == 0) {
                QString nexusUrl = item->data(ModRole::NexusUrl).toString();
                if (!nexusUrl.isEmpty()) {
                    // Bulk install: if multiple rows are selected, queue them
                    // all at once instead of forcing one-at-a-time clicks.
                    // The clicked row is always included even if it wasn't in
                    // the selection (matches Qt's usual right-click-on-unselected
                    // behaviour of treating the clicked row as the target).
                    QList<QListWidgetItem*> bulk;
                    QSet<QListWidgetItem*>  bulkSet;
                    auto addIfEligible = [&](QListWidgetItem *it) {
                        if (!it || bulkSet.contains(it)) return;
                        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) return;
                        if (it->data(ModRole::InstallStatus).toInt() != 0) return;
                        if (it->data(ModRole::NexusUrl).toString().isEmpty()) return;
                        bulk << it;
                        bulkSet.insert(it);
                    };
                    addIfEligible(item);
                    for (auto *sel : m_modList->selectedItems()) addIfEligible(sel);

                    if (bulk.size() > 1) {
                        menu.addAction(T("ctx_install_bulk").arg(bulk.size()), this,
                            [this, bulk]{
                                m_bulkInstall->enqueue(bulk);
                            });
                    } else {
                        menu.addAction(T("ctx_install"), this, [this, item]{
                            onInstallFromNexus(item);
                        });
                    }
                }

                // Non-Nexus placeholder: offer a Nexus search + local-file install.
                if (installStatus == 0 && nexusUrl.isEmpty()) {
                    const QString srcUrl = item->data(ModRole::SourceUrl).toString();
                    if (!srcUrl.isEmpty()) {
                        // Known non-Nexus URL (GitHub release, etc.) stored on import.
                        menu.addAction(T("ctx_open_download_page"), this, [srcUrl] {
                            QDesktopServices::openUrl(QUrl(srcUrl));
                        });
                    } else {
                        // No stored URL - build a live Nexus search from the mod name.
                        QString displayName = item->data(ModRole::CustomName).toString();
                        if (displayName.isEmpty()) displayName = item->text();
                        const QString game = m_profiles->isEmpty()
                            ? QStringLiteral("morrowind") : currentProfile().id;
                        // Spaces encoded as '+' to match Nexus keyword search format.
                        const QString keyword = displayName.toLower().replace(' ', '+');
                        const QString searchUrl =
                            QString("https://www.nexusmods.com/games/%1/search?keyword=%2")
                                .arg(game, keyword);
                        menu.addAction(T("ctx_search_on_nexus"), this, [searchUrl] {
                            QDesktopServices::openUrl(QUrl(searchUrl));
                        });
                    }
                    menu.addAction(T("ctx_install_local"), this, [this, item] {
                        const QString path = QFileDialog::getOpenFileName(
                            this, T("ctx_install_local"),
                            QDir::homePath(),
                            T("install_archive_filter"));
                        if (path.isEmpty()) return;
                        if (const auto r = extractAndAdd(path, item); !r)
                            qCWarning(logging::lcInstall)
                                << "local install failed:" << r.error();
                    });
                }
            }
            QString nexusUrl = item->data(ModRole::NexusUrl).toString();
            if (!nexusUrl.isEmpty()) {
                menu.addAction(T("ctx_copy_nexus_url"), this, [nexusUrl]{
                    QApplication::clipboard()->setText(nexusUrl);
                });
                menu.addAction(T("ctx_view_changelog"), this, [this, item]{
                    onViewChangelog(item);
                });
            }
            if (installStatus == 2 && m_downloadQueue->isDownloadActive(item)) {
                menu.addAction(T("ctx_cancel_download"), this, [this, item]{
                    m_downloadQueue->cancelQueued(item);
                });
            }
            if (installStatus != 2) { // don't allow toggling while installing
                menu.addAction(
                    item->checkState() == Qt::Checked
                        ? T("ctx_disable") : T("ctx_enable"),
                    this, [this, item]{
                    item->setCheckState(item->checkState() == Qt::Checked
                        ? Qt::Unchecked : Qt::Checked);
                    saveModList();
                });

                // Utility-mod toggle: mark frameworks/libraries (Skill
                // Framework, OAAB_Data) so they get a grey background, distinct
                // from content mods. First toggle shows an explainer, rest are
                // silent. With a multi-selection, all items follow the right-
                // clicked item's state.
                const bool isUtil = item->data(ModRole::IsUtility).toBool();
                const QList<QListWidgetItem *> utilTargets = [&]{
                    QList<QListWidgetItem *> sel;
                    for (auto *it : m_modList->selectedItems())
                        if (it->data(ModRole::ItemType).toString() == ItemType::Mod)
                            sel << it;
                    if (!sel.contains(item)) sel.prepend(item);
                    return sel;
                }();
                menu.addAction(
                    isUtil ? T("ctx_unmark_utility") : T("ctx_mark_utility"),
                    this, [this, utilTargets, isUtil]{
                    if (!isUtil) {
                        if (!Settings::utilityExplainerSeen()) {
                            QMessageBox box(this);
                            box.setWindowTitle(T("utility_explainer_title"));
                            box.setIcon(QMessageBox::Information);
                            box.setText(T("utility_explainer_body"));
                            auto *okBtn = box.addButton(
                                T("utility_explainer_ok"),
                                QMessageBox::AcceptRole);
                            box.addButton(QMessageBox::Cancel);
                            box.setDefaultButton(okBtn);
                            box.exec();
                            if (box.clickedButton() != okBtn) return;
                            Settings::setUtilityExplainerSeen(true);
                        }
                    }
                    m_undoStack->pushUndo();
                    for (auto *it : utilTargets) {
                        it->setData(ModRole::IsUtility, !isUtil);
                        m_modList->update(m_modList->indexFromItem(it));
                    }
                    saveModList();
                });

                // Favourite toggle: same as the hovering ★ icon, for users who
                // don't find the icon. No explainer - the gold star is obvious.
                const bool isFav = item->data(ModRole::IsFavorite).toBool();
                const QList<QListWidgetItem *> favTargets = [&]{
                    QList<QListWidgetItem *> sel;
                    for (auto *it : m_modList->selectedItems())
                        if (it->data(ModRole::ItemType).toString() == ItemType::Mod)
                            sel << it;
                    if (!sel.contains(item)) sel.prepend(item);
                    return sel;
                }();
                menu.addAction(
                    isFav ? T("ctx_unmark_favorite") : T("ctx_mark_favorite"),
                    this, [this, favTargets, isFav]{
                    m_undoStack->pushUndo();
                    for (auto *it : favTargets) {
                        it->setData(ModRole::IsFavorite, !isFav);
                        m_modList->update(m_modList->indexFromItem(it));
                    }
                    saveModList();
                });
            }
        }
        menu.addSeparator();
        // Precise placement: drop a new separator directly above the
        // right-clicked row.  Always a visible row, so it never lands inside a
        // collapsed neighbour's hidden children.  (The Mods menu / empty-space
        // "Add separator" appends at the end instead.)
        menu.addAction(T("ctx_add_separator_above"), this, [this, item]{
            addSeparatorAtRow(m_modList->row(item));
        });
        menu.addAction(T("ctx_remove"), this, &MainWindow::onRemoveSelected);
    } else {
        menu.addAction(T("ctx_add_separator"), this, &MainWindow::onAddSeparator);
        menu.addAction(T("ctx_add_mod"),       this, &MainWindow::onAddMod);
    }

    menu.exec(m_modList->viewport()->mapToGlobal(pos));
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

void MainWindow::onItemDoubleClicked(QListWidgetItem *item)
{
    if (!item) return;

    if (item->data(ModRole::ItemType).toString() == ItemType::Separator) {
        onEditSeparator(item);
    } else {
        // Mod: edit display name and personal annotation
        QDialog dlg(this);
        dlg.setWindowTitle(T("mod_edit_title"));
        dlg.setMinimumWidth(460);

        auto *layout    = new QVBoxLayout(&dlg);
        auto *form      = new QFormLayout;
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);

        QFileInfo fi(item->data(ModRole::ModPath).toString());
        QString storedName = item->data(ModRole::CustomName).toString();
        auto *nameEdit  = new QLineEdit(storedName.isEmpty() ? fi.fileName() : storedName);
        nameEdit->setPlaceholderText(fi.fileName());
        form->addRow(T("mod_edit_name"), nameEdit);

        auto *annotEdit = new QPlainTextEdit(item->data(ModRole::Annotation).toString());
        annotEdit->setPlaceholderText(T("mod_edit_annot_ph"));
        annotEdit->setMinimumHeight(96);
        annotEdit->setTabChangesFocus(true);
        form->addRow(T("mod_edit_annot"), annotEdit);

        auto *videoUrlEdit = new QLineEdit(item->data(ModRole::VideoUrl).toString());
        videoUrlEdit->setPlaceholderText("https://youtube.com/watch?v=...");
        form->addRow(T("mod_edit_video_url"), videoUrlEdit);

        layout->addLayout(form);

        QString nexusUrl = item->data(ModRole::NexusUrl).toString();
        if (!nexusUrl.isEmpty()) {
            auto *extraRow = new QHBoxLayout;
            auto *visitBtn = new QPushButton(T("mod_edit_visit_page"));
            connect(visitBtn, &QPushButton::clicked, &dlg, [nexusUrl]{
                QDesktopServices::openUrl(QUrl(nexusUrl));
            });
            extraRow->addWidget(visitBtn);
            extraRow->addStretch();
            layout->addLayout(extraRow);
        }

        // -- Dependency section ---
        // Build url→displayName map for all OTHER installed mods.
        QMap<QString, QString> urlToName;
        for (int i = 0; i < m_modList->count(); ++i) {
            auto *it = m_modList->item(i);
            if (it == item) continue;
            if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
            QString u = it->data(ModRole::NexusUrl).toString();
            if (!u.isEmpty()) urlToName[u] = it->text();
        }

        // "Depends on" - editable
        auto *depsGroup = new QGroupBox(T("mod_edit_deps_on"));
        auto *depsGroupLayout = new QVBoxLayout(depsGroup);
        auto *depsList = new QListWidget;
        depsList->setMaximumHeight(90);
        depsList->setSelectionMode(QAbstractItemView::SingleSelection);
        QStringList currentDeps = item->data(ModRole::DependsOn).toStringList();
        for (const QString &url : currentDeps) {
            auto *di = new QListWidgetItem(urlToName.value(url, url), depsList);
            di->setData(Qt::UserRole, url);
        }
        auto *depsButtons = new QHBoxLayout;
        auto *addDepBtn = new QPushButton(T("mod_edit_deps_add"));
        auto *remDepBtn = new QPushButton(T("mod_edit_deps_remove"));
        depsButtons->addWidget(addDepBtn);
        depsButtons->addWidget(remDepBtn);
        depsButtons->addStretch();
        depsGroupLayout->addWidget(depsList);
        depsGroupLayout->addLayout(depsButtons);
        layout->addWidget(depsGroup);

        connect(addDepBtn, &QPushButton::clicked, &dlg, [&]() {
            QStringList alreadyIn;
            for (int i = 0; i < depsList->count(); ++i)
                alreadyIn << depsList->item(i)->data(Qt::UserRole).toString();

            // Collect candidates sorted by display name (case-insensitive).
            // urlToName is keyed by URL (mod-ID order), which is unhelpful here.
            QList<QPair<QString,QString>> candidates; // {displayName, url}
            for (auto it2 = urlToName.begin(); it2 != urlToName.end(); ++it2) {
                if (alreadyIn.contains(it2.key())) continue;
                candidates.append({it2.value(), it2.key()});
            }
            std::sort(candidates.begin(), candidates.end(),
                      [](const QPair<QString,QString> &a, const QPair<QString,QString> &b) {
                          return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
                      });

            QDialog picker(&dlg);
            picker.setWindowTitle(T("mod_edit_deps_pick"));
            picker.setMinimumWidth(380);
            picker.setMinimumHeight(420);
            auto *pl = new QVBoxLayout(&picker);
            auto *searchBox = new QLineEdit;
            searchBox->setPlaceholderText(T("mod_edit_deps_search"));
            searchBox->setClearButtonEnabled(true);
            pl->addWidget(searchBox);
            auto *pw = new QListWidget;
            auto populateList = [&](const QString &filter) {
                pw->clear();
                for (const auto &c : candidates) {
                    if (!filter.isEmpty()
                            && !c.first.contains(filter, Qt::CaseInsensitive))
                        continue;
                    auto *pi = new QListWidgetItem(c.first, pw);
                    pi->setData(Qt::UserRole, c.second);
                }
                if (pw->count() > 0) pw->setCurrentRow(0);
            };
            populateList(QString());
            connect(searchBox, &QLineEdit::textChanged, [&](const QString &t) {
                populateList(t);
            });
            pl->addWidget(pw);
            auto *pb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            pl->addWidget(pb);
            connect(pb, &QDialogButtonBox::accepted, &picker, &QDialog::accept);
            connect(pb, &QDialogButtonBox::rejected, &picker, &QDialog::reject);
            connect(pw, &QListWidget::itemDoubleClicked, &picker, &QDialog::accept);
            searchBox->setFocus();
            if (picker.exec() != QDialog::Accepted) return;
            auto *sel = pw->currentItem();
            if (!sel) return;
            auto *ni = new QListWidgetItem(sel->text(), depsList);
            ni->setData(Qt::UserRole, sel->data(Qt::UserRole));
        });

        connect(remDepBtn, &QPushButton::clicked, &dlg, [&]() {
            delete depsList->currentItem();
        });

        // "Required by" - read-only reverse lookup
        QStringList usedBy;
        for (int i = 0; i < m_modList->count(); ++i) {
            auto *it = m_modList->item(i);
            if (it == item) continue;
            if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
            if (!nexusUrl.isEmpty() &&
                it->data(ModRole::DependsOn).toStringList().contains(nexusUrl))
                usedBy << it->text();
        }
        if (!usedBy.isEmpty()) {
            auto *usedGroup = new QGroupBox(T("mod_edit_required_by"));
            auto *usedLayout = new QVBoxLayout(usedGroup);
            auto *usedList = new QListWidget;
            usedList->setMaximumHeight(90);
            for (const QString &name : usedBy)
                new QListWidgetItem(name, usedList);
            usedList->setEnabled(false);
            usedLayout->addWidget(usedList);
            layout->addWidget(usedGroup);
        }
        // -- End dependency section ---

        auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(btns);
        connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        if (dlg.exec() != QDialog::Accepted) return;
        m_undoStack->pushUndo();

        QString newName = nameEdit->text().trimmed();
        QString annot   = annotEdit->toPlainText().trimmed();
        QString videoUrl = videoUrlEdit->text().trimmed();
        if (!videoUrl.isEmpty() && !QUrl(videoUrl).isValid())
            videoUrl.clear();

        if (newName.isEmpty() || newName == fi.fileName()) {
            item->setText(fi.fileName());
            item->setData(ModRole::CustomName, QVariant());
        } else {
            item->setText(newName);
            item->setData(ModRole::CustomName, newName);
        }
        item->setData(ModRole::Annotation, annot);
        if (!videoUrl.isEmpty())
            item->setData(ModRole::VideoUrl, videoUrl);
        else
            item->setData(ModRole::VideoUrl, QVariant());

        // Save updated dependency list
        QStringList newDeps;
        for (int i = 0; i < depsList->count(); ++i)
            newDeps << depsList->item(i)->data(Qt::UserRole).toString();
        item->setData(ModRole::DependsOn, newDeps);

        QString modPath = item->data(ModRole::ModPath).toString();
        item->setToolTip(annot.isEmpty() ? modPath : modPath + "\n\n" + annot);
        m_modList->update(m_modList->indexFromItem(item));
        saveModList();
    }
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

void MainWindow::loadLoadOrder()
{
    m_loadOrder.clear();
    QFile f(loadOrderPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QString contents = QString::fromUtf8(f.readAll());
    f.close();
    // parseLoadOrder transparently handles v1 (line-per-plugin, optional
    // # comments) and v2 (schema header followed by line-per-plugin).
    m_loadOrder = modlist_serializer::parseLoadOrder(contents);
}

void MainWindow::saveLoadOrder()
{
    QFile f(loadOrderPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return;
    QTextStream out(&f);
    out << modlist_serializer::serializeLoadOrder(m_loadOrder);
}

// Reads the `content=` list from openmw.cfg in their current order.
// Linux path only for now - matches the rest of our Morrowind-profile code.
// Used after LOOT writes a newly-sorted cfg to pull the order back into
// m_loadOrder, and at startup to respect reorders done in OpenMW Launcher.
static QStringList readOpenMWContentOrder()
{
    QStringList out;
    QString path = QDir::homePath() + "/.config/openmw/openmw.cfg";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.startsWith("content="))
            out.append(line.mid(8));
    }
    return out;
}

// If openmw.cfg has been modified more recently than our loadorder file
// (typically because the user reordered plugins in the OpenMW Launcher
// between sessions), rewrite m_loadOrder so the managed content= order from
// openmw.cfg wins. Without this step, Nerevarine would overwrite the
// launcher's changes on the next syncOpenMWConfig() call.
//
// Only active for the Morrowind profile (that's where we sync openmw.cfg).
// Entries in m_loadOrder that don't appear in openmw.cfg (disabled mods,
// plugins the launcher filtered out, etc.) keep their relative positions.
void MainWindow::absorbExternalLoadOrder()
{
    if (m_profiles->isEmpty() || currentProfile().id != "morrowind") return;

    const QString cfgPath      = QDir::homePath() + "/.config/openmw/openmw.cfg";
    const QString launcherPath = QDir::homePath() + "/.config/openmw/launcher.cfg";
    const QFileInfo cfgInfo(cfgPath);
    const QFileInfo launcherInfo(launcherPath);
    if (!cfgInfo.exists() && !launcherInfo.exists()) return;

    const QFileInfo loInfo(loadOrderPath());
    const QDateTime  loMtime  = loInfo.exists()  ? loInfo.lastModified()       : QDateTime();
    const QDateTime  cfgMtime = cfgInfo.exists() ? cfgInfo.lastModified()      : QDateTime();
    const QDateTime  lcMtime  = launcherInfo.exists() ? launcherInfo.lastModified() : QDateTime();

    // Two external signals, two separate mtime gates.  Either one being
    // newer than our loadorder file means the user touched something we
    // haven't absorbed yet and our m_loadOrder is a stale baseline.
    const bool cfgNewer      = cfgInfo.exists()      && (!loInfo.exists() || cfgMtime > loMtime);
    const bool launcherNewer = launcherInfo.exists() && (!loInfo.exists() || lcMtime  > loMtime);
    if (!cfgNewer && !launcherNewer) return;

    QStringList merged = m_loadOrder;

    // Pass 1: openmw.cfg. Catches the launcher-reorder case: user reorders,
    // clicks Save/Play, launcher writes content= order to openmw.cfg.
    if (cfgNewer) {
        const QStringList cfgOrder = readOpenMWContentOrder();
        if (!cfgOrder.isEmpty())
            merged = loadorder::mergeLoadOrder(merged, cfgOrder);
    }

    // Pass 2: launcher.cfg's current profile content= list.  The
    // launcher updates this on EVERY in-UI reorder, even when the user
    // closes without clicking Save/Play - openmw.cfg stays on the old
    // order but launcher.cfg already reflects the new one. Without
    // reading this signal here, the launcher.cfg sync would rewrite
    // launcher.cfg from a stale m_loadOrder and clobber the reorder.
    if (launcherNewer) {
        QFile lf(launcherPath);
        if (lf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString text = QString::fromUtf8(lf.readAll());
            lf.close();
            const QStringList launcherOrder =
                openmw::readLauncherCfgContentOrder(text);
            if (!launcherOrder.isEmpty())
                merged = loadorder::mergeLoadOrder(merged, launcherOrder);
        }
    }

    if (merged != m_loadOrder) {
        m_loadOrder = merged;
        saveLoadOrder();
    }
}

// Ensure m_loadOrder contains exactly the plugin filenames that are currently
// installed (any mod with InstallStatus=1), preserving existing positions for
// plugins already present. New plugins are appended to the end; a topological
// pass at the end enforces "masters above dependents" using the MAST entries
// declared inside each plugin so OpenMW never sees a parent loaded after its
// child (the "Stargazer - Telescopes Cyrodiil asks for parent file
// Stargazer - Telescopes" crash). Uninstalled plugins are removed.
void MainWindow::reconcileLoadOrder()
{
    static const QStringList contentExts{".esp", ".esm", ".omwaddon", ".omwscripts"};

    // filename → absolute path for every installed plugin we see.  Used
    // both to decide presence and to feed readTes3Masters() below.
    // Routed through the ScanCoordinator cache so a saveModList that fires
    // on every checkbox toggle / rename / annotation edit doesn't pay for
    // a full per-mod recursive directory walk every time.  Cache is
    // invalidated explicitly in addModFromPath / onRemoveSelected for
    // paths whose contents we just changed.
    QHash<QString, QString> pathByName;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 1) continue;
        QString modPath = item->data(ModRole::ModPath).toString();
        for (const auto &p : m_scans->cachedDataFolders(modPath, contentExts))
            for (const QString &cf : p.second)
                pathByName.insert(cf, p.first + "/" + cf);
    }

    // Drop plugins that no longer have an installed owner.
    auto endIt = std::remove_if(m_loadOrder.begin(), m_loadOrder.end(),
        [&](const QString &cf) { return !pathByName.contains(cf); });
    m_loadOrder.erase(endIt, m_loadOrder.end());

    // Append new plugins. Preserve the order they were discovered in - walk
    // the modlist top-to-bottom so newly-installed mods land beneath the
    // existing list in a predictable position; the topo pass below will
    // lift their masters above any dependents.
    QSet<QString> inList(m_loadOrder.begin(), m_loadOrder.end());
    // Saved order (identity unless a temporary view sort is active) so newly
    // discovered plugins append in load-order position, never the sorted display.
    const QList<int> loOrder = rowOrderForPersist();
    for (int oi = 0; oi < loOrder.size(); ++oi) {
        auto *item = m_modList->item(loOrder[oi]);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 1) continue;
        QString modPath = item->data(ModRole::ModPath).toString();
        for (const auto &p : m_scans->cachedDataFolders(modPath, contentExts)) {
            // .esm first within a mod folder, then .esp - a best-effort
            // first guess.  The topo pass below is what actually enforces
            // the master relationship for .omwaddon children whose parents
            // happen to sort alphabetically after them.
            QStringList files = p.second;
            std::ranges::sort(files, std::ranges::less{},
                [](const QString &s) {
                    // Projection to a tuple: !isEsm sorts masters (false) before
                    // plugins (true); lowercase name breaks ties alphabetically.
                    return std::pair{!s.endsWith(".esm", Qt::CaseInsensitive),
                                      s.toLower()};
                });
            for (const QString &cf : files) {
                if (!inList.contains(cf)) {
                    m_loadOrder.append(cf);
                    inList.insert(cf);
                }
            }
        }
    }

    // Topological pass: for each plugin, lift its declared masters above
    // it.  Routed through ScanCoordinator's (path, mtime) cache so a
    // saveModList that fires on rename / annotation / favorite toggle
    // doesn't re-read every plugin header on every call.  The
    // per-reconcile QHash is still useful to dedup the same `name`
    // looked up multiple times within a single topo sort - it's a
    // hot-path memo on top of the cross-call mtime cache.
    QHash<QString, QStringList> reconcileMemo;
    m_loadOrder = loadorder::topologicallySortByMasters(
        m_loadOrder,
        [this, &pathByName, &reconcileMemo](const QString &name) -> QStringList {
            auto it = reconcileMemo.constFind(name);
            if (it != reconcileMemo.constEnd()) return it.value();
            QStringList ms = m_scans
                ? m_scans->cachedTes3Masters(pathByName.value(name))
                : readTes3Masters(pathByName.value(name));
            reconcileMemo.insert(name, ms);
            return ms;
        });
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

static QString detectLootBinary()
{
    const QStringList binNames = {
#ifdef Q_OS_WIN
        "LOOT.exe", "loot.exe",
#else
        "loot", "LOOT",
#endif
    };
    const QStringList pathDirs = qEnvironmentVariable("PATH").split(
#ifdef Q_OS_WIN
        ';'
#else
        ':'
#endif
    );
    for (const QString &dir : pathDirs)
        for (const QString &bn : binNames) {
            QString c = QDir(dir).filePath(bn);
            if (QFileInfo::exists(c)) return c;
        }

#ifdef Q_OS_WIN
    const QStringList win = {
        "C:/Program Files/LOOT/LOOT.exe",
        "C:/Program Files (x86)/LOOT/LOOT.exe",
        QDir::homePath() + "/AppData/Local/LOOT/LOOT.exe",
    };
    for (const QString &p : win) if (QFileInfo::exists(p)) return p;
#else
    const QStringList nix = {
        "/var/lib/flatpak/exports/bin/io.github.loot.loot",
        QDir::homePath() + "/.local/share/flatpak/exports/bin/io.github.loot.loot",
        "/usr/bin/loot",
        "/usr/local/bin/loot",
        "/snap/bin/loot",
    };
    for (const QString &p : nix) if (QFileInfo::exists(p)) return p;
#endif
    return QString();
}

// If `lootPath` is a Flatpak export symlink (…/flatpak/exports/bin/<app.id>),
// return its Flatpak app id (e.g. "io.github.loot.loot"); otherwise empty.
// Running the symlink directly runs in LOOT's default sandbox, which can't read
// mods dirs (often on /mnt/...) nor write ~/.config/openmw/openmw.cfg - so it
// finds no plugins and exits. Non-empty result means launch via `flatpak run`
// with explicit --filesystem grants (see lootCommand()).
static QString lootFlatpakAppId(const QString &lootPath)
{
#ifndef Q_OS_WIN
    if (lootPath.contains(QLatin1String("/flatpak/exports/bin/"))) {
        const QString id = QFileInfo(lootPath).fileName();
        if (id.contains(QLatin1Char('.')))   // app ids are reverse-DNS
            return id;
    }
#endif
    return {};
}

// Build the actual (program, args) to run LOOT with `lootArgs` (e.g.
// --auto-sort --game <slug>).  Native installs run directly; a Flatpak install
// is wrapped in `flatpak run` with filesystem access so the sandboxed LOOT can
// see the plugins and the game config:
//   --filesystem=host             read plugins from any mods dir / mount
//   --filesystem=xdg-config/openmw read+write the OpenMW config (LOOT rewrites
//                                  openmw.cfg's content= order) at its real path
// LOOT is a user-installed, trusted tool, so host access is acceptable; the
// alternative (enumerating every data= path) would silently miss a mods root
// and reproduce the exact "no plugins" failure this fixes.
static QPair<QString, QStringList>
lootCommand(const QString &lootPath, const QStringList &lootArgs)
{
    const QString fpId = lootFlatpakAppId(lootPath);
    if (fpId.isEmpty())
        return {lootPath, lootArgs};

    QStringList args{
        QStringLiteral("run"),
        QStringLiteral("--filesystem=host"),
        QStringLiteral("--filesystem=xdg-config/openmw"),
        fpId,
    };
    args += lootArgs;
    return {QStringLiteral("flatpak"), args};
}

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
static QString lootGameFor(const QString &profileId)
{
    if (const GameAdapter *a = GameAdapterRegistry::find(profileId))
        return a->lootSlug();
    return {};
}

void MainWindow::autoSortLoadOrder()
{
    if (m_loadOrder.size() <= 1) return;

    QString loot = detectLootBinary();
    if (loot.isEmpty()) {
        statusBar()->showMessage(T("loot_missing"), 6000);
        return;
    }
    QString game = lootGameFor(currentProfile().id);
    if (game.isEmpty()) {
        statusBar()->showMessage(T("loot_unsupported_profile"), 5000);
        return;
    }

    // Make sure openmw.cfg on disk reflects our current m_loadOrder - LOOT
    // reads & writes whatever is in the cfg.
    syncGameConfig();

    // Modal dialog that streams LOOT's merged stdout/stderr so the user can
    // see progress (LOOT prints per-plugin lines while it works) and any
    // warnings before the dialog is dismissed.  QProcess is parented to the
    // dialog; closing the dialog mid-run kills the process.
    QDialog dlg(this);
    dlg.setWindowTitle(T("loot_dialog_title"));
    dlg.setMinimumSize(720, 460);
    auto *v = new QVBoxLayout(&dlg);

    auto *header = new QLabel(T("loot_dialog_header"), &dlg);
    header->setWordWrap(true);
    header->setStyleSheet("padding: 4px 2px;");
    v->addWidget(header);

    // Native LOOT runs directly; a Flatpak LOOT is wrapped in `flatpak run`
    // with filesystem grants so the sandbox can reach the mods + openmw.cfg.
    const QStringList lootArgs{"--auto-sort", "--game", game};
    const auto [prog, args] = lootCommand(loot, lootArgs);
    auto *cmdLbl = new QLabel(
        T("loot_dialog_command").arg(prog + " " + args.join(' ')), &dlg);
    cmdLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cmdLbl->setStyleSheet("color: #666; font-family: monospace;");
    v->addWidget(cmdLbl);

    auto *log = new QPlainTextEdit(&dlg);
    log->setReadOnly(true);
    log->setLineWrapMode(QPlainTextEdit::NoWrap);
    log->setFont(QFont("monospace"));
    v->addWidget(log, 1);

    auto *statusLbl = new QLabel(T("loot_dialog_running"), &dlg);
    statusLbl->setStyleSheet("font-weight: bold; padding: 2px;");
    v->addWidget(statusLbl);

    auto *btns = new QDialogButtonBox(&dlg);
    auto *actBtn = btns->addButton(T("loot_dialog_cancel"),
                                   QDialogButtonBox::RejectRole);
    v->addWidget(btns);

    auto *proc = new QProcess(&dlg);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    // Strip our AppImage's Qt/library env so LOOT (also Qt) loads its own
    // plugins instead of ours. See subprocess::childEnvironment().
    subprocess::applyEnv(*proc);

    // Track outcome on the dialog instance so the code after exec() knows
    // whether to splice the new load order back.  Default = "we didn't
    // successfully finish", which matches every failure/cancel path.
    bool finishedOk = false;

    connect(proc, &QProcess::readyReadStandardOutput, log, [proc, log]() {
        log->moveCursor(QTextCursor::End);
        log->insertPlainText(QString::fromUtf8(proc->readAllStandardOutput()));
        log->moveCursor(QTextCursor::End);
    });

    connect(proc, &QProcess::errorOccurred, &dlg,
            [statusLbl, actBtn](QProcess::ProcessError) {
        statusLbl->setText(T("loot_dialog_crashed"));
        actBtn->setText(T("loot_dialog_close"));
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            &dlg, [&, statusLbl, actBtn](int code, QProcess::ExitStatus st) {
        if (st == QProcess::NormalExit && code == 0) {
            finishedOk = true;
            statusLbl->setText(T("loot_dialog_done_ok"));
        } else {
            statusLbl->setText(T("loot_dialog_done_nonzero").arg(code));
        }
        actBtn->setText(T("loot_dialog_close"));
    });

    // While running this button cancels (kills proc, rejects dialog).
    // After finish (whether OK or not) we repurpose it as "Close"; the
    // button still maps to reject so the dialog just closes.
    connect(actBtn, &QPushButton::clicked, &dlg, [&]() {
        if (proc->state() != QProcess::NotRunning) {
            proc->kill();
            proc->waitForFinished(2000);
            statusLbl->setText(T("loot_dialog_cancelled"));
        }
        dlg.reject();
    });

    proc->start(prog, args);
    if (!proc->waitForStarted(5000)) {
        statusBar()->showMessage(T("loot_launch_failed"), 6000);
        return;
    }

    dlg.exec();

    if (!finishedOk) return;

    // Pull the new order back from openmw.cfg and splice it into m_loadOrder,
    // preserving the relative positions of plugins whose mods are disabled
    // (those aren't in the cfg but still live in m_loadOrder).
    const QStringList sortedActive = readOpenMWContentOrder();
    if (sortedActive.isEmpty()) {
        statusBar()->showMessage(T("loot_empty_cfg"), 6000);
        return;
    }

    QSet<QString> activeSet(sortedActive.begin(), sortedActive.end());
    QStringList merged;
    int idx = 0;
    for (const QString &cf : m_loadOrder) {
        if (activeSet.contains(cf)) {
            if (idx < sortedActive.size())
                merged.append(sortedActive[idx++]);
        } else {
            merged.append(cf);
        }
    }
    while (idx < sortedActive.size()) merged.append(sortedActive[idx++]);
    m_loadOrder = merged;

    statusBar()->showMessage(T("loot_done"), 4000);
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

void MainWindow::saveModList()
{
    // Coalesce: any pending debounce timer's fire is now redundant -
    // a synchronous save is happening anyway.  Safe even when no
    // debounce was pending (stop() is a no-op then).
    if (m_saveModListTimer) m_saveModListTimer->stop();

    // Safety guard: never overwrite an existing non-empty modlist file with an
    // empty in-memory list BEFORE loadModList has populated it.  Once the
    // initial load has run, an empty list is a legitimate user delete and
    // must be persisted (otherwise deleting your last mod silently un-deletes
    // it on next launch).
    if (!m_modListLoaded && m_modList->count() == 0 && QFile::exists(modlistPath())) {
        QFile guard(modlistPath());
        if (guard.open(QIODevice::ReadOnly | QIODevice::Text) && !guard.readAll().trimmed().isEmpty())
            return;
    }

    QElapsedTimer total;
    total.start();
    QElapsedTimer phase;
    phase.start();

    // Pick up any reorder the user did in the OpenMW Launcher between
    // launches.  Without this, saving the modlist (triggered by any mod-list
    // mutation - checkbox toggle, drag-drop, install) would rewrite
    // openmw.cfg from the stale m_loadOrder and wipe the launcher's reorder.
    // absorbExternalLoadOrder is mtime-gated so it's a no-op when we wrote
    // loadorder more recently than openmw.cfg, which correctly leaves our
    // own reorders (Edit Load Order dialog, LOOT sort) untouched.
    absorbExternalLoadOrder();
    const qint64 ms_absorb = phase.restart();

    // Snapshot the QListWidget into typed ModEntry rows, then run the
    // schema-versioned JSONL serializer.  Mid-install placeholders that
    // have nothing useful to persist (no NexusUrl) are dropped here so
    // the v2 file doesn't carry garbage rows; matches v1 behaviour.
    // snapshotEntriesForPersist walks rows in saved order, so a temporary
    // Size/Date view sort never rewrites the modlist file (identity walk when
    // no view sort is active).
    QList<ModEntry> entries = snapshotEntriesForPersist();
    for (int i = entries.size() - 1; i >= 0; --i) {
        const ModEntry &e = entries[i];
        if (e.isMod() && e.installStatus == 2 && e.nexusUrl.isEmpty()) {
            entries.removeAt(i);
            continue;
        }
        // Strip the spinner prefix from a placeholder's display name
        // ("⠋ Installing (...)") - the JSONL form stores the raw name.
        if (e.isMod() && e.installStatus == 2
            && e.customName.isEmpty()
            && e.displayName.startsWith(QStringLiteral("⠋ "))) {
            entries[i].displayName = e.displayName.mid(2);
        }
    }
    const QString modlistContent = modlist_serializer::serializeModlist(entries);
    const QString modlistFile = modlistPath();

    // Snapshot backup + modlist file write moved off the UI thread.  These
    // are pure-IO and previously sat squarely inside the user-perceived
    // grey-freeze on Add/Edit.  closeEvent() waits on m_lastSaveFuture
    // before exiting so unsaved state still flushes before the process
    // dies.  Earlier in-flight saves are awaited synchronously here so we
    // don't have two writers racing on the same file (mtime ordering also
    // matters - absorbExternalLoadOrder above checks loadorder vs
    // openmw.cfg mtime).
    if (m_lastSaveFuture.isRunning())
        m_lastSaveFuture.waitForFinished();
    QPointer<MainWindow> safeSelf(this);
    m_lastSaveFuture = QtConcurrent::run(
        [safeSelf, modlistFile, modlistContent]() {
            // modlist_io::writeModlistFile is unit-tested in
            // tests/test_modlist_io.cpp - the carve-out is what makes
            // the failure path (read-only mount, full disk) exercisable
            // without spinning up a MainWindow.
            auto err = modlist_io::writeModlistFile(modlistFile, modlistContent);
            if (err.has_value()) {
                const QString reason = *err;
                QMetaObject::invokeMethod(safeSelf.data(),
                    [safeSelf, modlistFile, reason]() {
                        if (!safeSelf) return;
                        safeSelf->onAsyncWriteFailed(modlistFile, reason);
                    }, Qt::QueuedConnection);
            }
        });
    const qint64 ms_write = phase.restart();

    // Keep the separate plugin load-order file in sync with the modlist
    // (additions/removals only; order-within-the-list is edited explicitly
    // by the user via Mods → Edit Load Order… or by autoSortLoadOrder()).
    reconcileLoadOrder();
    saveLoadOrder();
    const qint64 ms_loadorder = phase.restart();

    syncGameConfig();
    const qint64 ms_syncCfg = phase.restart();

    scanMissingMasters();
    const qint64 ms_masters = phase.restart();

    scanMissingDependencies();
    const qint64 ms_deps = phase.restart();

    updateSectionCounts();
    const qint64 ms_counts = phase.elapsed();

    // Per-stage ms in log.txt: a single number jumping pins a UI-freeze.
    qCInfo(logging::lcModList).nospace()
        << "saveModList ms total=" << total.elapsed()
        << " absorb=" << ms_absorb
        << " writeQueue=" << ms_write
        << " loadorder=" << ms_loadorder
        << " syncCfg=" << ms_syncCfg
        << " masters=" << ms_masters
        << " deps=" << ms_deps
        << " counts=" << ms_counts
        << " items=" << m_modList->count();
}


void MainWindow::syncGameConfig()
{
    if (m_profiles->isEmpty()) return;
    const QString &id = currentProfile().id;

    if (id == "morrowind") {
        syncOpenMWConfig();
        return;
    }
    // TODO: implement per-engine writers as needed:
    //   oblivion / oblivionremastered   → %USERPROFILE%/Documents/My Games/.../Plugins.txt
    //   skyrim / skyrimspecialedition   → %USERPROFILE%/Documents/My Games/.../Plugins.txt
    //   fallout3 / falloutnewvegas / fallout4  → similar Plugins.txt conventions
    // For now, non-Morrowind profiles rely on the user's own launcher/config tools.
}

// Drag-and-drop local archives onto the main window → install
//
// No download step: copy the dragged file into m_modsDir, make a
// placeholder row, then hand off to the same extractAndAdd pipeline that
// NXM downloads use (so FOMOD wizards, conflict scans, nexus-title rename
// etc. all fire the same way).  Refuses archives whose suffix we don't
// know how to extract.

static bool isInstallableArchiveSuffix(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == "7z" || s == "zip" || s == "rar" || s == "fomod"
        || s == "tar" || s == "gz"  || s == "bz2" || s == "xz";
}

static bool isImportFileSuffix(const QString &path)
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

static QString locateOpenMWBinary(const QString &profileHint)
{
    // 1. Explicit per-profile path takes precedence.
    if (!profileHint.isEmpty() && QFileInfo::exists(profileHint))
        return profileHint;

    // 2. Probe PATH.
    const QStringList pathEnv = qEnvironmentVariable("PATH").split(
#ifdef Q_OS_WIN
        ';'
#else
        ':'
#endif
    );
    const QStringList binNames = {
#ifdef Q_OS_WIN
        "openmw.exe", "openmw-launcher.exe"
#else
        "openmw", "openmw-launcher"
#endif
    };
    for (const QString &dir : pathEnv)
        for (const QString &bn : binNames) {
            QString candidate = QDir(dir).filePath(bn);
            if (QFileInfo::exists(candidate)) return candidate;
        }

    // 3. Platform-specific conventional locations.
#ifdef Q_OS_WIN
    const QStringList win = {
        "C:/Program Files/OpenMW/openmw.exe",
        "C:/Program Files (x86)/OpenMW/openmw.exe",
        QDir::homePath() + "/AppData/Local/OpenMW/openmw.exe",
    };
    for (const QString &p : win) if (QFileInfo::exists(p)) return p;
#else
    const QStringList nix = {
        "/usr/bin/openmw",
        "/usr/local/bin/openmw",
        "/var/lib/flatpak/exports/bin/org.openmw.OpenMW",
        QDir::homePath() + "/.local/share/flatpak/exports/bin/org.openmw.OpenMW",
        "/snap/bin/openmw",
        // Lutris ships its own OpenMW runner; if the user installed it via the
        // Lutris runner manager the binary lives under runners/openmw/<ver>/.
        QDir::homePath() + "/.local/share/lutris/runners/openmw/openmw",
        QDir::homePath() + "/.var/app/net.lutris.Lutris/data/lutris/runners/openmw/openmw",
    };
    for (const QString &p : nix) if (QFileInfo::exists(p)) return p;

    // Lutris runner-manager installs versioned subdirs (e.g. openmw/0.49.0/openmw).
    for (const QString &runnersRoot : {
            QDir::homePath() + "/.local/share/lutris/runners/openmw",
            QDir::homePath() + "/.var/app/net.lutris.Lutris/data/lutris/runners/openmw"}) {
        QDir d(runnersRoot);
        if (!d.exists()) continue;
        const QStringList versions = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
        for (const QString &v : versions) {
            const QString cand = runnersRoot + "/" + v + "/openmw";
            if (QFileInfo::exists(cand)) return cand;
        }
    }
    // Last resort: any Lutris yml with runner: openmw points at an install with a usable binary.
    {
        QString lut = GameProfileRegistry::findLutrisGameExe("openmw");
        if (!lut.isEmpty() && QFileInfo::exists(lut)) return lut;
    }
#endif

    return QString();
}

void MainWindow::onModlistSummary()
{
    // Aggregate sizes (refresh first so newly-installed mods get measured).
    m_scans->scheduleSizeScan();

    const QList<ModEntry> entries = snapshotEntriesForPersist();

    // The counting/formatting lives in modlist_summary (Qt Core only, unit
    // tested) and the rendering in modlist_summary_dialog; this slot only
    // gathers.  ScanCoordinator::sizeOf checks its cache before falling back to
    // a synchronous walk, so it's the right resolver for rows the async size
    // scan hasn't landed on yet.
    modlist_summary::View view;
    view.stats = modlist_summary::computeStats(
        entries, [this](const QString &modPath) { return m_scans->sizeOf(modPath); });
    view.outsideCount = modlist_summary::countOutsideModsDir(entries, m_modsDir);

    view.profileName = currentProfile().displayName;
    view.modsDir     = m_modsDir;
#ifdef Q_OS_WIN
    view.platform  = QStringLiteral("Windows");
    view.openmwCfg = QDir::homePath() + "/Documents/My Games/OpenMW/openmw.cfg";
#else
    view.platform  = QStringLiteral("Linux");
    view.openmwCfg = QDir::homePath() + "/.config/openmw/openmw.cfg";
#endif

    const QString openmwPath = locateOpenMWBinary(m_openmwPath);
    view.openmwBinary = openmwPath.isEmpty() ? T("summary_openmw_not_found")
                                             : openmwPath;

    modlist_summary::showDialog(this, view,
        [this] { onMoveModsDir(); },
        [this] { onConsolidateModsIntoActiveProfile(); });
}

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

// Diagnostic bundle. Zips the modlist + load-order files, openmw.cfg, the tail
// of OpenMW.log, and a system summary into one nerev_diagnostics_<timestamp>.zip
// to attach to a bug report. Saves the manual log-paste/cfg-share dance. Local
// only - no network, no upload.
void MainWindow::onCreateDiagnosticBundle()
{
    // Pick the output zip path.  Default to ~/Desktop/<file> when the
    // user has a Desktop dir, otherwise their home; QFileDialog falls
    // back to the OS default if neither exists.
    const QString stamp = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyyMMdd_HHmmss"));
    const QString defaultName = QStringLiteral("nerev_diagnostics_") + stamp +
                                QStringLiteral(".zip");
    QString defaultDir = QStandardPaths::writableLocation(
                             QStandardPaths::DesktopLocation);
    if (defaultDir.isEmpty() || !QDir(defaultDir).exists())
        defaultDir = QDir::homePath();

    const QString outZip = QFileDialog::getSaveFileName(
        this, T(Tk::diag_bundle_save_title),
        defaultDir + QLatin1Char('/') + defaultName,
        T(Tk::diag_bundle_filter));
    if (outZip.isEmpty()) return;

    // Stage everything in a temp dir so the final 7z run takes a single
    // tree.  QTemporaryDir auto-cleans on scope exit, even on early return.
    QTemporaryDir staging;
    staging.setAutoRemove(true);
    if (!staging.isValid()) {
        ui::warn(this, T(Tk::diag_bundle_title), T(Tk::diag_bundle_err_tmp).arg(staging.errorString()));
        return;
    }
    const QDir stageDir(staging.path());
    const QString reportRoot = stageDir.filePath(QStringLiteral("nerev_diagnostics"));
    QDir().mkpath(reportRoot);

    // -- Copy modlist + load-order files ---
    auto copyIfExists = [&](const QString &src, const QString &destName) {
        if (src.isEmpty() || !QFileInfo::exists(src)) return false;
        return QFile::copy(src, QDir(reportRoot).filePath(destName));
    };
    const bool gotModlist  = copyIfExists(modlistPath(),
                                          QFileInfo(modlistPath()).fileName());
    const bool gotLoadOrd  = copyIfExists(loadOrderPath(),
                                          QFileInfo(loadOrderPath()).fileName());

    // -- Copy openmw.cfg ---
#ifdef Q_OS_WIN
    const QString openmwCfg = QDir::homePath() +
        QStringLiteral("/Documents/My Games/OpenMW/openmw.cfg");
#else
    const QString openmwCfg = QDir::homePath() +
        QStringLiteral("/.config/openmw/openmw.cfg");
#endif
    const bool gotCfg = copyIfExists(openmwCfg, QStringLiteral("openmw.cfg"));

    // -- Tail OpenMW.log so the bundle stays manageable on multi-MB logs ---
#ifdef Q_OS_WIN
    const QString openmwLog = QDir::homePath() +
        QStringLiteral("/Documents/My Games/OpenMW/openmw.log");
#else
    const QString openmwLog = QDir::homePath() +
        QStringLiteral("/.config/openmw/openmw.log");
#endif
    bool gotLog = false;
    if (QFileInfo::exists(openmwLog)) {
        QFile in(openmwLog);
        if (in.open(QIODevice::ReadOnly)) {
            constexpr qint64 kTailBytes = 512 * 1024;  // last 512 KB
            const qint64 sz = in.size();
            const qint64 from = sz > kTailBytes ? sz - kTailBytes : 0;
            in.seek(from);
            const QByteArray tail = in.readAll();
            QFile out(QDir(reportRoot).filePath(QStringLiteral("openmw.log.tail")));
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                if (from > 0) {
                    out.write("[truncated to last 512 KB of " +
                              QByteArray::number(sz) + " bytes]\n");
                }
                out.write(tail);
                gotLog = true;
            }
        }
    }

    // -- System summary ---
    QString sysOut;
    QTextStream s(&sysOut);
    s << "Nerevarine Organizer - Diagnostic Bundle\n";
    s << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n\n";
    s << "App version       : " << QCoreApplication::applicationVersion() << "\n";
    s << "Qt build          : " << QT_VERSION_STR << " (runtime " << qVersion() << ")\n";
    s << "Compiler ABI      : " << QSysInfo::buildAbi() << "\n";
    s << "CPU arch          : " << QSysInfo::currentCpuArchitecture() << "\n";
    s << "Kernel            : " << QSysInfo::kernelType() << " "
                                << QSysInfo::kernelVersion() << "\n";
    s << "Product           : " << QSysInfo::prettyProductName() << "\n";
    s << "Product type      : " << QSysInfo::productType()
                                << " " << QSysInfo::productVersion() << "\n";
    s << "Hostname          : " << QSysInfo::machineHostName() << "\n";
#ifdef Q_OS_LINUX
    // Distro: read /etc/os-release verbatim - small, always present on
    // systemd distros, and includes PRETTY_NAME / VERSION_ID / ID etc.
    {
        QFile osrel(QStringLiteral("/etc/os-release"));
        if (osrel.open(QIODevice::ReadOnly | QIODevice::Text)) {
            s << "\n[/etc/os-release]\n"
              << QString::fromUtf8(osrel.readAll()).trimmed() << "\n";
        }
        // Live driver hints - lspci is usually present and surfaces both
        // the GPU vendor and the kernel module name (nvidia / nouveau /
        // amdgpu / i915).  Suppress stderr so a missing tool doesn't
        // contaminate the bundle.
        QProcess p;
        subprocess::applyEnv(p);
        p.start(QStringLiteral("lspci"), {QStringLiteral("-nnk")});
        if (p.waitForFinished(2000)) {
            const QString txt = QString::fromUtf8(p.readAllStandardOutput());
            QStringList vga;
            for (const QString &line : txt.split('\n')) {
                if (line.contains(QStringLiteral("VGA"), Qt::CaseInsensitive) ||
                    line.contains(QStringLiteral("3D"), Qt::CaseInsensitive)  ||
                    line.contains(QStringLiteral("Display"), Qt::CaseInsensitive))
                    vga << line;
            }
            if (!vga.isEmpty()) {
                s << "\n[GPU (lspci)]\n" << vga.join('\n') << "\n";
            }
        }
        // glxinfo if installed - "OpenGL renderer" / "OpenGL version" is
        // exactly what most OpenMW troubleshooting needs.
        QProcess g;
        subprocess::applyEnv(g);
        g.start(QStringLiteral("glxinfo"), {QStringLiteral("-B")});
        if (g.waitForFinished(2500)) {
            const QString txt = QString::fromUtf8(g.readAllStandardOutput());
            QStringList rel;
            for (const QString &line : txt.split('\n')) {
                if (line.startsWith(QStringLiteral("OpenGL"))     ||
                    line.startsWith(QStringLiteral("direct render"))||
                    line.startsWith(QStringLiteral("Vendor"))     ||
                    line.startsWith(QStringLiteral("Device")))
                    rel << line.trimmed();
            }
            if (!rel.isEmpty()) {
                s << "\n[glxinfo -B]\n" << rel.join('\n') << "\n";
            }
        }
        QFile nv(QStringLiteral("/sys/module/nvidia/version"));
        if (nv.exists() && nv.open(QIODevice::ReadOnly | QIODevice::Text)) {
            s << "\n[NVIDIA driver]\n"
              << QString::fromUtf8(nv.readAll()).trimmed() << "\n";
        }
    }
#endif
    // -- App state summary --
    s << "\n[Active profile]\n";
    if (m_profiles && !m_profiles->isEmpty()) {
        const GameProfile &gp = m_profiles->current();
        s << "Game id           : " << gp.id << "\n";
        s << "Display name      : " << gp.displayName << "\n";
        s << "Modlist profile   : " << gp.activeModlist().name << "\n";
        s << "Mods dir          : " << m_modsDir << "\n";
        s << "OpenMW path       : " << m_openmwPath << "\n";
        s << "Launcher path     : " << m_openmwLauncherPath << "\n";
    } else {
        s << "(no profile loaded)\n";
    }
    s << "\n[Modlist counters]\n";
    int modCount = 0, sepCount = 0, enabled = 0, installing = 0;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() == ItemType::Separator) {
            ++sepCount;
            continue;
        }
        ++modCount;
        if (it->checkState() == Qt::Checked) ++enabled;
        if (it->data(ModRole::InstallStatus).toInt() == 2) ++installing;
    }
    s << "Mods              : " << modCount << "\n";
    s << "Enabled           : " << enabled << "\n";
    s << "Separators        : " << sepCount << "\n";
    s << "In-flight installs: " << installing << "\n";
    s << "Load-order length : " << m_loadOrder.size() << "\n";

    s << "\n[Files included]\n";
    s << "modlist           : " << (gotModlist ? "yes" : "no") << "\n";
    s << "load order        : " << (gotLoadOrd ? "yes" : "no") << "\n";
    s << "openmw.cfg        : " << (gotCfg ? "yes" : "no") << "\n";
    s << "openmw.log (tail) : " << (gotLog ? "yes" : "no") << "\n";

    {
        QFile sf(QDir(reportRoot).filePath(QStringLiteral("system_summary.txt")));
        if (sf.open(QIODevice::WriteOnly | QIODevice::Truncate))
            sf.write(sysOut.toUtf8());
    }

    // -- Bundle into a zip via 7z (already a runtime dep for extraction) --
    // Remove an existing target so 7z doesn't try to update-merge into it.
    if (QFileInfo::exists(outZip)) QFile::remove(outZip);
    QProcess zip;
    subprocess::applyEnv(zip);
    zip.setWorkingDirectory(stageDir.absolutePath());
    zip.start(QStringLiteral("7z"),
              {QStringLiteral("a"), QStringLiteral("-tzip"),
               outZip, QStringLiteral("nerev_diagnostics")});
    if (!zip.waitForStarted(3000)) {
        ui::warn(this, T(Tk::diag_bundle_title), T(Tk::diag_bundle_err_no_7z));
        return;
    }
    if (!zip.waitForFinished(60000) || zip.exitCode() != 0) {
        const QString stderr_ = QString::fromUtf8(zip.readAllStandardError());
        ui::warn(this, T(Tk::diag_bundle_title), T(Tk::diag_bundle_err_7z_failed).arg(zip.exitCode()).arg(stderr_));
        return;
    }

    statusBar()->showMessage(
        T(Tk::diag_bundle_status_done).arg(QFileInfo(outZip).fileName()), 6000);

    // Reveal in the file manager so the user can attach it without
    // leaving the app. openUrl on a directory pops the file manager;
    // openUrl on the file would open it in an archive viewer instead.
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(outZip).absolutePath()));
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

void MainWindow::onInspectOpenMWSetup()
{
    // Force a fresh rewrite of openmw.cfg before we report what we wrote, so
    // the user sees the actual current state rather than a stale one.
    syncGameConfig();

    if (m_profiles->isEmpty() || currentProfile().id != "morrowind") {
        ui::info(this, T("openmw_inspect_title"), T("openmw_inspect_not_morrowind"));
        return;
    }

    static const QStringList contentExts{".esp", ".esm", ".omwaddon", ".omwscripts"};

    QString report;
    int totalMods    = 0;
    int enabledMods  = 0;
    int dataFolders  = 0;
    int contentFiles = 0;
    QStringList mastersSeen;     // for quick grep in the dialog
    QStringList emptyInstalls;   // mods marked installed but with no plugins
    // Collisions are only meaningful between mods that actually get their
    // data= emitted - i.e. enabled+installed - because a disabled mod's
    // plugins never reach openmw.cfg.  Build the collision input in the
    // same walk so we don't double-scan the modlist.
    QList<openmw::CollisionInput> collisionInput;
    QList<openmw::AssetCaseInput> assetCaseInput;
    // modlist-sync-guard: every installed mod's modPath is a candidate
    // for a portability check against m_modsDir, not just enabled ones -
    // a disabled mod with a machine-specific path will still break when
    // the user re-enables it on another host.
    QList<openmw::SyncGuardInput> syncGuardInput;

    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 1) continue;
        ++totalMods;
        const bool enabled = item->checkState() == Qt::Checked;
        if (enabled) ++enabledMods;

        QString name = item->data(ModRole::CustomName).toString();
        if (name.isEmpty()) name = item->text();
        QString modPath = item->data(ModRole::ModPath).toString();

        // Record every installed mod (enabled or not) for the sync-guard -
        // a disabled row with a broken path still matters next time it's
        // toggled on.
        syncGuardInput << openmw::SyncGuardInput{name, modPath};

        const auto folders = collectDataFolders(modPath, contentExts);
        QString marker = enabled ? "[✓] " : "[ ] ";
        report += marker + name + "\n";
        report += "    path: " + modPath + "\n";

        if (folders.isEmpty()) {
            report += "    ⚠ NO .esp/.esm/.omwaddon found inside this folder.\n"
                      "       → The install may be incomplete. Re-install via FOMOD,\n"
                      "         or verify the folder has actual plugin files.\n";
            emptyInstalls << name;
        } else {
            for (const auto &p : folders) {
                ++dataFolders;
                report += "    data= " + p.first + "\n";
                for (const QString &f : p.second) {
                    ++contentFiles;
                    report += "        - " + f + "\n";
                    if (f.endsWith(".esm", Qt::CaseInsensitive))
                        mastersSeen << f;
                }
            }
            if (enabled) {
                openmw::CollisionInput ci;
                ci.modLabel   = name;
                ci.pluginDirs = folders;
                collisionInput << ci;

                // Asset case-collision: walk all files in each data root and
                // collect relative paths so the pure detector can find
                // case-variant duplicates (e.g. Player.lua + player.lua).
                for (const auto &p : folders) {
                    const QString &root = p.first;
                    const int rootLen = root.length() + 1;
                    QStringList relPaths;
                    QDirIterator dit(root, QDir::Files | QDir::NoDotAndDotDot,
                                     QDirIterator::Subdirectories);
                    while (dit.hasNext()) {
                        dit.next();
                        const QString rel = dit.filePath().mid(rootLen);
                        if (!rel.isEmpty()) relPaths << rel;
                    }
                    assetCaseInput << openmw::AssetCaseInput{name, root, relPaths};
                }
            }
        }
        report += "\n";
    }

    // Duplicate-plugin detection: VFS last-wins means if two mods both ship
    // Rocky_WG_Base_1.1.esp, only one loads and the user has no way to see
    // that the other two are shadowed.  Pinned by test_plugin_collisions.
    const openmw::PluginCollisionReport collisions =
        openmw::findPluginBasenameCollisions(collisionInput);
    if (!collisions.collisions.isEmpty()) {
        report += QString("\n%1\n").arg(T("openmw_inspect_collisions_header"));
        report += T("openmw_inspect_collisions_total")
                      .arg(collisions.collisions.size())
                      .arg(collisions.totalPluginsChecked) + "\n\n";
        for (const openmw::PluginCollision &c : collisions.collisions) {
            report += "  " + c.basename + "\n";
            for (const openmw::PluginCollisionProvider &p : c.providers) {
                report += QString("      ← %1  [%2]\n")
                              .arg(p.modLabel, p.dataRoot);
            }
        }
        report += "\n";
    }

    // Asset case-collision: on Linux, Player.lua and player.lua are two
    // distinct files; OpenMW loads both and one silently shadows the other.
    const openmw::AssetCollisionReport assetCaseReport =
        openmw::findAssetCaseCollisions(assetCaseInput);
    if (!assetCaseReport.mods.isEmpty()) {
        report += QString("\n%1\n").arg(T("openmw_inspect_asset_case_header"));
        report += T("openmw_inspect_asset_case_total")
                      .arg(assetCaseReport.mods.size())
                      .arg(assetCaseReport.totalFilesChecked) + "\n\n";
        for (const openmw::AssetCaseModReport &m : assetCaseReport.mods) {
            report += "  " + m.modLabel + "  [" + m.dataRoot + "]\n";
            for (const openmw::AssetCaseHit &h : m.hits) {
                for (const QString &s : h.spellings)
                    report += "      " + s + "\n";
                report += "      ^ same file, different casing\n";
            }
        }
        report += "\n";
    }

    // modlist-sync-guard: modlist_morrowind.txt is often committed to git and
    // synced between machines, so flag every modPath not under the mods dir.
    // Those resolve to nothing on the peer host and drop out of openmw.cfg.
    // m_modsDir is the sole root for now; could take a user list later.
    const openmw::ModlistSyncReport syncReport =
        openmw::findModlistPathDrift(syncGuardInput, { m_modsDir });
    if (!syncReport.driftEntries.isEmpty()) {
        report += QString("\n%1\n").arg(T("openmw_inspect_sync_guard_header"));
        report += T("openmw_inspect_sync_guard_canonical")
                      .arg(syncReport.canonicalRoots.join(", ")) + "\n";
        report += T("openmw_inspect_sync_guard_total")
                      .arg(syncReport.driftEntries.size())
                      .arg(syncReport.totalModsChecked) + "\n\n";
        for (const openmw::ModPathDrift &d : syncReport.driftEntries) {
            report += "  " + d.modLabel + "\n";
            report += QString("      path:   %1\n").arg(
                d.modPath.isEmpty() ? QStringLiteral("(empty)") : d.modPath);
            report += QString("      reason: %1\n").arg(d.reason);
        }
        report += "\n";
    }

    mastersSeen.removeDuplicates();
    mastersSeen.sort(Qt::CaseInsensitive);

    QString summary;
    summary += QString("Profile: %1\n").arg(currentProfile().displayName);
    summary += QString("openmw.cfg: %1/.config/openmw/openmw.cfg\n")
                   .arg(QDir::homePath());
    summary += QString("Installed mods: %1 (%2 enabled)\n")
                   .arg(totalMods).arg(enabledMods);
    summary += QString("Data folders emitted: %1\n").arg(dataFolders);
    summary += QString("Content files total: %1\n").arg(contentFiles);
    summary += QString("Masters (.esm) found: %1\n")
                   .arg(mastersSeen.isEmpty() ? QStringLiteral("(none)")
                                               : mastersSeen.join(", "));
    if (!emptyInstalls.isEmpty()) {
        summary += "\n⚠ Mods marked INSTALLED but with NO plugins on disk:\n  "
                 + emptyInstalls.join("\n  ") + "\n";
    }

    ui::showMonospaceReport(this, T("openmw_inspect_title"), report, 720, 520, summary);
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

void MainWindow::onEditLoadOrder()
{
    reconcileLoadOrder(); // make sure the list reflects what's actually installed

    if (m_loadOrder.isEmpty()) {
        ui::info(this, T("loadorder_title"), T("loadorder_empty"));
        return;
    }

    // Build a set of plugins that belong to currently-ENABLED mods, so we can
    // visually distinguish them from ones whose mod is disabled (still in the
    // load order but not emitted to content=).
    static const QStringList contentExts{".esp", ".esm", ".omwaddon", ".omwscripts"};
    QSet<QString> enabledSet;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 1) continue;
        if (item->checkState() != Qt::Checked) continue;
        for (const auto &p : collectDataFolders(
                 item->data(ModRole::ModPath).toString(), contentExts))
            for (const QString &cf : p.second) enabledSet.insert(cf);
    }

    QDialog dlg(this);
    dlg.setWindowTitle(T("loadorder_title"));
    dlg.setMinimumSize(560, 520);

    auto *vlay   = new QVBoxLayout(&dlg);
    auto *lblTop = new QLabel(T("loadorder_explain"), &dlg);
    lblTop->setWordWrap(true);
    lblTop->setStyleSheet(
        "color: #444; padding: 6px; "
        "background: #f4f1ee; border-radius: 4px;");
    vlay->addWidget(lblTop);

    auto *hbox = new QHBoxLayout;
    auto *list = new QListWidget(&dlg);
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setAlternatingRowColors(true);

    QFont mono("monospace");
    for (const QString &cf : m_loadOrder) {
        auto *row = new QListWidgetItem(cf, list);
        row->setFont(mono);
        if (!enabledSet.contains(cf)) {
            QFont f = row->font(); f.setItalic(true); row->setFont(f);
            row->setForeground(QColor(150, 150, 150));
            row->setToolTip(T("loadorder_disabled_tooltip"));
        }
        // Masters get a subtle accent so dependency fixes are visible at a glance.
        if (cf.endsWith(".esm", Qt::CaseInsensitive))
            row->setBackground(QColor(230, 240, 250));
    }
    hbox->addWidget(list, 1);

    auto *btnCol = new QVBoxLayout;
    auto *upBtn    = new QPushButton(QStringLiteral("▲"), &dlg);
    auto *dnBtn    = new QPushButton(QStringLiteral("▼"), &dlg);
    auto *autoBtn  = new QPushButton(T("loadorder_btn_autosort"), &dlg);
    autoBtn->setToolTip(T("loadorder_btn_autosort_tooltip"));
    for (auto *b : {upBtn, dnBtn}) b->setFixedWidth(44);
    btnCol->addWidget(upBtn);
    btnCol->addWidget(dnBtn);
    btnCol->addSpacing(10);
    btnCol->addWidget(autoBtn);
    btnCol->addStretch();
    hbox->addLayout(btnCol);
    vlay->addLayout(hbox, 1);

    auto moveRow = [list](int delta) {
        int r = list->currentRow();
        int t = r + delta;
        if (r < 0 || t < 0 || t >= list->count()) return;
        auto *it = list->takeItem(r);
        list->insertItem(t, it);
        list->setCurrentRow(t);
    };
    connect(upBtn, &QPushButton::clicked, &dlg, [moveRow]{ moveRow(-1); });
    connect(dnBtn, &QPushButton::clicked, &dlg, [moveRow]{ moveRow(+1); });

    // Auto-sort button: run the topo-sort against the dialog's current order,
    // then rebuild the list UI. Cancel/Close still discards if the user doesn't
    // commit with OK.
    connect(autoBtn, &QPushButton::clicked, &dlg, [this, list]{
        m_loadOrder.clear();
        for (int i = 0; i < list->count(); ++i)
            m_loadOrder.append(list->item(i)->text());
        autoSortLoadOrder();
        list->clear();
        QFont mono("monospace");
        for (const QString &cf : m_loadOrder) {
            auto *row = new QListWidgetItem(cf, list);
            row->setFont(mono);
            if (cf.endsWith(".esm", Qt::CaseInsensitive))
                row->setBackground(QColor(230, 240, 250));
        }
    });

    auto *btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    vlay->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    // Apply edits.
    m_loadOrder.clear();
    for (int i = 0; i < list->count(); ++i)
        m_loadOrder.append(list->item(i)->text());
    saveLoadOrder();
    syncGameConfig();
    statusBar()->showMessage(T("loadorder_applied"), 3000);
}

void MainWindow::onSortSeparators()
{
    auto newOrder = modlist_sort::showReorderSeparatorsDialog(this, m_modList);
    if (newOrder.isEmpty()) return;

    checkpointBeforeSort();   // recoverable pre-sort checkpoint

    // A deliberate structural reorder commits the current display as the new
    // saved order (clears any temporary view sort without restoring it).
    dropViewSortKeepingOrder();
    m_undoStack->pushUndo();

    // Detach all items, then re-add in the new order. Pointers stay valid.
    while (m_modList->count() > 0) m_modList->takeItem(0);
    for (auto *it : newOrder) m_modList->addItem(it);

    saveModList();
    updateSectionCounts();
    scheduleConflictScan();
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

void MainWindow::clearViewSortState()
{
    // Cosmetic reset for list rebuilds (load / profile switch / new): drop the
    // banner + flag. Correctness doesn't depend on this - the rebuilt rows carry
    // no SortAnchor stamps, so rowOrderForPersist already returns identity.
    m_viewSortActive = false;
    if (m_notify) m_notify->hideSticky();
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

void MainWindow::enterViewSort()
{
    // Re-stamp only if the rows aren't already stamped from an earlier sort this
    // session; otherwise the current (already-sorted) order would be recorded as
    // the saved baseline. Keying off the rows (not just m_viewSortActive) also
    // means a stale flag after a list rebuild can't corrupt the saved order.
    bool anyStamped = false;
    for (int i = 0; i < m_modList->count(); ++i)
        if (m_modList->item(i)->data(ModRole::SortAnchor).isValid()) { anyStamped = true; break; }

    if (!anyStamped) {
        // Stamp each row's current index as its saved position. Block signals so
        // the bulk setData doesn't fire the itemChanged handler once per row.
        const QSignalBlocker blocker(m_modList);
        for (int i = 0; i < m_modList->count(); ++i)
            m_modList->item(i)->setData(ModRole::SortAnchor, static_cast<qint64>(i));
    }
    m_viewSortActive = true;
}

void MainWindow::dropViewSortKeepingOrder()
{
    if (!m_viewSortActive) return;
    {
        const QSignalBlocker blocker(m_modList);
        for (int i = 0; i < m_modList->count(); ++i)
            m_modList->item(i)->setData(ModRole::SortAnchor, QVariant());
    }
    m_viewSortActive = false;
    if (m_notify) m_notify->hideSticky();
}

void MainWindow::resetToSavedOrder()
{
    if (!m_viewSortActive) return;

    // Reorder the display back to the stamped saved order. Same item pointers,
    // so no data is lost; unstamped rows (added mid-sort) trail.
    const QList<int> order = rowOrderForPersist();
    QList<QListWidgetItem *> items;
    items.reserve(order.size());
    for (int idx : order) items.append(m_modList->item(idx));

    {
        const QSignalBlocker blocker(m_modList);
        while (m_modList->count() > 0) m_modList->takeItem(0);
        for (auto *it : items) {
            it->setData(ModRole::SortAnchor, QVariant());
            m_modList->addItem(it);   // re-added items default to visible
        }
    }
    m_viewSortActive = false;

    // Hidden state is lost across takeItem/addItem, so re-collapse the sections
    // that were collapsed (mirrors loadModList's post-build pass).
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *sep = m_modList->item(i);
        if (sep->data(ModRole::ItemType).toString() == ItemType::Separator
                && sep->data(ModRole::Collapsed).toBool())
            collapseSection(sep, true);
    }

    if (m_notify) m_notify->hideSticky();
    if (m_sizeSortBtn) m_sizeSortBtn->setText(m_sizeSortAsc ? T("col_size_asc") : T("col_size_desc"));
    if (m_dateSortBtn) m_dateSortBtn->setText(m_dateSortAsc ? T("col_date_added_asc") : T("col_date_added_desc"));
    updateModCount();
    updateSectionCounts();
    statusBar()->showMessage(T("status_view_sort_reset"), 3000);
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

void MainWindow::onSortBySize()
{
    checkpointBeforeSort();   // recoverable pre-sort checkpoint
    if (sender() == m_sizeSortBtn)
        m_sizeSortAsc = !m_sizeSortAsc;
    m_sizeSortBtn->setText(m_sizeSortAsc ? T("col_size_asc") : T("col_size_desc"));

    // View-only: stamp the saved order once, then reorder just the display. The
    // saved load order is emitted by rowOrderForPersist() on the next save, so
    // there is deliberately no saveModList() here.
    enterViewSort();
    modlist_sort::bySize(m_modList, m_sizeSortAsc);
    updateSectionCounts();

    if (m_notify) m_notify->showSticky(T("viewsort_banner"), QStringLiteral("#7a5c17"));
    statusBar()->showMessage(
        m_sizeSortAsc ? T("status_sorted_size_asc") : T("status_sorted_size_desc"),
        3000);
}

void MainWindow::updateSectionCounts()
{
    // Pass 1: does ANY mod anywhere in the list have a pending Nexus
    // update?  When yes, every separator gets the "needs attention" grey
    // wash - originally only the offending section was tinted, but that
    // made pending updates in a different (or collapsed) section too easy
    // to miss, so now a single update greys the whole scaffolding until
    // the user either installs the update or removes the offending mod.
    bool anyUpdate = false;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod)
            continue;
        if (it->data(ModRole::UpdateAvailable).toBool()) {
            anyUpdate = true;
            break;
        }
    }

    QListWidgetItem *currentSep = nullptr;
    int active = 0, total = 0;
    auto flush = [&]() {
        if (!currentSep) return;
        currentSep->setData(ModRole::ActiveCount,  active);
        currentSep->setData(ModRole::TotalCount,   total);
        currentSep->setData(ModRole::SepHasUpdate, anyUpdate);
    };
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() == ItemType::Separator) {
            flush();
            currentSep = it;
            active = total = 0;
        } else {
            ++total;
            if (it->checkState() == Qt::Checked) ++active;
        }
    }
    flush();
    m_modList->viewport()->update();
}

// readTes3Masters + collectDataFolders - moved to include/pluginparser.h
// (plugins::readTes3Masters, plugins::collectDataFolders). See the `using`
// declarations at the top of this file.

void MainWindow::scanMissingMasters()
{
    // Debounced trigger (see runMissingMastersScan for the real work).
    if (m_mastersScanTimer) m_mastersScanTimer->start();
}

// Sweep ENABLED mods whose DependsOn list points at other mods that are
// either not in the modlist at all, not installed, or currently disabled.
// Purely in-memory (URL → item lookup), so it runs synchronously on every
// saveModList and costs nothing.  Surfaces as the yellow warning circle
// in the delegate's icon strip - the #1 crash-on-launch class for mods
// that quietly require a prerequisite you haven't turned on.
void MainWindow::scanMissingDependencies()
{
    // Snapshot modlist → POD vector, then hand off to the pure resolver
    // in deps_resolver.  Everything dep-related (self-skip, multi-sibling
    // semantics, enabled/installed label selection) lives in that module
    // and is unit-tested in tests/test_deps_resolver.cpp.
    QList<deps::ModEntry> snap;
    snap.reserve(m_modList->count());
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;

        deps::ModEntry e;
        e.idx         = i;
        e.nexusUrl    = it->data(ModRole::NexusUrl).toString();
        e.enabled     = (it->checkState() == Qt::Checked);
        e.installed   = (it->data(ModRole::InstallStatus).toInt() == 1);
        e.dependsOn   = it->data(ModRole::DependsOn).toStringList();
        e.displayName = it->data(ModRole::CustomName).toString();
        if (e.displayName.isEmpty()) e.displayName = it->text();
        snap.append(e);
    }

    // Prune stale DependsOn entries: if a dep URL doesn't match any mod
    // in the list, the referenced mod was removed (or was never installed
    // and was only parsed from a Nexus description).  Keeping the stale
    // URL would trigger a perpetual "not in modlist" launch warning.
    QSet<QString> knownUrls;
    for (const auto &e : snap)
        if (!e.nexusUrl.isEmpty()) knownUrls.insert(e.nexusUrl);

    bool pruned = false;
    for (auto &e : snap) {
        int before = e.dependsOn.size();
        e.dependsOn.erase(
            std::remove_if(e.dependsOn.begin(), e.dependsOn.end(),
                           [&](const QString &u) { return !knownUrls.contains(u); }),
            e.dependsOn.end());
        if (e.dependsOn.size() != before) {
            pruned = true;
        }
    }

    // idx → QListWidgetItem for writing the result roles back.  `snap` is
    // sparse on purpose (separators are skipped), so a flat QHash is fine.
    QHash<int, QListWidgetItem*> byIdx;
    for (const auto &e : snap) byIdx.insert(e.idx, m_modList->item(e.idx));

    // Write pruned DependsOn lists back to the items.
    if (pruned) {
        for (const auto &e : snap) {
            auto *item = byIdx.value(e.idx);
            if (item) item->setData(ModRole::DependsOn, e.dependsOn);
        }
    }

    // Keywords that identify MWSE / MGE XE engine tools.  Nerevarine is
    // OpenMW-only so these deps are never satisfiable and should not
    // generate a launch warning or a red dependency icon.
    static const QStringList kEngineToolTokens = {
        "mwse", "mge xe", "mge-xe", "mgexe", "mge",
        "modorganizer-to-openmw", "modorganizer to openmw"
    };
    auto isEngineTool = [&](const QString &label) {
        const QString l = label.toLower();
        for (const QString &t : kEngineToolTokens)
            if (l.contains(t)) return true;
        return false;
    };

    for (const deps::ModEntry &target : snap) {
        auto r = deps::resolveDependencies(target, snap);
        auto *item = byIdx.value(target.idx);
        if (!item) continue;

        // Drop MWSE/MGE XE entries - irrelevant for an OpenMW-only manager.
        r.missingLabels.erase(
            std::remove_if(r.missingLabels.begin(), r.missingLabels.end(),
                           isEngineTool),
            r.missingLabels.end());
        r.hasMissing = !r.missingLabels.isEmpty();

        item->setData(ModRole::HasInListDependency,  r.hasInListDep);
        item->setData(ModRole::HasMissingDependency, r.hasMissing);
        item->setData(ModRole::MissingDependencies,  r.missingLabels);
    }
    if (m_modList->viewport()) m_modList->viewport()->update();
}

void MainWindow::runMissingMastersScan()
{
    // Plugin master checks are Morrowind-specific (TES3 format).  For other
    // profiles just clear any previously-flagged icons synchronously and bail.
    if (m_profiles->isEmpty() || currentProfile().id != "morrowind") {
        for (int i = 0; i < m_modList->count(); ++i) {
            auto *item = m_modList->item(i);
            item->setData(ModRole::HasMissingMaster, false);
            item->setData(ModRole::MissingMasters, QStringList());
        }
        return;
    }

    // Snapshot enabled + installed mods into the POD form the controller
    // expects.  No QListWidgetItem pointers cross the thread boundary --
    // the controller keys its results by ModPath.
    QList<LoadOrderController::MastersInput> enabledMods;
    QSet<QString> availableLower;

    static const QStringList contentExts{".esp", ".esm"};
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 1) continue;
        if (item->checkState() != Qt::Checked) continue;

        LoadOrderController::MastersInput e;
        e.modPath = item->data(ModRole::ModPath).toString();
        // Skip groundcover-approved mods - OpenMW gracefully handles missing
        // masters for groundcover= plugins, so flagging them just produces
        // noise (e.g. TOTSP plugins the user doesn't need).
        if (m_groundcoverApproved.contains(e.modPath)) continue;
        for (const auto &p : m_scans->cachedDataFolders(e.modPath, contentExts)) {
            for (const QString &file : p.second) {
                availableLower.insert(file.toLower());
                e.plugins.append({QDir(p.first).filePath(file), file});
            }
        }
        enabledMods.append(e);
    }

    m_loadCtl->scanMissingMasters(enabledMods, availableLower);
}

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

void MainWindow::syncOpenMWConfig()
{
    // OpenMW config sync is only applicable to the Morrowind game profile
    if (m_profiles->isEmpty() || currentProfile().id != "morrowind")
        return;

    QElapsedTimer syncTotal;
    syncTotal.start();
    QElapsedTimer syncPhase;
    syncPhase.start();

    const QString cfgPath = QDir::homePath() + "/.config/openmw/openmw.cfg";
    const QStringList contentExts{".esp", ".esm", ".omwaddon", ".omwscripts"};

    // Build the pure ConfigMod list from modlist UI state + filesystem probes.
    // The writer itself has no Qt-widget / I/O dependencies, so this loop is
    // the only place that needs to talk to m_modList and the disk.
    QList<openmw::ConfigMod> mods;
    mods.reserve(m_modList->count());
    // Mod roots for every mod still in the list. Used below to spot orphan
    // data= lines - paths still under m_modsDir but no longer matching a managed
    // mod (mod removed, or launcher promoted a pre-upgrade data= into preamble).
    QSet<QString> managedModPaths;
    // Display names of all currently-installed mods.  Used by the patch-skip
    // filter a few lines below so that FOMOD-style "01 X for Y" subfolders
    // (e.g. Ashfront's "01 Grass for Remiros' Groundcover") get dropped when
    // their Y mod isn't in the list - OpenMW would otherwise warn / crash
    // loading plugins that reference an absent companion mod.
    const QStringList installedModNames =
        m_model ? m_model->installedModDisplayNames() : QStringList();
    auto normalizeModName = [](const QString &s) {
        QString n;
        n.reserve(s.size());
        for (const QChar &c : s)
            if (c.isLetterOrNumber()) n.append(c.toLower());
        return n;
    };
    QStringList installedNormalized;
    installedNormalized.reserve(installedModNames.size());
    for (const QString &n : installedModNames)
        installedNormalized << normalizeModName(n);
    // "01 Grass for Remiros' Groundcover" → "Remiros' Groundcover".  Empty if
    // the folder isn't a numbered FOMOD-style "N[letter] ... for Y" subfolder,
    // so plain data folders like "Data Files" or "textures" are never touched.
    auto detectPatchTarget = [](const QString &folderName) -> QString {
        static const QRegularExpression prefixed(
            QStringLiteral("^\\s*\\d+[a-zA-Z]?\\s+(.+)$"));
        const auto pm = prefixed.match(folderName);
        if (!pm.hasMatch()) return {};
        static const QRegularExpression forPat(
            QStringLiteral("\\bfor\\s+(.+?)\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
        const auto m = forPat.match(pm.captured(1));
        return m.hasMatch() ? m.captured(1).trimmed() : QString();
    };
    auto patchTargetInstalled = [&](const QString &target) {
        const QString nt = normalizeModName(target);
        if (nt.length() < 4) return false;
        for (const QString &n : installedNormalized)
            if (n.contains(nt)) return true;
        return false;
    };
    // Walk mods in saved order (identity unless a temporary Size/Date view sort
    // is active) so data= precedence in openmw.cfg follows the real load order,
    // not the sorted display.
    const QList<int> cfgOrder = rowOrderForPersist();
    for (int oi = 0; oi < cfgOrder.size(); ++oi) {
        auto *item = m_modList->item(cfgOrder[oi]);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;

        openmw::ConfigMod cm;
        cm.enabled   = (item->checkState() == Qt::Checked);
        cm.installed = (item->data(ModRole::InstallStatus).toInt() == 1);

        if (cm.installed) {
            const QString modPath = item->data(ModRole::ModPath).toString();
            if (!modPath.isEmpty())
                managedModPaths.insert(QDir::cleanPath(modPath));
            cm.pluginDirs = m_scans->cachedDataFolders(modPath, contentExts);

            // Register BSA archives this mod ships as fallback-archive= entries.
            // Without this, BSA-only mods (e.g. Authentic Signs IT) load their
            // .esp refs but render [None] textures - the archive is invisible to
            // OpenMW's resource resolver. Recursive: BSAs can sit in subfolders
            // beside their .esp (Tamriel Data layout). Dedup by basename
            // (OpenMW wants unique fallback-archive= names). Cached in
            // ScanCoordinator - this walk used to run synchronously per mod on
            // every saveModList.
            cm.bsaFiles = m_scans->cachedBsaFiles(modPath);
            // Drop patch subfolders whose target mod isn't in the list, plus
            // those the user declined at mod-add. If the target is later
            // installed, addModFromPath re-prompts; accepting clears the declined
            // flag and the next sync reinstates the data= line.
            for (int pi = cm.pluginDirs.size() - 1; pi >= 0; --pi) {
                const QString folder =
                    QFileInfo(cm.pluginDirs[pi].first).fileName();
                const QString declinedKey = modPath + '\t' + folder;
                if (m_declinedPatches.contains(declinedKey)) {
                    cm.pluginDirs.removeAt(pi);
                    continue;
                }
                const QString target = detectPatchTarget(folder);
                if (!target.isEmpty() && !patchTargetInstalled(target))
                    cm.pluginDirs.removeAt(pi);
            }
            if (cm.enabled && cm.pluginDirs.isEmpty()) {
                cm.resourceRoots = plugins::collectResourceFolders(modPath);
                if (cm.resourceRoots.isEmpty()) cm.resourceRoots << modPath;
            }
            // Groundcover plugins: emit as groundcover= instead of content=
            // so OpenMW renders them as instanced grass/flora.
            // Three detection paths:
            //   1. Whole mod approved by the user via the install-time prompt.
            //   2. Plugin directory path contains "groundcover" or "grass"
            //      (e.g. "04 Remiros' Groundcover Patch" inside Caldera Priory).
            //   3. Individual plugin filename matches a known groundcover
            //      pattern (e.g. Rem_*.esp bundled in OAAB Grazelands, or
            //      "Vurt's Groundcover" in the filename).
            static const QRegularExpression kGroundcoverPlugin(
                QStringLiteral(R"(^(Rem_|FGM_|Vurt'?s Groundcover).+\.esp$)"),
                QRegularExpression::CaseInsensitiveOption);
            const QString nexusUrl = item->data(ModRole::NexusUrl).toString();
            const bool modApproved =
                m_groundcoverApproved.contains(modPath)
             || (!nexusUrl.isEmpty() && m_groundcoverApproved.contains(nexusUrl));
            if (modApproved) {
                for (const auto &p : cm.pluginDirs)
                    for (const QString &cf : p.second)
                        cm.groundcoverFiles.insert(cf);
            } else {
                // Named grass mods whose folder doesn't literally say
                // "grass"/"groundcover" - same list as the addModFromPath
                // helper, so an unprompted sync still emits the right section.
                static const QStringList kGroundcoverNameHints = {
                    QStringLiteral("lush synthesis"),
                };
                auto hintMatches = [&](const QString &s) {
                    for (const QString &h : kGroundcoverNameHints)
                        if (s.contains(h, Qt::CaseInsensitive)) return true;
                    return false;
                };
                const bool modNameIsGC =
                    hintMatches(modPath)
                 || hintMatches(item->text())
                 || hintMatches(item->data(ModRole::CustomName).toString());
                for (const auto &p : cm.pluginDirs) {
                    const bool dirIsGC =
                        modNameIsGC
                     || p.first.contains("groundcover", Qt::CaseInsensitive)
                     || p.first.contains("grass", Qt::CaseInsensitive)
                     || hintMatches(p.first);
                    for (const QString &cf : p.second) {
                        if (dirIsGC
                            || cf.contains("groundcover", Qt::CaseInsensitive)
                            || kGroundcoverPlugin.match(cf).hasMatch())
                            cm.groundcoverFiles.insert(cf);
                    }
                }
            }
        }
        mods << cm;
    }
    const qint64 ms_scanMods = syncPhase.restart();

    // Read existing cfg + launcher.cfg (empty strings if absent).  The
    // pure orchestration helper below consumes both as inputs - keeps
    // it FS-readonly aside from QFileInfo probes, which makes it
    // unit-testable against QTemporaryDir fixtures.
    QString existing;
    {
        QFile f(cfgPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            existing = QString::fromUtf8(f.readAll());
    }
    QString launcherCfgText;
    {
        QFile lf(QDir::homePath() + "/.config/openmw/launcher.cfg");
        if (lf.open(QIODevice::ReadOnly | QIODevice::Text))
            launcherCfgText = QString::fromUtf8(lf.readAll());
    }
    const qint64 ms_readCfg = syncPhase.restart();

    // -- Pure orchestration -- (orphan rescue + launcher-only
    // augmentation + master satisfaction + scrub + effective load
    // order).  See openmw::prepareForSync in
    // src/openmwconfigwriter.cpp - the bug-report-history block
    // comments live there now, and tests/test_openmw_sync_prep.cpp
    // pins the behaviour against on-disk fixtures.
    openmw::SyncPrepareInputs prep;
    prep.existingCfg      = std::move(existing);
    prep.launcherCfgText  = std::move(launcherCfgText);
    prep.mods             = std::move(mods);
    prep.managedModPaths  = std::move(managedModPaths);
    prep.modsRoot         = m_modsDir.isEmpty() ? QString()
                                                : QDir::cleanPath(m_modsDir);
    prep.loadOrder        = m_loadOrder;

    openmw::SyncPrepareResult prepared = openmw::prepareForSync(prep);
    mods = std::move(prepared.mods);
    const qint64 ms_masters = syncPhase.restart();

    if (prepared.effectiveLoadOrder != m_loadOrder) {
        m_loadOrder = prepared.effectiveLoadOrder;
        saveLoadOrder();
    }

    if (prepared.droppedOrphans > 0) {
        statusBar()->showMessage(
            T("status_orphan_content_dropped").arg(prepared.droppedOrphans),
            6000);
    }

    const QString rendered =
        openmw::renderOpenMWConfig(mods, m_loadOrder, prepared.scrubbedExisting);

    // -- launcher.cfg inputs (UI-thread, cheap) ---
    //
    // The OpenMW Launcher keeps its own per-profile cache of data= and
    // content= lines in ~/.config/openmw/launcher.cfg, and its Data Files
    // tab renders from that cache, not from openmw.cfg.  So after an
    // uninstall/remove the mod vanishes from openmw.cfg but the launcher
    // still shows it, and the user can re-tick a plugin that has no
    // provider - instant "content file does not exist" crash.
    //
    // Pre-extract the data= paths and content= filenames the launcher
    // expects in load-priority / activation order, then hand them to the
    // worker below. The actual launcher.cfg read+render+write runs on
    // the worker so the user-facing UI doesn't see this file IO either.
    const QString launcherPath =
        QDir::homePath() + "/.config/openmw/launcher.cfg";
    QStringList lDataPaths;
    QStringList lContent;
    for (const QString &raw : rendered.split('\n')) {
        QString line = raw;
        if (line.endsWith('\r')) line.chop(1);
        if (line.startsWith(QStringLiteral("data="))) {
            QString p = line.mid(5);
            if (p.size() >= 2 && p.startsWith('"') && p.endsWith('"'))
                p = p.mid(1, p.size() - 2);
            lDataPaths << p;
        } else if (line.startsWith(QStringLiteral("content="))) {
            lContent << line.mid(8);
        }
    }
    const qint64 ms_render = syncPhase.restart();

    // openmw.cfg + launcher.cfg writes moved off the UI thread.  Chained
    // onto the same m_lastSaveFuture as the modlist file write so
    // closeEvent only has to wait on one future, and sequenced behind
    // any prior in-flight write so two saves can't race on the same
    // file.  The launcher mtime gate runs in the worker so it sees the
    // post-write mtime of openmw.cfg.
    if (m_lastSaveFuture.isRunning())
        m_lastSaveFuture.waitForFinished();
    const QByteArray cfgBytes = rendered.toUtf8();
    QPointer<MainWindow> safeSelf(this);
    m_lastSaveFuture = QtConcurrent::run(
        [safeSelf, cfgPath, cfgBytes, launcherPath, lDataPaths, lContent]() {
            auto reportFail = [safeSelf](const QString &path, const QString &why) {
                QMetaObject::invokeMethod(safeSelf.data(),
                    [safeSelf, path, why]() {
                        if (!safeSelf) return;
                        safeSelf->onAsyncWriteFailed(path, why);
                    }, Qt::QueuedConnection);
            };

            (void)safefs::snapshotBackup(cfgPath);
            {
                QFile f(cfgPath);
                if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    reportFail(cfgPath, f.errorString());
                    return;
                }
                f.write(cfgBytes);
            }

            // Mtime gate: if launcher.cfg is newer than the openmw.cfg
            // we just wrote, the user has touched the launcher between
            // our render and now (e.g. opened openmw-launcher);
            // absorbExternalLoadOrder picks the signal up on the next
            // saveModList cycle, so skip clobbering it here.
            const QFileInfo launcherInfo(launcherPath);
            const QFileInfo newCfgInfo(cfgPath);
            if (launcherInfo.exists() && newCfgInfo.exists()
                && launcherInfo.lastModified() > newCfgInfo.lastModified())
                return;

            QFile lf(launcherPath);
            if (!lf.open(QIODevice::ReadOnly | QIODevice::Text)) return;
            const QString before = QString::fromUtf8(lf.readAll());
            lf.close();

            const QString after =
                openmw::renderLauncherCfg(before, lDataPaths, lContent);
            if (after.isEmpty() || after == before) return;

            (void)safefs::snapshotBackup(launcherPath);
            QFile wf(launcherPath);
            if (!wf.open(QIODevice::WriteOnly | QIODevice::Text)) {
                reportFail(launcherPath, wf.errorString());
                return;
            }
            wf.write(after.toUtf8());
        });
    const qint64 ms_writeQueue = syncPhase.elapsed();

    qCInfo(logging::lcOpenMW).nospace()
        << "syncOpenMWConfig ms total=" << syncTotal.elapsed()
        << " scanMods=" << ms_scanMods
        << " readCfg=" << ms_readCfg
        << " masters=" << ms_masters
        << " render=" << ms_render
        << " writeQueue=" << ms_writeQueue
        << " mods=" << mods.size();
}

void MainWindow::loadModList(const QString &path,
                             const QString &remapFrom,
                             const QString &remapTo)
{
    QFile f(path.isEmpty() ? modlistPath() : path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    // A freshly loaded list is in saved order; end any temporary view sort.
    clearViewSortState();
    m_modList->clear();

    // Schema-versioned read.  parseModlist sniffs the first non-empty
    // line: if it's the v2 JSONL header, dispatch to the JSON parser;
    // otherwise fall back to the v1 tab parser so legacy files still
    // load on first launch under the new code.  Both paths produce a
    // QList<ModEntry>; the loop below converts each entry into the
    // QListWidgetItem the UI expects, plus the per-row install-status
    // probe and the on-disk "is this dir actually populated?" check
    // that the old loader did inline.
    const QString contents = QString::fromUtf8(f.readAll());
    f.close();
    const QList<ModEntry> entries = modlist_serializer::parseModlist(contents);

    for (ModEntry e : entries) {
        if (e.isSeparator()) {
            // Defaults that match the legacy loader for missing colours.
            if (!e.bgColor.isValid()) e.bgColor = QColor(55, 55, 75);
            if (!e.fgColor.isValid()) e.fgColor = QColor(Qt::white);

            auto *item = new QListWidgetItem(e.displayName);
            item->setData(ModRole::ItemType,  ItemType::Separator);
            item->setData(ModRole::BgColor,   e.bgColor);
            item->setData(ModRole::FgColor,   e.fgColor);
            item->setData(ModRole::Collapsed, e.collapsed);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
            m_modList->addItem(item);
            continue;
        }

        // Mid-install placeholder (`installing: true`): rebuild as
        // status==2 with the spinner-prefixed text the delegate expects.
        // The InstallController has no live extraction for this row;
        // the user has to retry or click Install on the Nexus URL.
        if (e.installStatus == 2) {
            auto *item = new QListWidgetItem(e.displayName);
            item->setData(ModRole::ItemType,      ItemType::Mod);
            item->setData(ModRole::CustomName,    e.customName);
            item->setData(ModRole::NexusUrl,      e.nexusUrl);
            item->setData(ModRole::DateAdded,     e.dateAdded);
            item->setData(ModRole::InstallStatus, 0);   // not actually mid-install anymore
            item->setCheckState(Qt::Unchecked);
            m_modList->addItem(item);
            continue;
        }

        // Cross-machine path remap.
        QString modPath = e.modPath;
        if (!remapFrom.isEmpty() && !remapTo.isEmpty()
            && modPath.startsWith(remapFrom))
            modPath = remapTo + modPath.mid(remapFrom.size());

        const QString displayName = e.customName.isEmpty()
            ? QFileInfo(modPath).fileName()
            : e.customName;

        auto *item = new QListWidgetItem(displayName);
        item->setData(ModRole::ItemType,      ItemType::Mod);
        item->setData(ModRole::ModPath,       modPath);
        item->setData(ModRole::CustomName,    e.customName);
        item->setData(ModRole::Annotation,    e.annotation);
        item->setData(ModRole::NexusUrl,      e.nexusUrl);
        item->setData(ModRole::DateAdded,     e.dateAdded);
        if (!e.dependsOn.isEmpty())
            item->setData(ModRole::DependsOn, e.dependsOn);
        if (e.updateAvailable)
            item->setData(ModRole::UpdateAvailable, true);
        if (e.isUtility)
            item->setData(ModRole::IsUtility,  true);
        if (e.isFavorite)
            item->setData(ModRole::IsFavorite, true);
        if (!e.fomodChoices.isEmpty())
            item->setData(ModRole::FomodChoices, e.fomodChoices);
        if (!e.bainChoices.isEmpty())
            item->setData(ModRole::BainChoices, e.bainChoices);
        if (!e.videoUrl.isEmpty())
            item->setData(ModRole::VideoUrl,  e.videoUrl);
        if (!e.sourceUrl.isEmpty())
            item->setData(ModRole::SourceUrl, e.sourceUrl);

        // Same install-status probe as before: an empty directory is
        // treated as not-installed so the missing-master scan and the
        // reinstall-prompt UI behave consistently.
        {
            QDir d(modPath);
            const bool installed = !modPath.isEmpty()
                                && d.exists()
                                && !d.isEmpty();
            item->setData(ModRole::InstallStatus, installed ? 1 : 0);
        }
        item->setCheckState(e.checked ? Qt::Checked : Qt::Unchecked);
        QString tip = e.annotation.isEmpty() ? modPath
                                             : modPath + "\n\n" + e.annotation;
        // Mark a shared install: its folder lives outside this profile's mods
        // dir (it was shared in from another profile, or added from elsewhere).
        if (!modPath.isEmpty() && !m_modsDir.isEmpty()
            && !mod_sharing::cleanModPath(modPath).startsWith(
                   mod_sharing::cleanModPath(m_modsDir) + QLatin1Char('/')))
            tip += QStringLiteral("\n\n") + T("share_external_tooltip");
        item->setToolTip(tip);
        m_modList->addItem(item);
    }

    // Apply any collapsed separators (hidden items must be set after all items exist)
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *sep = m_modList->item(i);
        if (sep->data(ModRole::ItemType).toString() == ItemType::Separator
                && sep->data(ModRole::Collapsed).toBool())
            collapseSection(sep, true);
    }

    updateModCount();
    m_modListLoaded = true;

    // Sync m_modList -> m_model now the widget is populated. Part of the
    // QListWidget->model decoupling: new reads use m_model. Until every reader
    // is migrated, refresh after each bulk widget change so the model never
    // goes stale by more than one operation.
    if (m_model)
        modlist::refreshModelFromList(m_model, m_modList);

    // Warm the data-folder cache for installed mods. Runs on a worker thread
    // so loadModList returns immediately; by the time the user tries to delete
    // a mod the cache is ready and cachedDataFolders returns instantly instead
    // of doing a cold filesystem scan.
    {
        QStringList paths;
        for (int i = 0; i < m_modList->count(); ++i) {
            auto *item = m_modList->item(i);
            if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
            if (item->data(ModRole::InstallStatus).toInt() != 1) continue;
            paths << item->data(ModRole::ModPath).toString();
        }
        m_scans->warmDataFoldersCache(paths);
    }
}

// Launch OpenMW



bool MainWindow::refuseLaunchIfRebootPending()
{
    return launch_warnings::refuseIfRebootPending(this);
}



// Launch non-Morrowind games (via Steam URL or auto-detected executable)















void MainWindow::onViewChangelog(QListWidgetItem *item)
{
    if (!item) return;

    if (m_apiKey.isEmpty()) {
        ui::info(this, T("nxm_api_key_required_title"), T("nxm_api_key_required_body"));
        onSetApiKey();
        if (m_apiKey.isEmpty()) return;
    }

    const QString nexusUrl = item->data(ModRole::NexusUrl).toString();
    const auto ref = parseNexusModUrl(nexusUrl);
    if (!ref) return;
    const int     modId = ref->modId;
    const QString game  = ref->game;

    QString modName = item->data(ModRole::CustomName).toString();
    if (modName.isEmpty()) modName = item->text();

    // Show the dialog immediately with a loading placeholder; the network
    // reply populates it when it arrives.
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(T("changelog_title").arg(modName));
    dlg->setMinimumSize(640, 500);
    dlg->resize(720, 580);

    auto *layout = new QVBoxLayout(dlg);
    auto *browser = new QTextBrowser(dlg);
    browser->setOpenExternalLinks(false);
    browser->setHtml(QStringLiteral("<p style='color:#999; padding:8px'>")
                     + T("changelog_loading") + "</p>");
    layout->addWidget(browser, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    layout->addWidget(bb);

    // The connection is scoped to the dialog lifetime (dlg as context object),
    // so it is automatically disconnected when the user closes the dialog.
    // Filter by game+modId (not the item pointer) so the check is safe even
    // if the row is deleted while the reply is in flight.
    connect(m_nexusCtl, &NexusController::changelogFetched, dlg,
            [browser, game, modId](QListWidgetItem *,
                                   const QString &srcGame, int srcModId,
                                   const QList<NexusClient::ChangelogEntry> &entries) {
        if (srcGame != game || srcModId != modId) return;

        if (entries.isEmpty()) {
            browser->setHtml(QStringLiteral(
                "<p style='color:#999; padding:8px'>No changelog available.</p>"));
            return;
        }

        QString html = QStringLiteral(
            "<html><body style='font-family:sans-serif; margin:8px'>");
        for (const auto &e : entries) {
            html += QStringLiteral("<h3 style='color:#d4a017; margin-bottom:2px;"
                                   " margin-top:12px'>v")
                  + e.version.toHtmlEscaped() + "</h3><ul style='margin-top:2px'>";
            for (const QString &c : e.changes)
                html += "<li>" + c + "</li>";
            html += "</ul>";
        }
        html += "</body></html>";
        browser->setHtml(html);
    });

    dlg->show();
    m_nexusCtl->fetchChangelog(item, game, modId);
}

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

void MainWindow::onSortByDate()
{
    checkpointBeforeSort();   // recoverable pre-sort checkpoint
    // Menu actions pre-set m_dateSortAsc; only the header button toggles.
    if (sender() == m_dateSortBtn)
        m_dateSortAsc = !m_dateSortAsc;
    m_dateSortBtn->setText(m_dateSortAsc
        ? T("col_date_added_asc")
        : T("col_date_added_desc"));

    // View-only sort (see onSortBySize): reorders the display, never the saved
    // order. No saveModList() - rowOrderForPersist() keeps disk canonical.
    enterViewSort();
    modlist_sort::byDate(m_modList, m_dateSortAsc);
    updateSectionCounts();

    if (m_notify) m_notify->showSticky(T("viewsort_banner"), QStringLiteral("#7a5c17"));
    statusBar()->showMessage(
        m_dateSortAsc ? T("status_sorted_asc") : T("status_sorted_desc"), 3000);
}

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

void MainWindow::applyCurrentProfileToMirrors()
{
    const GameProfile &gp = m_profiles->current();
    m_modsDir            = gp.modsDir;
    if (m_downloadQueue) m_downloadQueue->setModsDir(m_modsDir);
    m_openmwPath         = gp.openmwPath;
    m_openmwLauncherPath = gp.openmwLauncherPath;
    QDir().mkpath(m_modsDir);
}

void MainWindow::updateGameButton()
{
    if (!m_gameBtn || m_profiles->isEmpty()) return;

    const GameProfile &gp = m_profiles->current();
    m_gameBtn->setText("  " + gp.displayName + "  ▾");

    auto *menu = new QMenu(m_gameBtn);

    // The pinned-game list comes from the GameAdapter registry now --
    // each adapter sets pinned()=true to surface its game at the top
    // of the menu in registration order, even when the user hasn't
    // created a profile for it yet.  Other games (Skyrim, Oblivion,
    // Fallout 4, Cyberpunk, Witcher 1-3, Stardew, Gothic 1-3, Dark
    // Souls, Mortal Shell, Skyblivion, Skywind, Fallout London…) are
    // intentionally NOT pinned for the current release pending per-game
    // testing of the install/launch paths.
    QSet<int> pinnedIdx;
    for (const GameAdapter *a : GameAdapterRegistry::pinned()) {
        const QString  pid          = a->id();
        const QString  fallbackName = a->displayName();
        int found = -1;
        for (int i = 0; i < m_profiles->size(); ++i)
            if (m_profiles->games()[i].id == pid) { found = i; break; }

        if (found >= 0) {
            // Use the pinned name, not whatever the profile stored
            auto *act = menu->addAction(fallbackName);
            act->setCheckable(true);
            act->setChecked(found == m_profiles->currentIndex());
            connect(act, &QAction::triggered, this, [this, found]() { switchToGame(found); });
            pinnedIdx.insert(found);
        } else if (a->isMorrowind()) {
            // OpenMW must always be configured (created on first run); if it
            // somehow isn't, fall back to a disabled placeholder.
            auto *act = menu->addAction(fallbackName);
            act->setEnabled(false);
        } else {
            // Click to detect + add this game on first use.
            auto *act = menu->addAction(fallbackName);
            connect(act, &QAction::triggered, this,
                    [this, pid, fallbackName]() { addAndDetectGame(pid, fallbackName); });
        }
    }

    // Remaining (non-pinned) games the user has added are gated on the
    // experimental "Show all games" preference (Settings menu).  0.4
    // ships pinned to OpenMW + FNV whose install/launch paths are tested
    // for the release; flipping the toggle surfaces the legacy game list
    // for users who installed pre-0.4 with another game configured.
    if (Settings::showAllGames()) {
        bool needSep = true;
        for (int i = 0; i < m_profiles->size(); ++i) {
            if (pinnedIdx.contains(i)) continue;
            if (needSep) { menu->addSeparator(); needSep = false; }
            auto *act = menu->addAction(m_profiles->games()[i].displayName);
            act->setCheckable(true);
            act->setChecked(i == m_profiles->currentIndex());
            connect(act, &QAction::triggered, this, [this, i]() { switchToGame(i); });
        }

        menu->addSeparator();
        menu->addAction(T("toolbar_manage_games"), this, &MainWindow::onAddGame);
    }

    // Replace the old menu (avoid memory leak)
    delete m_gameBtn->menu();
    m_gameBtn->setMenu(menu);

    // Show the right launch button(s) for the current game.  Per-game
    // flags (isMorrowind, hasLauncher) live on the game's GameAdapter
    // -- adding a new total conversion that hides the launcher button
    // is now an override on its adapter class, not a fresh string
    // compare here.  Falls back to "no adapter = treat as plain Steam
    // game" so unknown profile ids stay launchable.
    const GameAdapter *adapter = GameAdapterRegistry::find(gp.id);
    const bool isMorrowind = adapter && adapter->isMorrowind();
    const bool hasLauncher = !adapter || adapter->hasLauncher();
    auto setProfileVis = [this](QAction *a, bool v) {
        if (!a) return;
        a->setProperty("nerev_profile_visible", v);
        m_tbCustom->applyVisibility(a);
    };
    setProfileVis(m_actLaunchOpenMW,        isMorrowind);
    setProfileVis(m_actLaunchLauncher,      isMorrowind);
    setProfileVis(m_actLaunchGame,          !isMorrowind);
    setProfileVis(m_actLaunchSteamLauncher, !isMorrowind && hasLauncher);
    setProfileVis(m_actTuneSkyrimIni,       gp.id == "skyrimspecialedition");
    setProfileVis(m_actSortLoot,            !lootGameFor(gp.id).isEmpty());
    // Mods-menu twin of the toolbar LOOT action: not subject to the
    // user-visibility gate in m_tbCustom->applyVisibility(), so toggle it
    // with the plain QAction API.
    if (m_actMenuSortLoot)
        m_actMenuSortLoot->setVisible(!lootGameFor(gp.id).isEmpty());
    // Bethesda Data/ deployment: only for classified non-OpenMW titles
    // (those with a Data subdir).  Experimental; hidden for OpenMW/Morrowind.
    if (m_actDeployBethesda)
        m_actDeployBethesda->setVisible(adapter && !adapter->dataSubdir().isEmpty());
    if (m_actUndeployBethesda)
        m_actUndeployBethesda->setVisible(adapter && !adapter->dataSubdir().isEmpty());
    if (m_actInspectDeployment)
        m_actInspectDeployment->setVisible(adapter && !adapter->dataSubdir().isEmpty());

    // Featured Modlists dropdown is parked behind a "Work in progress"
    // dialog (see setupToolbar). When the feature is revived, restore the
    // per-game menu build here - the old logic is intentionally preserved
    // in version control history.
}

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

void MainWindow::saveModListFor(const QString &profileKey,
                                QListWidgetItem *placeholder)
{
    if (!placeholder) return;
    const QUuid token = placeholder->data(ModRole::InstallToken).toUuid();
    // The token is the ONLY identity we have for the row inside the
    // foreign profile's file -- no token, no safe replace.  (This is by
    // design: identifying by displayName / nexusUrl can collide when a
    // user re-installs the same mod twice in the same session.)
    const QString file = modlistPathFor(profileKey);
    if (file.isEmpty()) return;

    QList<ModEntry> entries;
    {
        QFile in(file);
        if (in.open(QIODevice::ReadOnly | QIODevice::Text)) {
            entries = modlist_serializer::parseModlist(
                          QString::fromUtf8(in.readAll()));
        }
        // Missing file is fine - we'll write a fresh one with this row.
    }

    const ModEntry updated = ModEntry::fromItem(placeholder);

    bool replaced = false;
    if (!token.isNull()) {
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i].installToken == token) {
                entries[i] = updated;
                replaced  = true;
                break;
            }
        }
    }
    if (!replaced) {
        // Fallback: match a status==2 row by nexusUrl (the row this
        // install was originally minted from).  Covers the case where
        // the file pre-dates the InstallToken field.
        const QString url = updated.nexusUrl;
        if (!url.isEmpty()) {
            for (int i = 0; i < entries.size(); ++i) {
                if (entries[i].installStatus == 2
                    && entries[i].nexusUrl == url) {
                    entries[i] = updated;
                    replaced  = true;
                    break;
                }
            }
        }
    }
    if (!replaced) entries.append(updated);

    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        // Cross-profile saves are SYNCHRONOUS (no QtConcurrent worker
        // here - this writes to a foreign profile's file while the
        // user is on a different profile, so there's no UI freeze
        // pressure to relieve), but a silent qCWarning is exactly the
        // failure mode the async-write banner was added for.  Reuse
        // the same UI sink so a read-only mount on a foreign profile
        // surfaces the same way as the active profile.
        onAsyncWriteFailed(file, out.errorString());
        return;
    }
    QTextStream ts(&out);
    ts << modlist_serializer::serializeModlist(entries);
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

void MainWindow::switchToGame(int idx)
{
    if (idx < 0 || idx >= m_profiles->size() || idx == m_profiles->currentIndex()) return;

    saveModList(); // persist current game's list before switching

    // Take in-flight install rows OUT of m_modList before the clear()
    // below destroys them - they live on in m_strandedInstalls so the
    // InstallController's pending signals still hit a valid pointer.
    strandInflightInstalls();

    m_profiles->setCurrentIndex(idx);
    applyCurrentProfileToMirrors();

    // Forbidden mods are per-game: rebind to the new game's list so the
    // manager and install-block stop showing the previous game's entries.
    reloadForbiddenMods();

    m_modList->clear();
    loadModList();
    // If the user has previously stranded items for THIS profile, splice
    // them back in over the freshly-loaded rows.
    restoreStrandedInstalls();
    updateGameButton();
    updateProfileButton();

    statusBar()->showMessage(
        T("status_switched_game").arg(m_profiles->current().displayName), 3000);
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

void MainWindow::onManageModlistProfiles()
{
    if (m_profiles->isEmpty()) return;
    GameProfile &gp = m_profiles->current();

    QDialog dlg(this);
    dlg.setWindowTitle(T("profile_manage_title").arg(gp.displayName));
    dlg.resize(440, 320);
    auto *lay = new QVBoxLayout(&dlg);

    auto *list = new QListWidget(&dlg);
    auto refresh = [&]() {
        list->clear();
        for (int i = 0; i < gp.modlistProfiles.size(); ++i) {
            const ModlistProfile &mp = gp.modlistProfiles[i];
            QString label = mp.name;
            if (i == gp.activeModlistIdx) label += T("profile_active_suffix");
            if (mp.modsDir.isEmpty())     label += T("profile_no_mods_suffix");
            list->addItem(label);
        }
        if (gp.activeModlistIdx >= 0 && gp.activeModlistIdx < gp.modlistProfiles.size())
            list->setCurrentRow(gp.activeModlistIdx);
    };
    refresh();
    lay->addWidget(list, 1);

    auto *btnRow = new QHBoxLayout;
    auto *newBtn    = new QPushButton(T("profile_btn_new"),    &dlg);
    auto *cloneBtn  = new QPushButton(T("profile_btn_clone"),  &dlg);
    auto *renameBtn = new QPushButton(T("profile_btn_rename"), &dlg);
    auto *deleteBtn = new QPushButton(T("profile_btn_delete"), &dlg);
    auto *activeBtn = new QPushButton(T("profile_btn_set_active"), &dlg);
    btnRow->addWidget(newBtn);
    btnRow->addWidget(cloneBtn);
    btnRow->addWidget(renameBtn);
    btnRow->addWidget(deleteBtn);
    btnRow->addWidget(activeBtn);
    lay->addLayout(btnRow);

    auto *closeBtn = new QPushButton(T("close"), &dlg);
    lay->addWidget(closeBtn);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);

    connect(newBtn, &QPushButton::clicked, &dlg, [&]() {
        if (createNewModlistProfile() >= 0) refresh();
    });
    connect(cloneBtn, &QPushButton::clicked, &dlg, [&]() {
        const int srcIdx = list->currentRow();
        if (srcIdx < 0) return;
        bool ok = false;
        const QString suggest = gp.modlistProfiles[srcIdx].name +
                                T("profile_clone_suffix");
        const QString name = QInputDialog::getText(
            &dlg, T("profile_clone_title"),
            T("profile_clone_prompt").arg(gp.modlistProfiles[srcIdx].name),
            QLineEdit::Normal, suggest, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        if (m_profiles->cloneModlistProfile(srcIdx, name) < 0) {
            ui::warn(&dlg, T("profile_clone_title"), T("profile_new_collision").arg(name));
            return;
        }
        refresh();
    });
    connect(renameBtn, &QPushButton::clicked, &dlg, [&]() {
        const int idx = list->currentRow();
        if (idx < 0) return;
        bool ok = false;
        const QString name = QInputDialog::getText(
            &dlg, T("profile_rename_title"), T("profile_rename_prompt"),
            QLineEdit::Normal, gp.modlistProfiles[idx].name, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        if (!m_profiles->renameModlistProfile(idx, name)) {
            ui::warn(&dlg, T("profile_rename_title"), T("profile_new_collision").arg(name));
            return;
        }
        refresh();
        updateProfileButton();
    });
    connect(deleteBtn, &QPushButton::clicked, &dlg, [&]() {
        const int idx = list->currentRow();
        if (idx < 0) return;
        if (gp.modlistProfiles.size() <= 1) {
            ui::info(&dlg, T("profile_delete_title"), T("profile_delete_last"));
            return;
        }
        const QString name = gp.modlistProfiles[idx].name;
        QMessageBox box(&dlg);
        box.setWindowTitle(T("profile_delete_title"));
        box.setText(T("profile_delete_body").arg(name));
        auto *delFiles    = box.addButton(T("profile_delete_btn_state"),
                                          QMessageBox::DestructiveRole);
        auto *keepFiles   = box.addButton(T("profile_delete_btn_keep"),
                                          QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        const bool deleteFiles = (box.clickedButton() == delFiles);
        if (box.clickedButton() != delFiles && box.clickedButton() != keepFiles)
            return;
        // If the user removes the currently-active profile, save its state
        // first (the registry will fall back to another profile).
        const bool wasActive = (idx == gp.activeModlistIdx);
        if (wasActive) {
            saveModList();
            saveLoadOrder();
        }
        if (!m_profiles->removeModlistProfile(idx, deleteFiles)) {
            ui::warn(&dlg, T("profile_delete_title"), T("profile_delete_failed"));
            return;
        }
        // Removing the active profile already promoted a sibling in the
        // registry; pull its state into this window.
        if (wasActive) {
            applyCurrentProfileToMirrors();
            m_modList->clear();
            loadModList();
            syncGameConfig();
        }
        refresh();
        updateProfileButton();
    });
    connect(activeBtn, &QPushButton::clicked, &dlg, [&]() {
        const int idx = list->currentRow();
        if (idx < 0 || idx == gp.activeModlistIdx) return;
        switchToModlistProfile(idx);
        refresh();
    });

    dlg.exec();
}

void MainWindow::updateProfileButton()
{
    if (!m_profileBtn) return;

    if (m_profiles->isEmpty()) {
        m_profileBtn->setVisible(false);
        return;
    }

    GameProfile &gp = m_profiles->current();
    m_profileBtn->setVisible(true);
    m_profileBtn->setText("  " + gp.activeModlist().name + "  ▾");
    m_profileBtn->setToolTip(T("profile_btn_tooltip"));

    auto *menu = new QMenu(m_profileBtn);
    for (int i = 0; i < gp.modlistProfiles.size(); ++i) {
        const ModlistProfile &mp = gp.modlistProfiles[i];
        QString label = mp.name;
        if (i == gp.activeModlistIdx) label += T("profile_active_suffix");
        QAction *act = menu->addAction(label);
        if (i == gp.activeModlistIdx) {
            act->setCheckable(true);
            act->setChecked(true);
        } else {
            connect(act, &QAction::triggered, this,
                    [this, i]() { switchToModlistProfile(i); });
        }
    }
    menu->addSeparator();
    // Inline rename - one-click access to the most common profile edit so
    // the user doesn't have to open the Manage dialog just to fix a typo.
    // The active profile is the implicit target; the menu entry shows its
    // name so there's no ambiguity about what's being renamed.
    QAction *renameAct = menu->addAction(
        T("profile_menu_rename").arg(gp.activeModlist().name));
    connect(renameAct, &QAction::triggered, this, [this]() {
        if (m_profiles->isEmpty()) return;
        GameProfile &g = m_profiles->current();
        const int idx = g.activeModlistIdx;
        if (idx < 0 || idx >= g.modlistProfiles.size()) return;
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, T("profile_rename_title"), T("profile_rename_prompt"),
            QLineEdit::Normal, g.modlistProfiles[idx].name, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        if (!m_profiles->renameModlistProfile(idx, name)) {
            ui::warn(this, T("profile_rename_title"), T("profile_new_collision").arg(name));
            return;
        }
        updateProfileButton();
    });
    QAction *newAct = menu->addAction(T("profile_menu_new"));
    connect(newAct, &QAction::triggered, this, [this]() {
        const int idx = createNewModlistProfile();
        if (idx >= 0) {
            // Switch to the new (empty) profile so the user's next action
            // - install or drop a Wabbajack - lands there.
            switchToModlistProfile(idx);
        }
    });
    QAction *manageAct = menu->addAction(T("profile_menu_manage"));
    connect(manageAct, &QAction::triggered, this,
            &MainWindow::onManageModlistProfiles);

    delete m_profileBtn->menu();
    m_profileBtn->setMenu(menu);
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
