#include "mainwindow.h"
#include "annotation_codec.h"
#include "load_order_merge.h"
#include "separatordialog.h"
#include "modlistdelegate.h"
#include "modroles.h"
#include "translator.h"
#include "fomod_install.h"
#include "fomodwizard.h"
#include "installcontroller.h"
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
#include "ini_doc.h"
#include "nexus_name.h"
#include "conflict_scan.h"
#include "reboot_check.h"
#include "toolbar_customization.h"
#include "scan_coordinator.h"
#include "backup_manager.h"
#include "bulk_install_queue.h"
#include "review_updates_dialog.h"
#include "launch_warnings.h"
#include "modlist_sort.h"
#include "send_to_dialog.h"
#include "logging.h"
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
#include "master_satisfaction.h"
#include "openmwconfigwriter.h"
#include "log_triage.h"
#include "plugin_collisions.h"
#include "asset_collisions.h"
#include "modlist_sync_guard.h"
#include "bsareader.h"
#include "safe_fs.h"
#include "deps_resolver.h"
#include <QFutureWatcher>
#include <QProgressDialog>
#include <atomic>
#include <memory>
// Pull the two plugin-file helpers into the local namespace so existing
// call sites keep their short names.  The real definitions live in
// src/pluginparser.cpp (unit-testable, no Qt widgets).
using plugins::collectDataFolders;
using plugins::readTes3Masters;

// Forward decls for helpers that ARE still defined in this file.
static QString     detectLootBinary();
static QString     lootGameFor(const QString &profileId);

// Game-table accessor - definition lives alongside kBuiltinGames further
// down the file.  Returned via value so the wizard can consume it without
// needing the BuiltinGameDef type visible at this point.
static QList<firstrun::GameChoice> builtinGameChoices();
#include <QDropEvent>
#include <QMimeData>

// ModListWidget: QListWidget subclass that routes a drop onto a separator
// to "just after the separator" (so the dropped mod becomes the first entry
// of that section) instead of Qt's default above/below-by-y-coord behaviour.

// Forward decl: real definition lives near MainWindow::dropEvent.
static bool isInstallableArchiveSuffix(const QString &path);
// Returns true for file types that trigger the list-import flow instead of
// the archive-install flow: .wabbajack files and MO2's modlist.txt.
static bool isImportFileSuffix(const QString &path);

class ModListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    // Accept external file drags (archives) in addition to the existing
    // internal-move drag-drop.  dragEnter/dragMove must call
    // acceptProposedAction for the drop event to fire.
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
        // External file drop (file manager → mod list): handle the archive
        // paths in-place.  DO NOT QCoreApplication::sendEvent the drop up to
        // the MainWindow - Qt's QApplication dropEvent dispatcher looks at
        // the cursor position and re-delivers the drop to the deepest widget
        // under that point, which is *this* ModListWidget again.  That
        // re-enters dropEvent → forwards up → re-enters… until the stack
        // overflows and SIGSEGV fires in QInternalMimeData::formats
        // (observed on Wayland with the True Type Fonts for OpenMW drop).
        //
        // Instead, mirror what MainWindow::dropEvent does: collect the
        // installable archive paths, accept the event synchronously, and
        // defer the actual install to the next event-loop tick via
        // QMetaObject::invokeMethod so the drop fully unwinds before we
        // mutate the list widget.
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

            // Take items in descending order (keeps remaining indices stable),
            // but preserve their original top-to-bottom order in the destination.
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

// ConflictScanner moved to include/loadordercontroller.h + src/loadordercontroller.cpp.
// MainWindow is now just the debounce timer + the snapshot-collection slot
// that feeds the controller, plus onConflictsScanned that writes roles back.

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(T("window_title"));
    setMinimumSize(700, 500);
    setAcceptDrops(true); // for drag-and-drop mod archives

    // Restore window state from the last session. restoreGeometry() also
    // brings back the maximized flag on platforms that encode it in the byte
    // array (most X11/Wayland compositors and Windows do); we also keep an
    // explicit "window/maximized" key as a fallback for first-run parity.
    {
        QSettings s;
        QByteArray geo = s.value("window/geometry").toByteArray();
        bool restored = !geo.isEmpty() && restoreGeometry(geo);
        if (!restored) {
            resize(950, 620);
            if (auto *screen = QGuiApplication::primaryScreen()) {
                QRect avail = screen->availableGeometry();
                move(avail.center().x() - width()  / 2,
                     avail.center().y() - height() / 2);
            }
        }
        if (s.value("window/maximized", false).toBool())
            QTimer::singleShot(0, this,
                [this]{ setWindowState(windowState() | Qt::WindowMaximized); });
    }

    QSettings settings;
    loadApiKey(); // populates m_apiKey (keychain where available, else settings)

    m_profiles = new GameProfileRegistry(this);
    m_profiles->load();
    applyCurrentProfileToMirrors();

    // Load groundcover-approved mod paths (user confirmed at install time).
    {
        const QStringList gc = settings.value("groundcover/approved").toStringList();
        m_groundcoverApproved = QSet<QString>(gc.begin(), gc.end());
    }

    // Load declined-patch keys (user said "no" to enabling an auto-detected
    // patch for a newly-added mod).
    {
        const QStringList dp = settings.value("patches/declined").toStringList();
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
        // Light-grey the parent separator so the user is nudged to either
        // update or delete the offending mod; the tint clears automatically
        // once every mod in the section is resolved.
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

    m_forbidden = new ForbiddenModsRegistry(forbiddenModsPath(), this);
    m_forbidden->load();

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

    // Built before setupMenuBar() so the Columns submenu can wire its actions;
    // the bar's underlying widget gets parented into the central layout in
    // setupCentralWidget(), and m_modList is bound there too.
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

    QTimer::singleShot(0, m_scans, &ScanCoordinator::scheduleSizeScan);

    // Pre-warm Qt's dialog icon and style resources so the first user-triggered
    // Pre-warm QMessageBox under AppImage: QStyle::standardPixmap,
    // QIcon::fromTheme, and the QMessageBox class itself page in cold off
    // FUSE on first use, adding 200-500 ms to the first dialog. Pre-warming
    // pixmaps + icons covers most of it, and instantiating a QMessageBox
    // offscreen pages in the dialog rendering path. Deferred 100 ms so the
    // main window finishes its first paint first.
    QTimer::singleShot(100, this, [this]() {
        for (auto sp : {QStyle::SP_MessageBoxQuestion, QStyle::SP_MessageBoxWarning,
                        QStyle::SP_MessageBoxInformation, QStyle::SP_MessageBoxCritical}) {
            (void)style()->standardPixmap(sp, nullptr, this);
        }
        for (const char *name : {"dialog-question", "dialog-warning",
                                  "dialog-information", "dialog-error"}) {
            (void)QIcon::fromTheme(QLatin1String(name));
        }

        // Construct a real QMessageBox offscreen and force it through polish,
        // layout and a paint cycle.  WA_DontShowOnScreen routes the widget to
        // an offscreen surface - the window manager never sees it, so there's
        // no flash and no focus theft.  The Yes/No buttons are added so the
        // standard-button path (translated labels + button styles) is also
        // warm by the time the user hits Delete.
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

    // Background AppImage warmup - paged-cache the bundled Qt plugins, icon
    // themes and shared libs so first-use of any of those code paths (image
    // formats, style switches, file dialogs, TLS, GTK theming, etc.) doesn't
    // stall on a cold FUSE read off the squashfs mount.  Runs on the global
    // thread pool while the user is browsing the modlist; once it's done,
    // every subsequent open() of those files hits the kernel page cache and
    // returns immediately.  Skipped on regular installs (nothing to warm).
    //
    // Order matters: small + frequently-faulted dirs first (plugin .so's are
    // ~50-500 KB each and faulted at the first relevant API call), then the
    // bigger asset trees.  The walk is read-only, IO-bound, and yields
    // naturally between files via QFile's blocking reads.
    if (!qEnvironmentVariableIsEmpty("APPIMAGE")) {
        QTimer::singleShot(250, this, []() {
            (void)QtConcurrent::run([]() {
                QString appDir = qEnvironmentVariable("APPDIR");
                if (appDir.isEmpty()) {
                    // Fallback: derive from applicationDirPath ($APPDIR/usr/bin).
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

    // Surface a one-time reminder if LOOT isn't installed - otherwise the
    // "auto-sort was skipped" messages are silent and users wonder why.
    QTimer::singleShot(800, this, &MainWindow::maybeShowLootMissingBanner);
    // First-run welcome - game/mods-dir/API-key/integrations.  Deferred so
    // the main window paints before the modal dialog steals focus.
    QTimer::singleShot(300, this, &MainWindow::maybeShowFirstRunWizard);

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

    // Bulk-install drip-feed (drives onInstallFromNexus one item at a time
    // so the Nexus rate-limiter and modal file-picker dialogs don't pile up).
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

    // Ctrl+Home / Ctrl+End: jump to the first / last row of the mod list,
    // regardless of whether the user has a current selection yet.  Qt's
    // built-in list widget handlers require a current item to anchor from,
    // which fails silently on a freshly-loaded list where nothing is
    // highlighted.  We work around that by scrolling + setting currentRow
    // ourselves.
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

    // Ctrl+W: close the application.  Using close() (not qApp->quit()) so
    // QCloseEvent + aboutToQuit fire normally - saveModList and the other
    // shutdown hooks wired through those signals stay in the loop.
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
    // Push undo before a checkbox toggle so it can be undone.
    // We detect a left-press in the checkbox area (leftmost ~22 px of the item).
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
    if (QSettings().value("loot/banner_disabled", false).toBool()) return;
    // Already installed? Nothing to show.
    if (!detectLootBinary().isEmpty()) return;

    // Use the existing notify banner so it auto-dismisses after 7 seconds
    // and plays nicely with the rest of the UI. Left-click opens the LOOT
    // install page; right-click suppresses the banner persistently.
    m_notify->showWithLink(T("loot_banner_missing"), "#8a4a1a",
                           "https://loot.github.io/", "loot_missing");
}

void MainWindow::maybeShowFirstRunWizard()
{
    // One-shot flag - if set, we've already onboarded this user.
    QSettings s;
    if (s.value("wizard/completed", false).toBool()) return;

    // Upgrade path: users who installed the app before the wizard existed
    // already have configured profiles, modlists, API keys, etc.  Surfacing
    // a "pick your game" dialog for them would be absurd.  Detect the
    // obvious signals of prior setup and silently mark the wizard as
    // completed so it never shows up again.
    bool hasExistingModlist = false;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() == ItemType::Mod) {
            hasExistingModlist = true;
            break;
        }
    }
    // Also probe the modlist file on disk for every configured game -
    // the current profile might not be the one with mods on it.
    if (!hasExistingModlist) {
        for (const GameProfile &gp : m_profiles->games()) {
            QString filename = "modlist_" + gp.id + ".txt";
            for (const QString &dir : {QCoreApplication::applicationDirPath() + "/",
                                        QCoreApplication::applicationDirPath() + "/../"})
            {
                QFileInfo fi(dir + filename);
                if (fi.exists() && fi.size() > 0) {
                    hasExistingModlist = true;
                    break;
                }
            }
            if (hasExistingModlist) break;
        }
    }
    // Any other signal of prior use: non-default game list in QSettings,
    // API key already set (in keychain OR legacy QSettings), or a non-
    // default mods-dir override saved for any profile.
    bool hasExistingSettings =
        !m_apiKey.isEmpty() ||
        !s.value("nexus/apikey").toString().isEmpty() ||
        !s.value("games/list").toStringList().isEmpty();

    if (hasExistingModlist || hasExistingSettings) {
        s.setValue("wizard/completed", true);
        return;
    }

    // Build the game list the wizard offers from our built-in table.
    firstrun::Result r;
    if (!firstrun::runWizard(this, builtinGameChoices(), r)) {
        // User cancelled.  Don't mark completed - they'll see it next launch.
        // Also don't bail: the app is still usable with defaults.
        return;
    }

    // -- Apply selections ---
    // Game profile: switchToGame by finding the matching index in the registry.
    for (int i = 0; i < m_profiles->size(); ++i) {
        if (m_profiles->games()[i].id == r.gameId) { switchToGame(i); break; }
    }

    // Mods directory.  Create it if missing.
    if (!r.modsDir.isEmpty()) {
        QDir().mkpath(r.modsDir);
        m_modsDir = r.modsDir;
        if (m_downloadQueue) m_downloadQueue->setModsDir(m_modsDir);
        currentProfile().modsDir = r.modsDir;
        QSettings().setValue("games/" + currentProfile().id + "/mods_dir", r.modsDir);
        saveModList();
    }

    // API key (optional - empty means user skipped)
    if (!r.apiKey.isEmpty()) {
        m_apiKey = r.apiKey;
        saveApiKey(r.apiKey);
        if (m_nexus) m_nexus->setApiKey(m_apiKey);
    }

    // nxm:// handler - force a re-check; the writeability-vs-stale-exec
    // logic inside handles the actual work.
    if (r.registerNxm) checkNxmHandlerRegistration();

    s.setValue("wizard/completed", true);
    statusBar()->showMessage(T("wizard_done_status"), 5000);
}

void MainWindow::setupMenuBar()
{
    auto *fileMenu = menuBar()->addMenu(T("menu_file"));
    fileMenu->addAction(T("menu_new_modlist"), this, &MainWindow::onNewModList);
    fileMenu->addAction(T("menu_export"), this, &MainWindow::exportModList);
    fileMenu->addAction(T("menu_import"), this, &MainWindow::onImportModList);
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
    modsMenu->addSeparator();
    // Sort with LOOT: disabled (hidden) for profiles LOOT doesn't support,
    // so the menu entry mirrors the toolbar button's per-profile visibility.
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
    modsMenu->addAction(T("menu_log_triage"),      this, &MainWindow::onTriageOpenMWLog);
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
            QSettings().setValue("games/" + currentProfile().id + "/openmw_path", path);
        }
    });
    settingsMenu->addAction(T("menu_set_openmw_launcher_path"), this, [this]{
        QString path = QFileDialog::getOpenFileName(
            this, T("launch_locate_launcher"), m_openmwLauncherPath.isEmpty() ? "/usr/bin" : m_openmwLauncherPath);
        if (!path.isEmpty()) {
            m_openmwLauncherPath = path;
            currentProfile().openmwLauncherPath = path;
            QSettings().setValue("games/" + currentProfile().id + "/openmw_launcher_path", path);
        }
    });
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

    double currentScale = QSettings().value("ui/scale_factor", 1.0).toDouble();
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
            QSettings().setValue("ui/scale_factor", factor);
            QMessageBox::information(this,
                T("ui_scale_restart_title"),
                T("ui_scale_restart_body"));
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
    // The feature is on hold - undecided on scope. For now this button opens
    // a "Work in progress" dialog instead of a live dropdown. When the
    // feature comes back, restore setPopupMode(InstantPopup) and the per-game
    // menu population in updateGameButton().
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
        QMessageBox::information(this, T("featured_wip_title"),
            T("featured_wip_body"));
    });
    tb->addWidget(m_featuredModlistsBtn);

    auto *actAddMod = tb->addAction(T("toolbar_add_mod"), this, &MainWindow::onAddMod);

    // Right-aligned section: an expanding spacer pushes subsequent actions to
    // the opposite end of the toolbar.  Forbidden Mods and Check Updates live
    // out here because they're "status" actions (surface warnings / pull
    // updates) rather than mod-management verbs - grouping them with the
    // Modlist Summary button at the right edge keeps the left-hand region
    // focused on editing the list.
    auto *tbSpacer = new QWidget(tb);
    tbSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(tbSpacer);

    // -- Good States dropdown ---
    // Leftmost button of the right-aligned group.  Click to see the list of
    // user-marked "good" modlist checkpoints; the menu is rebuilt on each
    // show so additions/deletions reflect immediately.
    auto *goodStatesBtn = new QToolButton(tb);
    goodStatesBtn->setText(QString("\U0001F4CC ") + T("toolbar_good_states"));
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

    auto *actForbidden = tb->addAction(QString("\U0001F6AB ") + T("menu_forbidden_mods"),
                                       this, [this]{ m_forbidden->showManageDialog(this); });
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(actForbidden)))
        btn->setStyleSheet("color: #c0392b; font-weight: bold;");

    auto *actRestore = tb->addAction(T("toolbar_restore_backup"),
                                      this, [this]{ m_backups->showRestoreBackupDialog(this); });

    auto *actSummary = tb->addAction(T("toolbar_modlist_summary"),
                                      this, &MainWindow::onModlistSummary);
    if (auto *btn = qobject_cast<QToolButton *>(tb->widgetForAction(actSummary)))
        btn->setStyleSheet("color: #1a6fa8; font-weight: bold;");

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
    // (m_gameBtn) is deliberately NOT registered - it's load-bearing.
    // Profile-gated entries (launch buttons, Tune INI, Sort LOOT) are still
    // gated in updateGameButton(); the user preference is ANDed on top.
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

    // Must be called last so all toolbar members are initialised
    updateGameButton(); // sets game button text/menu + shows correct launch button(s)
    m_tbCustom->applyAll();
}

void MainWindow::setupCentralWidget()
{
    m_modList = new ModListWidget(this);
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
        saveModList();
    });

    // Click on the green update-triangle: the user has already installed
    // this mod once, so we skip the first-install dep check (which was
    // popping the "Possible Missing Requirements" screen on every update
    // click) and go straight to the Nexus file picker + download.
    // Forbidden check and reinstall confirm are also redundant on update
    // - the user explicitly asked for it.
    connect(m_delegate, &ModListDelegate::updateArrowClicked, this,
            [this](const QModelIndex &idx){
        auto *item = m_modList->item(idx.row());
        if (!item) return;

        if (m_apiKey.isEmpty()) {
            QMessageBox::information(this, T("nxm_api_key_required_title"),
                T("nxm_api_key_required_body"));
            onSetApiKey();
            if (m_apiKey.isEmpty()) return;
        }

        const QString nexusUrl = item->data(ModRole::NexusUrl).toString();
        const QStringList parts = QUrl(nexusUrl).path().split('/', Qt::SkipEmptyParts);
        if (parts.size() < 3 || parts[1] != "mods") {
            QMessageBox::warning(this, T("nexus_api_error_title"),
                T("install_invalid_url"));
            return;
        }
        const QString game = parts[0];
        bool ok;
        const int modId = parts[2].toInt(&ok);
        if (!ok) {
            QMessageBox::warning(this, T("nexus_api_error_title"),
                T("install_invalid_url"));
            return;
        }

        QString name = item->data(ModRole::CustomName).toString();
        if (name.isEmpty()) name = item->text();
        if (QMessageBox::question(this, T("update_mod_title"),
                T("update_mod_body").arg(name)) != QMessageBox::Yes)
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
        saveModList();
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
            this, [this]() { saveModList(); scheduleConflictScan(); });

    m_modList->viewport()->installEventFilter(this);
    m_modList->installEventFilter(this);

    m_columnHeader->attachListWidget(m_modList);
    connect(m_columnHeader, &ColumnHeader::visibilityChanged,
            this, [this](const ColVisibility &cv) {
        m_delegate->setColVisibility(cv);
        m_modList->viewport()->update();
    });

    // The two sort buttons live here because their clicked signals route to
    // MainWindow's sort slots and their text is updated by those slots when
    // the sort direction toggles. ColumnHeader splices them into the layout.
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

// NXM handler registration check.
// Nexus Mods uses two URL schemes: nxm:// (standard) and nxms:// (premium /
// CDN, SSL variant). Both must be registered or KDE/KIO throws "Unknown
// protocol: nxms" and the download never starts.

// Forbidden mods

void MainWindow::checkNxmHandlerRegistration()
{
    // KDE/KIO needs a per-scheme .protocol file whose `exec=` line points at
    // the CURRENT binary. Historically this got written once and then skipped
    // on later launches - but if the binary moves (rebuild to a different path,
    // packaging change, Flatpak update, …), the stale `exec=` makes KIO fail
    // with "Unknown protocol: nxms" despite the file being present. So on
    // every launch we rebuild the expected content, compare byte-for-byte
    // against what's on disk, and only rewrite+rebuild-sycoca when something
    // actually differs. Cheap when already correct; self-heals everything
    // else. DO NOT re-introduce an early-return that skips re-checking the
    // exec path - that's what broke this in the past.
    // Under AppImage, applicationFilePath() resolves to the per-launch
    // /tmp/.mount_<random>/usr/bin/... path, which is gone by the time KIO
    // tries to invoke it for the next nxm:// click.  The AppImage runtime
    // exports APPIMAGE pointing at the stable .AppImage file itself - use
    // that so the registered handler keeps working across launches.
    QString execPath = qEnvironmentVariable("APPIMAGE");
    if (execPath.isEmpty())
        execPath = QCoreApplication::applicationFilePath();

    // Write the .protocol file to BOTH kservices5 and kservices6. Different
    // KDE deployments pick one or the other; writing both is harmless and
    // eliminates the "it works on my machine" matrix.
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
            QMessageBox::warning(this, T("registration_failed_title"),
                T("registration_failed_body").arg(desktopPath));
            return;
        }
        changed = true;
    }

    if (!changed) return; // everything already correct - skip the expensive
                          // xdg-mime / sycoca rebuild

    // -- Re-register the mime type association and refresh the caches ---
    QProcess reg;
    reg.start("xdg-mime",
        {"default", "nerevarine_organizer.desktop", "x-scheme-handler/nxm"});
    reg.waitForFinished(3000);
    reg.start("xdg-mime",
        {"default", "nerevarine_organizer.desktop", "x-scheme-handler/nxms"});
    reg.waitForFinished(3000);

    QProcess::execute("update-desktop-database", {appDir});

    // Rebuild BOTH sycoca versions we might be on (KF5 / KF6 coexist on some
    // distros mid-transition). Failures are silent - missing tool = that
    // version of KDE isn't installed here.
    QProcess::execute("kbuildsycoca6", {"--noincremental"});
    QProcess::execute("kbuildsycoca5", {"--noincremental"});

    statusBar()->showMessage(T("status_registered_nxm"), 4000);
}

void MainWindow::checkDesktopShortcut()
{
    QSettings settings;
    if (settings.value("shortcuts/skipDesktopCheck", false).toBool())
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
        settings.setValue("shortcuts/skipDesktopCheck", true);

    if (result != QMessageBox::Yes)
        return;

    // Install icon to ~/.local/share/icons so the compositor can find it
    QString iconDir = QDir::homePath() + "/.local/share/icons/hicolor/256x256/apps";
    QDir().mkpath(iconDir);
    QString iconDest = iconDir + "/nerevarine_organizer.png";
    if (!QFile::exists(iconDest))
        QFile::copy(":/assets/icons/cystal_full_0.png", iconDest);
    QProcess::execute("gtk-update-icon-cache",
                      {"-f", "-t", QDir::homePath() + "/.local/share/icons/hicolor"});

    // Write .desktop file
    QFile f(shortcutPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, T("desktop_shortcut_title"),
                             T("desktop_shortcut_failed").arg(shortcutPath));
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
        QMessageBox::warning(this, T("nxm_invalid_url_title"), body);
        return;
    }
    const QString  game    = parsed->game;
    const int      modId   = parsed->modId;
    const int      fileId  = parsed->fileId;
    const QString &key     = parsed->key;
    const QString &expires = parsed->expires;

    // Forbidden mod check - hard block, no install-anyway escape
    if (const ForbiddenMod *f = m_forbidden->find(game, modId)) {
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
        return;
    }

    // Already-installed guard: route Replace through the existing row-reuse
    // path (which deletes the prior folder via PrevModPath after install),
    // and force a fresh placeholder for Separate so the new download lands
    // in its own folder beside the existing entry.
    const auto reinstallChoice = confirmReinstallIfInstalled(game, modId);
    if (reinstallChoice == ReinstallChoice::Cancel) return;
    const bool forceSeparate = (reinstallChoice == ReinstallChoice::Separate);

    if (m_apiKey.isEmpty()) {
        QMessageBox::information(this, T("nxm_api_key_required_title"),
            T("nxm_api_key_required_body"));
        onSetApiKey();
        if (m_apiKey.isEmpty()) return;
    }

    raise();
    activateWindow();
    statusBar()->showMessage(
        T("status_fetching_download").arg(modId).arg(fileId));

    // Reuse an existing row for this mod if one is already in the list, so we
    // don't create a duplicate.  Match by game + modId parsed from the stored
    // NexusUrl.  Pending placeholders (status=0, e.g. MO2 import or a prior
    // download that didn't complete) always qualify - they're the same in-
    // flight slot, just being filled in.  Already-installed rows (status=1)
    // qualify ONLY when the user picked Replace in the dispatch prompt;
    // Separate forces a fresh placeholder so the new download lands beside
    // the existing entry rather than on top of it.
    QString nexusPageUrl = QString("https://www.nexusmods.com/%1/mods/%2").arg(game).arg(modId);
    QListWidgetItem *placeholder = nullptr;
    QListWidgetItem *installedMatch = nullptr;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        const int status = it->data(ModRole::InstallStatus).toInt();
        if (status != 0 && status != 1) continue;
        const QString storedUrl = it->data(ModRole::NexusUrl).toString();
        if (storedUrl.isEmpty()) continue;
        const QStringList parts = QUrl(storedUrl).path().split('/', Qt::SkipEmptyParts);
        if (parts.size() < 3 || parts[1] != QLatin1String("mods")) continue;
        bool ok; const int storedId = parts[2].toInt(&ok);
        if (!ok || storedId != modId) continue;
        if (parts[0].compare(game, Qt::CaseInsensitive) != 0) continue;
        if (status == 0) { placeholder = it; break; }
        if (!installedMatch) installedMatch = it;
    }
    if (!placeholder && !forceSeparate) placeholder = installedMatch;

    if (placeholder) {
        // For an installed match, stash the current folder so addModFromPath
        // can purge it once the new install lands.  Skip when the row already
        // had a previous-path stashed (rare double-fire) to avoid losing the
        // original.
        if (placeholder == installedMatch) {
            const QString currentPath = placeholder->data(ModRole::ModPath).toString();
            if (!currentPath.isEmpty()
                && placeholder->data(ModRole::PrevModPath).toString().isEmpty())
                placeholder->setData(ModRole::PrevModPath, currentPath);
        }
        placeholder->setText(QString("⠋ %1 (mod %2)").arg(T("status_installing_label")).arg(modId));
        placeholder->setData(ModRole::InstallStatus, 2);
        placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    } else {
        placeholder = new QListWidgetItem(
            QString("⠋ %1 (mod %2)").arg(T("status_installing_label")).arg(modId));
        placeholder->setData(ModRole::ItemType,      ItemType::Mod);
        placeholder->setData(ModRole::InstallStatus, 2);
        placeholder->setData(ModRole::NexusUrl,      nexusPageUrl);
        placeholder->setData(ModRole::DateAdded,     QDateTime::currentDateTime());
        placeholder->setCheckState(Qt::Checked);
        placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_modList->addItem(placeholder);
    }
    m_modList->scrollToItem(placeholder);
    saveModList(); // persist URL immediately so it survives a crash or cancel

    // Kick off a title fetch in parallel - arrives well before the download
    // completes, and is used by the FOMOD path to name the installed folder.
    fetchNexusTitle(game, modId, placeholder);

    // Pull the file's md5 + size_in_bytes from files.json so post-download
    // verification has something to check against.  Runs in parallel with
    // fetchDownloadLink - both need to be done before the archive finishes,
    // and the verify is only consulted at finish-time anyway.
    m_nexusCtl->fetchExpectedChecksum(placeholder, game, modId, fileId);

    m_downloadQueue->fetchDownloadLink(game, modId, fileId, key, expires, placeholder);
}

// Download queue - implementation lives in src/downloadqueue.cpp

// sanitizeFolderName - moved to include/fs_utils.h (fsutils::sanitizeFolderName).
// The `using` declaration below keeps existing call sites short.
using fsutils::sanitizeFolderName;

void MainWindow::setupDownloadQueue()
{
    m_downloadQueue = new DownloadQueue(m_modList, m_net, m_nexus, this, this);

    // Adapter lambda: verifyAndExtract is [[nodiscard]] so the PMF form
    // of connect would warn about dropping the precondition result.  The
    // function itself already logs + surfaces a status message on a
    // precondition failure, so we explicitly discard here.
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

    m_downloadQueue->setModsDir(m_modsDir);

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

    // Read the expectations off the placeholder.  Absent → the controller
    // will short-circuit and signal verified() immediately (local-archive
    // drops and NXM flows whose metadata fetch failed both land here).
    const QString expectedMd5  = placeholder->data(ModRole::ExpectedMd5).toString()
                                      .trimmed().toLower();
    const qint64  expectedSize = placeholder->data(ModRole::ExpectedSize).toLongLong();
    m_installCtl->verifyArchive(archivePath, placeholder, expectedMd5, expectedSize);
    return {};
}

void MainWindow::onVerificationStarted(const QString &archivePath)
{
    statusBar()->showMessage(
        T("status_verifying").arg(QFileInfo(archivePath).fileName()));
}

void MainWindow::onArchiveVerified(const QString &archivePath, QListWidgetItem *placeholder)
{
    // The row may have been removed while the MD5 worker was running.  If
    // so, the archive has nowhere to go - drop it and bail.
    if (!m_modList->indexFromItem(placeholder).isValid()) {
        QFile::remove(archivePath);
        return;
    }
    // Either we had nothing to verify, or the verify just succeeded.  In the
    // MD5 path, show the transient "verified OK" status so the user sees
    // the hash ran.  Empty/no-check path: nothing to announce.
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

void MainWindow::onArchiveVerificationFailed(const QString &archivePath,
                                             QListWidgetItem *placeholder,
                                             InstallController::VerifyFailKind kind,
                                             const QString &actual,
                                             const QString &expected)
{
    // Archive is toast regardless of kind - remove it.
    QFile::remove(archivePath);

    // Reset the placeholder row back to "not installed" so the user can try
    // again.  If it was removed mid-verify, skip the row work.
    if (m_modList->indexFromItem(placeholder).isValid()) {
        placeholder->setData(ModRole::InstallStatus,    0);
        placeholder->setData(ModRole::DownloadProgress, QVariant());
        placeholder->setData(ModRole::ExpectedMd5,      QVariant());
        placeholder->setData(ModRole::ExpectedSize,     QVariant());
        placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                              Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable);
        // Restore the display name, stripping the "⠋ installing…" prefix.
        QString name = placeholder->data(ModRole::CustomName).toString();
        if (name.isEmpty()) {
            name = QFileInfo(placeholder->data(ModRole::ModPath).toString()).fileName();
            if (name.isEmpty()) name = QFileInfo(archivePath).completeBaseName();
        }
        if (!name.isEmpty()) placeholder->setText(name);
        saveModList();
    }

    const QString fileName = QFileInfo(archivePath).fileName();
    const QString body = (kind == InstallController::VerifyFailKind::Size)
        ? T("verify_mismatch_size").arg(fileName).arg(actual.toLongLong()).arg(expected.toLongLong())
        : T("verify_mismatch_md5").arg(fileName, actual, expected);
    QMessageBox::warning(this, T("verify_error_title"), body);
    statusBar()->showMessage(T("verify_status_failed"), 6000);
}

std::expected<void, QString>
MainWindow::extractAndAdd(const QString &archivePath, QListWidgetItem *placeholder)
{
    // Pre-flight what the controller used to assume.  A stale/missing
    // archive used to reach QProcess and surface as "unzip exit code 9"
    // which is worse than useless; an unset mods dir would have happily
    // unpacked into the current working directory.
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
        qCWarning(logging::lcInstall)
            << "extractAndAdd: m_modsDir is empty - first-run setup incomplete";
        statusBar()->showMessage(
            T("status_extraction_failed"), 4000);
        return std::unexpected(QStringLiteral("mods-dir-unset"));
    }

    // The QProcess + extension-dispatch lives in InstallController; the
    // FOMOD wizard and addModFromPath steps are driven by the
    // onExtractionSucceeded slot below.
    //
    // Passing the placeholder's current ModPath as a "reuse hint" lets the
    // controller extract back into the existing wrapper dir when this
    // archive was installed here before (including re-installs and
    // cross-machine syncs where the modlist came from the other machine).
    // Without it every install/reinstall would coin a fresh "_<ts>"
    // wrapper and the modlist path would drift.
    const QString reuseHint = placeholder->data(ModRole::ModPath).toString();
    m_installCtl->extractArchive(archivePath, m_modsDir, placeholder, reuseHint);
    return {};
}

void MainWindow::onExtractionFailed(const QString &archivePath,
                                    const QString &extractDir,
                                    QListWidgetItem *placeholder,
                                    InstallController::ExtractFailKind kind,
                                    const QString &detail)
{
    Q_UNUSED(placeholder);
    const QFileInfo fi(archivePath);
    QString body;
    if (kind == InstallController::ExtractFailKind::ProgramMissing) {
        if (detail == QStringLiteral("unrar|7z"))
            body = T("extraction_error_failed_rar").arg(fi.fileName());
        else
            body = T("extraction_error_no_program").arg(detail);
    } else {
        const int code = detail.toInt();
        const QString ext = fi.suffix().toLower();
        if (ext == QLatin1String("7z") || ext == QLatin1String("fomod")) {
            if (code == 1)        body = T("extraction_error_7z_code1").arg(fi.fileName());
            else if (code == 2)   body = T("extraction_error_7z_code2").arg(fi.fileName());
            else if (code == 255) body = T("extraction_error_7z_code255").arg(fi.fileName());
        }
        if (body.isEmpty())
            body = T("extraction_error_failed").arg(fi.fileName()).arg(code);
    }
    QMessageBox::warning(this, T("extraction_error_title"), body);
    statusBar()->showMessage(T("status_extraction_failed"), 4000);
    QDir(extractDir).removeRecursively();
    QFile::remove(archivePath); // auto-clean on failure too
}

void MainWindow::onExtractionSucceeded(const QString &archivePath,
                                       const QString &extractDir,
                                       const QString &modPathIn,
                                       QListWidgetItem *placeholder)
{
    const QFileInfo fi(archivePath);
    QString modPath = modPathIn;

    // FOMOD installer: if the archive ships a ModuleConfig.xml, open the
    // wizard as a non-modal window so the user can run multiple wizards in
    // parallel and click between them freely.  All post-wizard bookkeeping
    // runs inside the onDone callback; we return immediately after show().
    if (FomodWizard::hasFomod(modPath)) {
        const QString priorChoices = placeholder
            ? placeholder->data(ModRole::FomodChoices).toString()
            : QString();
        QStringList installedModNames;
        for (int mi = 0; mi < m_modList->count(); ++mi) {
            auto *mitem = m_modList->item(mi);
            if (mitem->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
            const QString mname = mitem->text();
            if (!mname.isEmpty()) installedModNames.append(mname);
        }
        const QString archiveFileName = fi.fileName();
        const QString title = placeholder
            ? placeholder->data(ModRole::NexusTitle).toString().trimmed()
            : QString();
        const QString sanitizedTitle  = sanitizeFolderName(title);

        FomodWizard::showAsync(modPath, priorChoices, this, installedModNames,
            [this, archivePath, extractDir, modPath,
             placeholder, archiveFileName, sanitizedTitle]
            (const QString &fomodPath, const QString &fomodChoices) {

            if (fomodPath.isEmpty()) {
                QDir(extractDir).removeRecursively();
                QFile::remove(archivePath);
                resetPlaceholderAfterInstallCancel(placeholder, archivePath);
                statusBar()->showMessage(T("fomod_cancelled"), 3000);
                return;
            }

            const auto promote = fomod_install::promote(
                extractDir, modPath, fomodPath, sanitizedTitle, m_modsDir);

            QString finalPath = modPath;
            if (promote.outcome == fomod_install::PromoteOutcome::EmptyFallback) {
                QMessageBox::warning(this, T("fomod_empty_title"),
                    T("fomod_empty_body").arg(archiveFileName));
            } else {
                finalPath = promote.finalModPath;
                if (placeholder && !fomodChoices.isEmpty())
                    placeholder->setData(ModRole::FomodChoices, fomodChoices);
            }

            addModFromPath(finalPath, placeholder);
            QFile::remove(archivePath);
        });
        return; // wizard is now shown; callback drives the rest
    }

    addModFromPath(modPath, placeholder);
    QFile::remove(archivePath);
}

void MainWindow::resetPlaceholderAfterInstallCancel(QListWidgetItem *placeholder,
                                                     const QString &archivePath)
{
    // Row may have been removed mid-install.  Bail silently if so.
    if (!placeholder || !m_modList->indexFromItem(placeholder).isValid())
        return;
    placeholder->setData(ModRole::InstallStatus,    0);
    placeholder->setData(ModRole::DownloadProgress, QVariant());
    // The extracted folder was deleted - clear its path so the modlist
    // doesn't store a reference to a non-existent dir.
    placeholder->setData(ModRole::ModPath,          QVariant());
    placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                          Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable);
    // Recover the display name and persist it into CustomName so it
    // survives a save/reload cycle (loadModList rebuilds the display name
    // from CustomName, not from item->text()).
    QString name = placeholder->data(ModRole::CustomName).toString();
    if (name.isEmpty())
        name = QFileInfo(archivePath).completeBaseName();
    if (!name.isEmpty()) {
        placeholder->setText(name);
        placeholder->setData(ModRole::CustomName, name);
    }
    saveModList();
}

void MainWindow::prepareItemForInstall(QListWidgetItem *item)
{
    QString name = item->data(ModRole::CustomName).toString();
    if (name.isEmpty()) name = item->text();
    item->setText(QString("⠋ %1 (%2)").arg(T("status_installing_label"), name));
    item->setData(ModRole::InstallStatus, 2);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
}

void MainWindow::onInstallFromNexus(QListWidgetItem *item)
{
    if (m_apiKey.isEmpty()) {
        QMessageBox::information(this, T("nxm_api_key_required_title"),
            T("nxm_api_key_required_body"));
        onSetApiKey();
        if (m_apiKey.isEmpty()) return;
    }

    QString nexusUrl = item->data(ModRole::NexusUrl).toString();
    QStringList parts = QUrl(nexusUrl).path().split('/', Qt::SkipEmptyParts);
    // path: /{game}/mods/{modId}
    if (parts.size() < 3 || parts[1] != "mods") {
        QMessageBox::warning(this, T("nexus_api_error_title"), T("install_invalid_url"));
        return;
    }
    QString game = parts[0];
    bool ok;
    int modId = parts[2].toInt(&ok);
    if (!ok) {
        QMessageBox::warning(this, T("nexus_api_error_title"), T("install_invalid_url"));
        return;
    }

    // Forbidden mod check - hard block, no install-anyway escape
    if (const ForbiddenMod *f = m_forbidden->find(game, modId)) {
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
        return;
    }

    // Already-installed guard - warn (and let user cancel) before re-installing.
    // The Search-on-Nexus flow installs into `item` itself rather than reusing
    // the existing match, so Replace and Separate are functionally identical
    // here - both proceed with `item` as the target.  Only Cancel aborts.
    const auto choice = confirmReinstallIfInstalled(game, modId, item);
    if (choice == ReinstallChoice::Cancel) return;

    checkModDependencies(game, modId, item);
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

void MainWindow::onExpectedChecksumFetched(QListWidgetItem *item,
                                            const QString &md5, qint64 sizeBytes)
{
    if (!m_modList->indexFromItem(item).isValid()) return;
    if (!md5.isEmpty())  item->setData(ModRole::ExpectedMd5,  md5);
    if (sizeBytes > 0)   item->setData(ModRole::ExpectedSize, sizeBytes);
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

MainWindow::ReinstallChoice
MainWindow::confirmReinstallIfInstalled(const QString &game, int modId,
                                         QListWidgetItem *except)
{
    QString gameLc = game.toLower();
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it == except) continue;
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (it->data(ModRole::InstallStatus).toInt() != 1) continue;
        QString url = it->data(ModRole::NexusUrl).toString();
        if (url.isEmpty()) continue;

        QStringList parts = QUrl(url).path().split('/', Qt::SkipEmptyParts);
        if (parts.size() < 3 || parts[1] != "mods") continue;
        bool ok; int existingId = parts[2].toInt(&ok);
        if (!ok) continue;
        if (parts[0].toLower() != gameLc || existingId != modId) continue;

        QString existingName = it->data(ModRole::CustomName).toString();
        if (existingName.isEmpty()) existingName = it->text();

        // Three-way disambiguation: a single Nexus mod page can ship
        // multiple distinct optional files (Wretched + Sage's Backgrounds on
        // mod 58704), but the same modId also identifies "the new version of
        // <mod>" in Nexus's update flow.  The bare OK/Cancel prompt that
        // used to live here interpreted every match as Replace, which
        // silently overwrote the prior install when the user actually
        // wanted a sibling file.  Default the focus to Separate - it's the
        // non-destructive choice and the more common case for mod pages
        // that bundle complementary content.
        QMessageBox box(this);
        box.setWindowTitle(T("reinstall_warn_title"));
        box.setIcon(QMessageBox::Question);
        box.setText(T("reinstall_warn_body").arg(existingName));
        auto *replaceBtn  = box.addButton(T("reinstall_choice_replace"),
                                          QMessageBox::AcceptRole);
        auto *separateBtn = box.addButton(T("reinstall_choice_separate"),
                                          QMessageBox::ActionRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(separateBtn);
        box.exec();
        if (box.clickedButton() == replaceBtn)  return ReinstallChoice::Replace;
        if (box.clickedButton() == separateBtn) return ReinstallChoice::Separate;
        return ReinstallChoice::Cancel;
    }
    return ReinstallChoice::NotInstalled;
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
        const QStringList p = QUrl(u).path().split('/', Qt::SkipEmptyParts);
        if (p.size() < 3 || p[0] != game || p[1] != "mods") continue;
        bool ok = false; int id = p[2].toInt(&ok);
        if (ok) idToUrl[id] = u;
    }
    m_nexusCtl->scanDependencies(item, game, modId, idToUrl);
}

void MainWindow::onDependencyScanFailed(QListWidgetItem *item,
                                        const QString &game, int modId)
{
    // Network or API error - don't block install, just continue.
    fetchModFiles(game, modId, item);
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
        const QString depUrl =
            QString("https://www.nexusmods.com/%1/mods/%2").arg(game).arg(id);

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
    const QString msg = (httpStatus == 403)
        ? T("nexus_api_error_link_403")
        : T("nexus_api_error_link").arg(reason);
    QMessageBox::warning(this, T("nexus_api_error_title"), msg);
    statusBar()->showMessage(T("status_download_failed"), 4000);
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
        QMessageBox::information(this, T("install_pick_file_title"),
            T("install_no_files"));
        statusBar()->clearMessage();
        return;
    }

    auto stashChecksum = [](QListWidgetItem *ph, const NexusClient::FileEntry &f) {
        if (!f.md5.isEmpty()) ph->setData(ModRole::ExpectedMd5,  f.md5);
        if (f.sizeBytes > 0)  ph->setData(ModRole::ExpectedSize, f.sizeBytes);
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
    for (const auto &f : files) {
        const double mb = f.sizeKb / 1024.0;
        auto *li = new QListWidgetItem(
            QString("%1  [v%2]  %3  -  %4 MB")
                .arg(f.name, f.version, f.category)
                .arg(mb, 0, 'f', 1),
            fileList);
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

void MainWindow::addModFromPath(const QString &dirPath, QListWidgetItem *placeholder)
{
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
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                       Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable);
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
    // "<prefix>", "<prefix>_<ts1>", "<prefix>_<ts2>" ….  Delete every sibling
    // folder matching "<prefix>(_<digits>)?" so only the freshly-installed
    // one survives.  Gated on the stripped prefix still looking like a Nexus
    // "name-<id>-<ver>-…-<timestamp>" shape - user-named folders whose name
    // just happens to end in "_1234" never trip the cleanup.
    {
        const QFileInfo newInfo(dirPath);
        const QString parentDir = newInfo.absolutePath();
        const QString cleanMods = m_modsDir.isEmpty()
            ? QString() : QDir::cleanPath(m_modsDir);
        if (!cleanMods.isEmpty()
            && QDir::cleanPath(parentDir) == cleanMods)
        {
            static const QRegularExpression trailSuffix(
                QStringLiteral("_\\d+$"));
            QString prefix = newInfo.fileName();
            prefix.remove(trailSuffix);
            static const QRegularExpression nexusShape(
                QStringLiteral("^.+-\\d+$"));
            if (nexusShape.match(prefix).hasMatch()) {
                const QRegularExpression siblingPat(
                    QLatin1String("^") + QRegularExpression::escape(prefix) +
                    QLatin1String("(_\\d+)?$"));
                QDir parent(parentDir);
                const QStringList subs = parent.entryList(
                    QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                int removed = 0;
                for (const QString &sub : subs) {
                    if (sub == newInfo.fileName()) continue;
                    if (!siblingPat.match(sub).hasMatch()) continue;
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
                    if (QDir(oldPath).removeRecursively()) ++removed;
                }
                if (removed > 0)
                    statusBar()->showMessage(
                        T("mod_cleaned_siblings").arg(removed), 5000);
            }
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
        const bool insideMods = !cleanRoot.isEmpty()
            && (cleanOld == cleanRoot
                || cleanOld.startsWith(cleanRoot + QLatin1Char('/')));
        if (insideMods && QDir(prevModPath).exists()) {
            m_scans->invalidateDataFoldersCache(prevModPath);
            if (QDir(prevModPath).removeRecursively()) {
                statusBar()->showMessage(
                    T("mod_cleaned_siblings").arg(1), 5000);
            }
        }
    }

    // If the installed folder has a generic name (e.g. "scripts"), try to replace
    // it with the actual Nexus mod title so the list entry is human-readable.
    // Three classes of match:
    //   · exact name in kGenericFolderNames (data / meshes / scripts / …)
    //   · Nexus archive-folder shape like "main-54985-0-6-6-1775044149" or
    //     "main-54985-0-6-6-1775044149_1776202250".  The literal "main"
    //     prefix is what 7z writes when a Nexus archive is extracted
    //     without an explicit output folder; the trailing "_<digits>" is
    //     an upload-iteration marker the CDN sometimes appends, so both
    //     hyphens and underscores must be accepted throughout.
    //   · generic "<anything>-<id>-<v>-<v>-<v>-<timestamp>" shape matching
    //     mods that were drag-dropped with their archive-derived folder
    //     name intact (OAAB_Data-49042-2-5-1-1764958680 etc.) - four or
    //     more numeric segments after the first is enough to tell a
    //     user-named folder from a Nexus-slugged one.
    static const QStringList kGenericFolderNames = {
        "scripts", "data", "data files", "meshes", "textures", "sounds", "music",
        "bookart", "icons", "splash", "video", "fonts", "main",
        // "mygui" is a UI-layout directory (e.g. Interface Reimagined ships
        // a `mygui/` root in the archive).  When users drag-drop the
        // archive's inner folder directly, the mod ends up in the list as
        // literally "mygui" - useless to scan for, so auto-rename it to
        // the Nexus title whenever the URL is known.
        "mygui",
        // FOMOD installers frequently call their required module "00 Core"
        // (OAAB_Data, Tamriel Rebuilt, and countless smaller mods).  If the
        // user picks the inner folder instead of the archive root, the mod
        // lands in the list as literally "00 Core" - give it the Nexus title
        // instead, same as the other generic names above.
        "00 core",
        // Bare "complete pack" sometimes ends up as the folder name for mods
        // whose archive root is literally "Complete pack" - same story as
        // "main"/"mygui": no information left to scan, so fall back to the
        // Nexus title when available.
        "complete pack",
        // Some mods ship an inner folder literally called "open_mw" or
        // "sm_CV_mask" etc.  Rename to the Nexus title like the rest.
        "open_mw",
        "sm_cv_mask",
        "sm_m_blade",
        "hq",
        "mq",
        "animations",
        "disenchanting",
        "disenchant"
    };
    static const QRegularExpression kNexusArchiveShape(
        // "main-<id>-<v>-<ts>" and "complete pack-<id>-<v>-<ts>" are the two
        // common "archive-root-plus-version-chain" slugs that survive a
        // drag-drop or an unnamed extraction.  Matching both here lets their
        // trailing version chain be arbitrarily long (or short) without
        // having to dial the generic kNexusVersionedArchive threshold down.
        QStringLiteral(R"(^(main|complete pack)[-_]\d+([-_]\d+)*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kNexusVersionedArchive(
        QStringLiteral(R"(^.+[-_]\d+([-_]\d+){3,}$)"),
        QRegularExpression::CaseInsensitiveOption);
    // Common inner-folder prefixes that are too generic to keep as a list
    // entry: "sound", "audio", "mesh(es)", "fix(es)" - with or without
    // appended suffixes (soundfx, SoundPack, audio01, meshes_replacer,
    // fixes_pack, …).  Drag-dropped inner folders of replacer / patch
    // archives land as literally these names, same story as "main" / "mygui"
    // above.  Subsumes the bare "sounds" and "meshes" entries in the list.
    static const QRegularExpression kGenericPrefixFolder(
        QStringLiteral(R"(^(sound|audio|mesh|fix).*$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QString folderLc = fi.fileName().toLower();
    const bool genericName = kGenericFolderNames.contains(folderLc)
                          || kNexusArchiveShape.match(folderLc).hasMatch()
                          || kNexusVersionedArchive.match(folderLc).hasMatch()
                          || kGenericPrefixFolder.match(folderLc).hasMatch();
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
                QStringList parts = QUrl(nexusUrl).path().split('/', Qt::SkipEmptyParts);
                if (parts.size() >= 3 && parts[1] == "mods") {
                    bool ok; int modId = parts[2].toInt(&ok);
                    if (ok) fetchNexusTitle(parts[0], modId, item, /*setAsCustomName=*/true);
                }
            }
        }
    }

    // Hard-coded renames for mods whose folder name is too terse or
    // misleading to be useful as a display name.
    static const QHash<QString, QString> kFolderRenames = {
        { "restock", "(OpenMW 0.49) Restocking" },
    };
    if (item->data(ModRole::CustomName).toString().isEmpty()) {
        auto it = kFolderRenames.find(folderLc);
        if (it != kFolderRenames.end()) {
            item->setData(ModRole::CustomName, it.value());
            item->setText(it.value());
        }
    }

    // If no custom name yet and the folder carries a Nexus version chain
    // (e.g. "Shishi - Redoran Outpost-57535-v1-1-1760726463"), strip the
    // trailing IDs/versions/timestamp and use the human-readable prefix.
    if (item->data(ModRole::CustomName).toString().isEmpty()) {
        static const QRegularExpression kTrailingVersionChain(
            QStringLiteral(R"([-_]\d+(?:[-_]v?\d+){2,}$)"),
            QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch m = kTrailingVersionChain.match(fi.fileName());
        if (m.hasMatch()) {
            QString cleanName = fi.fileName().left(m.capturedStart()).trimmed();
            if (!cleanName.isEmpty()) {
                item->setData(ModRole::CustomName, cleanName);
                item->setText(cleanName);
            }
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

    saveModList();         // reconciles m_loadOrder (adds the new plugins)
    // LOOT sorting is on-demand now - toolbar button "Sort with LOOT".
    updateModCount();
    scheduleConflictScan();

    // -- Groundcover helper ---
    // If the mod looks like a groundcover/grass mod, ask the user every
    // install whether Nerevarine should manage it as groundcover.  The
    // answer is used to set OR clear the approval for both the mod path
    // and the Nexus URL, so changing one's mind on a later reinstall
    // immediately downgrades the mod to regular content= handling.
    if (!m_profiles->isEmpty() && currentProfile().id == "morrowind") {
        const QString modPath = fi.absoluteFilePath();
        QString displayName = item->data(ModRole::CustomName).toString();
        if (displayName.isEmpty()) displayName = item->text();
        // Additional known grass-mod name substrings that don't literally
        // contain "grass"/"groundcover".  Extend this list as new named
        // grass mods become popular; the helper matches case-insensitively
        // against both the filesystem path and the display name.
        static const QStringList kGroundcoverNameHints = {
            QStringLiteral("lush synthesis"),
        };
        auto matchesGrassHint = [&](const QString &s) {
            for (const QString &h : kGroundcoverNameHints)
                if (s.contains(h, Qt::CaseInsensitive)) return true;
            return false;
        };
        const bool looksLikeGroundcover =
            modPath.contains("groundcover", Qt::CaseInsensitive)
         || modPath.contains("grass", Qt::CaseInsensitive)
         || displayName.contains("groundcover", Qt::CaseInsensitive)
         || displayName.contains("grass", Qt::CaseInsensitive)
         || matchesGrassHint(modPath)
         || matchesGrassHint(displayName);

        const QString nexusUrl = item->data(ModRole::NexusUrl).toString();

        if (looksLikeGroundcover) {
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
                p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("\U0001F33F"));
                p.end();
                box.setIconPixmap(pm);
            }
            const bool userYes = (box.exec() == QMessageBox::Yes);

            // Re-apply the latest answer unconditionally.  The mod path
            // changes on every reinstall (timestamp suffix), so even a
            // repeat "yes" may add a fresh path entry; a persisted save
            // + saveModList() keep openmw.cfg in sync either way.
            if (userYes) {
                m_groundcoverApproved.insert(modPath);
                if (!nexusUrl.isEmpty())
                    m_groundcoverApproved.insert(nexusUrl);
            } else {
                m_groundcoverApproved.remove(modPath);
                if (!nexusUrl.isEmpty())
                    m_groundcoverApproved.remove(nexusUrl);
            }
            QSettings s;
            s.setValue("groundcover/approved",
                       QStringList(m_groundcoverApproved.begin(),
                                   m_groundcoverApproved.end()));
            saveModList();   // re-sync cfg with updated groundcover= lines
        }
    }

    // -- Splash screen helper ---
    // If the newly added mod ships a Splash/ directory (splash screen
    // replacer), offer to delete the default Morrowind splash screens so
    // only the mod's replacements show up in-game.
    if (!m_profiles->isEmpty() && currentProfile().id == "morrowind") {
        // Look for splash screen images in the mod.  The mod root itself
        // may BE the Splash/ directory (e.g. path ends in "/Splash"), or it
        // may contain a Splash/ subdirectory somewhere inside.
        QString splashDir;
        {
            // First: check if the mod root itself is a splash directory.
            if (fi.fileName().compare("splash", Qt::CaseInsensitive) == 0
                || fi.fileName().compare("Splash", Qt::CaseInsensitive) == 0) {
                QDir sd(fi.absoluteFilePath());
                QStringList imgs = sd.entryList({"*.tga", "*.bmp", "*.png", "*.jpg"},
                                                QDir::Files);
                if (!imgs.isEmpty()) splashDir = sd.absolutePath();
            }
            // Second: recursively search for a splash/ subdirectory.
            if (splashDir.isEmpty()) {
                QList<QPair<QString,int>> queue;
                queue.append({fi.absoluteFilePath(), 0});
                while (!queue.isEmpty()) {
                    auto [path, depth] = queue.takeFirst();
                    QDir d(path);
                    for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                        if (sub.compare("splash", Qt::CaseInsensitive) == 0) {
                            QDir sd(d.filePath(sub));
                            QStringList imgs = sd.entryList({"*.tga", "*.bmp", "*.png", "*.jpg"},
                                                            QDir::Files);
                            if (!imgs.isEmpty()) { splashDir = sd.absolutePath(); break; }
                        }
                        if (depth < 3)
                            queue.append({d.filePath(sub), depth + 1});
                    }
                    if (!splashDir.isEmpty()) break;
                }
            }
        }

        if (!splashDir.isEmpty()) {
            // Find the base game's Splash/ directory from external data= in openmw.cfg.
            QString baseGameSplash;
            {
                QFile cfg(QDir::homePath() + "/.config/openmw/openmw.cfg");
                if (cfg.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    static const QString kBegin = "# --- Nerevarine Organizer BEGIN ---";
                    bool inManaged = false;
                    for (QString line : QString::fromUtf8(cfg.readAll()).split('\n')) {
                        if (line.endsWith('\r')) line.chop(1);
                        if (line == kBegin)  { inManaged = true;  continue; }
                        if (line.startsWith("# --- Nerevarine Organizer END ---"))
                                             { inManaged = false; continue; }
                        if (inManaged) continue;
                        if (!line.startsWith("data=")) continue;
                        QString path = line.mid(5);
                        if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"'))
                            path = path.mid(1, path.size() - 2);
                        QDir d(path);
                        // Look for Splash/ in this external data directory.
                        for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                            if (sub.compare("splash", Qt::CaseInsensitive) == 0) {
                                QDir sd(d.filePath(sub));
                                QStringList imgs = sd.entryList({"*.tga", "*.bmp", "*.png", "*.jpg"},
                                                                QDir::Files);
                                if (!imgs.isEmpty()) {
                                    baseGameSplash = sd.absolutePath();
                                    break;
                                }
                            }
                        }
                        if (!baseGameSplash.isEmpty()) break;
                    }
                }
            }

            if (!baseGameSplash.isEmpty()) {
                QDir sd(baseGameSplash);
                QStringList defaultSplash = sd.entryList({"*.tga", "*.bmp", "*.png", "*.jpg"},
                                                          QDir::Files);
                if (!defaultSplash.isEmpty()) {
                    int ret = QMessageBox::question(this,
                        T("splash_delete_title"),
                        T("splash_delete_body")
                            .arg(defaultSplash.size())
                            .arg(baseGameSplash),
                        QMessageBox::Yes | QMessageBox::No,
                        QMessageBox::Yes);
                    if (ret == QMessageBox::Yes) {
                        int removed = 0;
                        for (const QString &f : defaultSplash) {
                            if (QFile::remove(sd.filePath(f))) ++removed;
                        }
                        statusBar()->showMessage(
                            T("splash_deleted_status").arg(removed), 5000);
                    }
                }
            }
        }
    }

    // Offer to re-enable patches for this mod that are bundled in OTHER mods
    // as "<N> ... for <ThisMod>" subfolders.  syncOpenMWConfig auto-skips
    // those subfolders while their target is absent, so when the target is
    // finally installed the user may want the patch back - but may also
    // prefer to keep it disabled (e.g. Remiros reinstalled as a dependency
    // of another mod, not because the user wants its grass).  Ask once; the
    // declined set persists the "no" answer across sessions.
    {
        const QString newModName = item->text().trimmed();
        if (!newModName.isEmpty()) {
            auto normalizeModName = [](const QString &s) {
                QString n;
                n.reserve(s.size());
                for (const QChar &c : s)
                    if (c.isLetterOrNumber()) n.append(c.toLower());
                return n;
            };
            const QString newNormalized = normalizeModName(newModName);
            static const QRegularExpression prefixed(
                QStringLiteral("^\\s*\\d+[a-zA-Z]?\\s+(.+)$"));
            static const QRegularExpression forPat(
                QStringLiteral("\\bfor\\s+(.+?)\\s*$"),
                QRegularExpression::CaseInsensitiveOption);

            // Each hit = "<hostModDisplayName>\t<hostModPath>\t<subfolderName>"
            QStringList hits;
            if (newNormalized.length() >= 4) {
                for (int i = 0; i < m_modList->count(); ++i) {
                    auto *other = m_modList->item(i);
                    if (other == item) continue;
                    if (other->data(ModRole::ItemType).toString() != ItemType::Mod)
                        continue;
                    if (other->data(ModRole::InstallStatus).toInt() != 1) continue;
                    const QString hostPath = other->data(ModRole::ModPath).toString();
                    if (hostPath.isEmpty()) continue;
                    QDir host(hostPath);
                    const QStringList subs = host.entryList(
                        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                    for (const QString &sub : subs) {
                        const auto pm = prefixed.match(sub);
                        if (!pm.hasMatch()) continue;
                        const auto fm = forPat.match(pm.captured(1));
                        if (!fm.hasMatch()) continue;
                        if (!normalizeModName(fm.captured(1)).contains(newNormalized))
                            continue;
                        hits << other->text() + '\t' + hostPath + '\t' + sub;
                    }
                }
            }

            if (!hits.isEmpty()) {
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
                    QSettings().setValue("patches/declined",
                        QStringList(m_declinedPatches.begin(),
                                    m_declinedPatches.end()));
                    syncOpenMWConfig();
                }
            }
        }
    }
}

// Existing slots

void MainWindow::onAddSeparator()
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

    int row = m_modList->currentRow();
    m_modList->insertItem(row < 0 ? m_modList->count() : row + 1, item);
    m_modList->setCurrentItem(item);
    statusBar()->showMessage(T("status_separator_added").arg(name), 2000);
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

    if (QMessageBox::question(this, T("remove_title"), msg) != QMessageBox::Yes)
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
            QSettings s;
            s.setValue("groundcover/approved",
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
        for (const QString &p : installedPaths) {
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
                    QSettings().setValue("patches/declined",
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
    m_undoStack->pushUndo();
    auto *item = m_modList->takeItem(row);
    m_modList->insertItem(row - 1, item);
    m_modList->setCurrentRow(row - 1);
    saveModList();
}

void MainWindow::onMoveDown()
{
    int row = m_modList->currentRow();
    if (row < 0 || row >= m_modList->count() - 1) return;
    m_undoStack->pushUndo();
    auto *item = m_modList->takeItem(row);
    m_modList->insertItem(row + 1, item);
    m_modList->setCurrentRow(row + 1);
    saveModList();
}

void MainWindow::onCheckUpdates()
{
    if (m_apiKey.isEmpty()) {
        QMessageBox::information(this, T("nxm_api_key_required_title"),
            T("nxm_api_key_required_body"));
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

        const QStringList parts = QUrl(nexusUrl).path().split('/', Qt::SkipEmptyParts);
        // path: /{game}/mods/{modId}
        if (parts.size() < 3 || parts[1] != "mods") continue;
        bool ok = false;
        const int modId = parts[2].toInt(&ok);
        if (!ok) continue;

        // Clear any stale flag from a previous check
        item->setData(ModRole::UpdateAvailable, false);
        toCheck.append({item, parts[0], modId});
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
        QProcess::startDetached("notify-send",
            {"-i", "dialog-information",
             "-t", "6000",
             T("window_title"),
             T("check_updates_none")});
    } else {
        const QString msg = T("check_updates_found").arg(foundCount);
        statusBar()->showMessage(msg, 5000);
        QProcess::startDetached("notify-send",
            {"-i", "software-update-available",
             "-t", "6000",
             T("window_title"),
             msg});
    }
}

// Batch-update review screen
//
// Collects every mod the last onCheckUpdates pass flagged with
// UpdateAvailable=true, shows them in a checklist, and lets the user either
// update a cherry-picked subset or hit "Update All".  All selected rows go
// through the same path the green-triangle arrow already uses - prepare-
// for-install + fetchModFiles, with autoPickMain so the per-mod file
// picker is skipped (running 15 modal pickers back-to-back is worse than
// just taking the first MAIN/UPDATE file).
//
// Single-slot download queue (kMaxConcurrentDownloads=1) already serialises
// the actual network I/O, so we just loop and kick each one off - they
// line up in m_downloadQueue and drain one at a time.
void MainWindow::onReviewUpdates()
{
    if (m_apiKey.isEmpty()) {
        QMessageBox::information(this, T("nxm_api_key_required_title"),
            T("nxm_api_key_required_body"));
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
        const QStringList parts = QUrl(url).path().split('/', Qt::SkipEmptyParts);
        if (parts.size() < 3 || parts[1] != "mods") continue;
        bool ok; int modId = parts[2].toInt(&ok);
        if (!ok) continue;

        QString name = it->data(ModRole::CustomName).toString();
        if (name.isEmpty()) name = it->text();
        candidates.append({it, name, parts[0], modId, url});
    }

    if (candidates.isEmpty()) {
        QMessageBox::information(this, T("review_updates_title"),
            T("review_updates_nothing"));
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
                    QProcess::startDetached("xdg-open", {path});
                });

                // Reinstall: only meaningful when we know the Nexus source.
                QString nexusUrl = item->data(ModRole::NexusUrl).toString();
                if (!nexusUrl.isEmpty()) {
                    auto *reinstallAct =
                    menu.addAction(T("ctx_reinstall"), this, [this, item]{
                        QString name = item->data(ModRole::CustomName).toString();
                        if (name.isEmpty()) name = item->text();

                        if (QMessageBox::question(this, T("ctx_reinstall"),
                                T("reinstall_confirm").arg(name))
                            != QMessageBox::Yes) return;

                        item->setData(ModRole::InstallStatus, 0);
                        item->setData(ModRole::ModSize, QVariant());
                        item->setData(ModRole::HasMissingMaster, false);
                        item->setData(ModRole::MissingMasters, QStringList());
                        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                       Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable);
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
                    auto btn = QMessageBox::question(this, T("ctx_uninstall"),
                        T("uninstall_confirm").arg(name));
                    if (btn != QMessageBox::Yes) return;
                    if (!path.isEmpty()) {
                        QDir dir(path);
                        if (dir.exists() && !dir.removeRecursively()) {
                            QMessageBox::warning(this, T("uninstall_error_title"),
                                T("uninstall_error_body").arg(path));
                            return;
                        }
                    }
                    delete m_modList->takeItem(m_modList->row(item));
                    saveModList();
                });
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

                // Utility-mod toggle: user marks frameworks / libraries
                // (Skill Framework, OAAB_Data, etc.) so they get a grey
                // background and are visually distinct from content mods.
                // First-time users see an explainer dialog before the
                // flag flips - subsequent toggles are silent.
                // When multiple mods are selected, the action mirrors the
                // right-clicked item's state and applies to all of them.
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
                        QSettings s;
                        if (!s.value("ui/utility_explainer_seen").toBool()) {
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
                            s.setValue("ui/utility_explainer_seen", true);
                        }
                    }
                    m_undoStack->pushUndo();
                    for (auto *it : utilTargets) {
                        it->setData(ModRole::IsUtility, !isUtil);
                        m_modList->update(m_modList->indexFromItem(it));
                    }
                    saveModList();
                });

                // Favourite toggle: mirrors the hovering ★ icon so users
                // who don't discover the icon can still flip the flag from
                // the menu.  No explainer dialog - the semantics are
                // self-evident (gold star appears next to the mod).
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

void MainWindow::onTuneSkyrimIni()
{
    // -- Locate the Skyrim SE "My Games" directory ---
    const QString settingsKey = "games/skyrimspecialedition/ini_dir";
    QString iniDir = QSettings().value(settingsKey).toString();

    auto hasIni = [](const QString &dir) {
        return !dir.isEmpty() &&
               QFileInfo::exists(QDir(dir).filePath("SkyrimPrefs.ini"));
    };

    if (!hasIni(iniDir)) {
        // Probe common Proton locations before asking the user.
        const QStringList candidates = {
            QDir::homePath() + "/.local/share/Steam/steamapps/compatdata/489830/pfx"
                "/drive_c/users/steamuser/My Documents/My Games/Skyrim Special Edition",
            QDir::homePath() + "/.local/share/Steam/steamapps/compatdata/489830/pfx"
                "/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition",
            QDir::homePath() + "/Documents/My Games/Skyrim Special Edition",
        };
        for (const QString &p : candidates) if (hasIni(p)) { iniDir = p; break; }
    }

    if (!hasIni(iniDir)) {
        QMessageBox::information(this, T("skyini_locate_title"),
            T("skyini_locate_body"));
        QString picked = QFileDialog::getExistingDirectory(
            this, T("skyini_locate_dialog"), QDir::homePath());
        if (picked.isEmpty()) return;
        if (!hasIni(picked)) {
            QMessageBox::warning(this, T("skyini_locate_title"),
                T("skyini_locate_missing"));
            return;
        }
        iniDir = picked;
    }
    QSettings().setValue(settingsKey, iniDir);

    QString prefsPath = QDir(iniDir).filePath("SkyrimPrefs.ini");

    IniDoc prefs;
    if (!prefs.load(prefsPath)) {
        QMessageBox::warning(this, T("skyini_error_title"),
            T("skyini_read_error").arg(prefsPath));
        return;
    }

    // -- Build dialog ---
    QDialog dlg(this);
    dlg.setWindowTitle(T("skyini_dialog_title"));
    dlg.setMinimumWidth(460);
    auto *layout = new QVBoxLayout(&dlg);

    auto *pathLbl = new QLabel(T("skyini_editing").arg(prefsPath), &dlg);
    pathLbl->setStyleSheet("color: #666; font-size: 9pt;");
    pathLbl->setWordWrap(true);
    layout->addWidget(pathLbl);

    auto *form = new QFormLayout;

    // Resolution - populate from the primary screen's supported modes, plus
    // common fallbacks, and always include whatever the INI currently holds.
    auto *resBox = new QComboBox(&dlg);
    QStringList knownRes = {
        "1280x720", "1600x900", "1920x1080", "2560x1080", "2560x1440",
        "3440x1440", "3840x2160",
    };
    if (auto *sc = QGuiApplication::primaryScreen()) {
        QSize s = sc->size();
        QString me = QString::number(s.width()) + "x" + QString::number(s.height());
        if (!knownRes.contains(me)) knownRes.prepend(me);
    }
    QString curW = prefs.get("Display", "iSize W");
    QString curH = prefs.get("Display", "iSize H");
    QString curRes = (!curW.isEmpty() && !curH.isEmpty()) ? curW + "x" + curH : QString();
    if (!curRes.isEmpty() && !knownRes.contains(curRes)) knownRes.prepend(curRes);
    knownRes.removeDuplicates();
    resBox->addItems(knownRes);
    if (!curRes.isEmpty()) resBox->setCurrentText(curRes);
    resBox->setEditable(true);
    form->addRow(T("skyini_resolution"), resBox);

    // Display mode - three common presets; maps to the bFull Screen / bBorderless pair
    auto *modeBox = new QComboBox(&dlg);
    modeBox->addItem(T("skyini_mode_fullscreen"));   // index 0
    modeBox->addItem(T("skyini_mode_borderless"));   // index 1
    modeBox->addItem(T("skyini_mode_windowed"));     // index 2
    bool fullScreen = prefs.get("Display", "bFull Screen").toInt() == 1;
    bool borderless = prefs.get("Display", "bBorderless").toInt() == 1;
    modeBox->setCurrentIndex(fullScreen ? 0 : (borderless ? 1 : 2));
    form->addRow(T("skyini_mode"), modeBox);

    // VSync - toggles iVSyncPresentInterval in the same file
    auto *vsyncBox = new QCheckBox(&dlg);
    vsyncBox->setChecked(prefs.get("Display", "iVSyncPresentInterval").toInt() != 0);
    form->addRow(T("skyini_vsync"), vsyncBox);

    // Shadow quality preset - drives fShadowDistance + iShadowMapResolution
    auto *shadowBox = new QComboBox(&dlg);
    shadowBox->addItem(T("skyini_shadow_low"));     // dist 2500, res 1024
    shadowBox->addItem(T("skyini_shadow_medium"));  // dist 4000, res 2048
    shadowBox->addItem(T("skyini_shadow_high"));    // dist 6000, res 4096
    shadowBox->addItem(T("skyini_shadow_ultra"));   // dist 8000, res 4096
    // Guess current preset from fShadowDistance
    double shadowDist = prefs.get("Display", "fShadowDistance").toDouble();
    int shadowIdx = (shadowDist >= 7000) ? 3
                  : (shadowDist >= 5000) ? 2
                  : (shadowDist >= 3000) ? 1 : 0;
    shadowBox->setCurrentIndex(shadowIdx);
    form->addRow(T("skyini_shadow"), shadowBox);

    layout->addLayout(form);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    layout->addWidget(btns);
    connect(btns->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    // -- Apply: write back into SkyrimPrefs.ini ---
    QStringList parts = resBox->currentText().toLower().split('x', Qt::SkipEmptyParts);
    if (parts.size() == 2) {
        bool okW, okH;
        int w = parts[0].toInt(&okW), h = parts[1].toInt(&okH);
        if (okW && okH && w >= 640 && h >= 480) {
            prefs.set("Display", "iSize W", QString::number(w));
            prefs.set("Display", "iSize H", QString::number(h));
        }
    }

    switch (modeBox->currentIndex()) {
        case 0: // Fullscreen
            prefs.set("Display", "bFull Screen", "1");
            prefs.set("Display", "bBorderless",  "0");
            break;
        case 1: // Borderless
            prefs.set("Display", "bFull Screen", "0");
            prefs.set("Display", "bBorderless",  "1");
            break;
        case 2: // Windowed
            prefs.set("Display", "bFull Screen", "0");
            prefs.set("Display", "bBorderless",  "0");
            break;
    }

    prefs.set("Display", "iVSyncPresentInterval", vsyncBox->isChecked() ? "1" : "0");

    switch (shadowBox->currentIndex()) {
        case 0: prefs.set("Display", "fShadowDistance",      "2500.0000");
                prefs.set("Display", "iShadowMapResolution", "1024");  break;
        case 1: prefs.set("Display", "fShadowDistance",      "4000.0000");
                prefs.set("Display", "iShadowMapResolution", "2048");  break;
        case 2: prefs.set("Display", "fShadowDistance",      "6000.0000");
                prefs.set("Display", "iShadowMapResolution", "4096");  break;
        case 3: prefs.set("Display", "fShadowDistance",      "8000.0000");
                prefs.set("Display", "iShadowMapResolution", "4096");  break;
    }

    // Backup + write
    QFile::remove(prefsPath + ".nerevarine.bak");
    QFile::copy(prefsPath, prefsPath + ".nerevarine.bak");
    if (!prefs.save(prefsPath)) {
        QMessageBox::warning(this, T("skyini_error_title"),
            T("skyini_write_error").arg(prefsPath));
        return;
    }

    statusBar()->showMessage(T("skyini_status_saved"), 4000);
}

void MainWindow::onSetApiKey()
{
    bool ok;
    QString key = QInputDialog::getText(
        this, T("api_key_dialog_title"), T("api_key_dialog_prompt"),
        QLineEdit::Password, m_apiKey, &ok);

    if (ok && !key.trimmed().isEmpty()) {
        m_apiKey = key.trimmed();
        saveApiKey(m_apiKey);
        if (m_nexus) m_nexus->setApiKey(m_apiKey);
        statusBar()->showMessage(T("status_api_key_saved"), 3000);
    }
}

// API-key storage - prefers QKeychain (libsecret / KWallet / DPAPI),
// transparently migrates away from the old plain-text QSettings value on
// the first launch after the library becomes available.
//
// When built without HAVE_QTKEYCHAIN (library missing at configure time),
// falls back to QSettings so the app still works - the key lives in
// ~/.config/<vendor>/<app>.conf then, same as before.

static constexpr const char *kKeychainService = "NerevarineOrganizer";
static constexpr const char *kKeychainKey     = "nexus_api_key";
static constexpr const char *kSettingsKey     = "nexus/apikey";

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
        QSettings s;
        QString legacy = s.value(kSettingsKey).toString();
        if (!legacy.isEmpty()) {
            m_apiKey = legacy;
            saveApiKey(legacy);     // writes to keychain
            s.remove(kSettingsKey); // then scrub plain-text copy
        }
    } else {
        // Backend error (no available service, user denied access, …).
        // Fall back to QSettings so the app still works this session.
        qCWarning(logging::lcApp, "Keychain read failed: %s",
                  qUtf8Printable(job->errorString()));
        m_apiKey = QSettings().value(kSettingsKey).toString();
    }
    job->deleteLater();
#else
    m_apiKey = QSettings().value(kSettingsKey).toString();
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
            QSettings().setValue(kSettingsKey, key);
        }
        job->deleteLater();
    });
    job->start();
    // Scrub any stale plain-text copy from QSettings immediately.
    QSettings().remove(kSettingsKey);
#else
    QSettings().setValue(kSettingsKey, key);
#endif
}

void MainWindow::onSetModsDir()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, T("mods_dir_dialog_title"), m_modsDir);
    if (!dir.isEmpty()) {
        m_modsDir = dir;
        if (m_downloadQueue) m_downloadQueue->setModsDir(m_modsDir);
        currentProfile().modsDir = dir;
        QSettings().setValue("games/" + currentProfile().id + "/mods_dir", dir);
        statusBar()->showMessage(T("status_mods_dir_set").arg(m_modsDir), 3000);
    }
}

void MainWindow::onSetLanguage(const QString &language)
{
    if (language == Translator::currentLanguage()) return;
    QSettings().setValue("ui/language", language);
    QMessageBox::information(this,
        T("language_change_title"),
        T("language_change_body"));
}

// Resolve a per-user state file (modlist / loadorder / forbidden) to either
// the binary-adjacent location (dev / release builds) or AppDataLocation
// (AppImage, where applicationDirPath is a read-only mount). Existing file
// wins so users upgrading don't lose state. Under AppImage AppDataLocation
// is always used; the AppImage runtime sets $APPIMAGE so we key off that.
static QString resolveUserStatePath(const QString &filename)
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
    return resolveUserStatePath("modlist_" +
        (m_profiles->isEmpty() ? QString("morrowind") : m_profiles->current().id) + ".txt");
}

QString MainWindow::forbiddenModsPath() const
{
    return resolveUserStatePath(QStringLiteral("forbidden_mods.txt"));
}

QString MainWindow::loadOrderPath() const
{
    return resolveUserStatePath("loadorder_" +
        (m_profiles->isEmpty() ? QString("morrowind") : m_profiles->current().id) + ".txt");
}

void MainWindow::loadLoadOrder()
{
    m_loadOrder.clear();
    QFile f(loadOrderPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        m_loadOrder.append(line);
    }
}

void MainWindow::saveLoadOrder()
{
    QFile f(loadOrderPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return;
    QTextStream out(&f);
    out << "# Plugin load order for this game profile - one filename per line.\n"
        << "# This is SEPARATE from the mod list in the main window; it's what\n"
        << "# actually gets written as `content=` lines to openmw.cfg.\n"
        << "# Edit via: Mods menu → Edit Load Order…\n";
    for (const QString &cf : m_loadOrder) out << cf << "\n";
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

    // Pass 1: openmw.cfg.  This catches the original case the regression
    // test pins - the user reordered in the launcher, clicked Save/Play,
    // and the launcher wrote content= order to openmw.cfg.
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
    QHash<QString, QString> pathByName;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 1) continue;
        QString modPath = item->data(ModRole::ModPath).toString();
        for (const auto &p : collectDataFolders(modPath, contentExts))
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
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 1) continue;
        QString modPath = item->data(ModRole::ModPath).toString();
        for (const auto &p : collectDataFolders(modPath, contentExts)) {
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

    // Topological pass: for each plugin, lift its declared masters above it.
    // One readTes3Masters() call per plugin per reconcile - cheap (plugin
    // headers are <1KB reads) and the sort is stable, so plugins with no
    // dependency edge keep their positions.
    QHash<QString, QStringList> mastersCache;
    m_loadOrder = loadorder::topologicallySortByMasters(
        m_loadOrder,
        [&pathByName, &mastersCache](const QString &name) -> QStringList {
            auto it = mastersCache.constFind(name);
            if (it != mastersCache.constEnd()) return it.value();
            QStringList ms = readTes3Masters(pathByName.value(name));
            mastersCache.insert(name, ms);
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

// Build a QProcessEnvironment safe for launching a *foreign* Qt program
// (LOOT, OpenMW Launcher, MWSE-launcher, …) from inside our AppImage.
//
// linuxdeploy's AppRun exports LD_LIBRARY_PATH / QT_PLUGIN_PATH /
// QML2_IMPORT_PATH / XDG_DATA_DIRS so the Qt6 we ship can find its own
// plugins. QProcess inherits the parent env by default, so a child Qt
// app would resolve `libQt6Core.so` and the platform/IM plugins from
// inside our squashfs - almost always an ABI mismatch with whatever Qt
// the child was linked against. The user-visible failure mode is a
// stray "QIBusPlatformInputContext: invalid portal bus" on the
// terminal followed by the child exiting silently before doing any
// real work (the IBus platform input plugin from our bundled Qt
// can't talk to the child's session bus, and the platform-plugin
// initialization aborts with it).
//
// linuxdeploy stashes the pre-AppImage values with an "_ORIG" suffix
// for exactly this purpose: restore them where present, otherwise
// unset. No-op outside the AppImage runtime.
static QProcessEnvironment childProcessEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (qEnvironmentVariableIsEmpty("APPIMAGE")) return env;

    static const QStringList kAppImageVars = {
        "LD_LIBRARY_PATH",            "QT_PLUGIN_PATH",
        "QT_QPA_PLATFORM_PLUGIN_PATH",
        "QML2_IMPORT_PATH",           "QML_IMPORT_PATH",
        "QTWEBENGINEPROCESS_PATH",
        "PYTHONHOME",                 "PYTHONPATH",
        "PERLLIB",
        "GTK_PATH",                   "GTK_DATA_PREFIX",
        "GTK_EXE_PREFIX",             "GTK_IM_MODULE_FILE",
        "GDK_PIXBUF_MODULE_FILE",     "GDK_PIXBUF_MODULEDIR",
        "GST_PLUGIN_SYSTEM_PATH",     "GST_PLUGIN_SYSTEM_PATH_1_0",
        "FONTCONFIG_FILE",            "FONTCONFIG_PATH",
        "XDG_DATA_DIRS",
    };
    for (const QString &v : kAppImageVars) {
        const QString orig = env.value(v + "_ORIG");
        if (!orig.isEmpty())
            env.insert(v, orig);
        else
            env.remove(v);
    }

    // Our AppRun wrapper prepends an in-AppImage glibc-compat shim onto
    // LD_PRELOAD (see build-appimage.sh step 6b). The shim is harmless for
    // the parent process but, when inherited by a foreign Qt binary like
    // LOOT, it dlopens an in-squashfs .so from the child's address space
    // and the child silently exits during platform-plugin init - the user-
    // visible signal is "QIBusPlatformInputContext: invalid portal bus"
    // followed by no work being done. There's no _ORIG saved for
    // LD_PRELOAD because the wrapper only ever prepends, so strip every
    // entry that lives under APPDIR and keep any user-supplied entries.
    if (env.contains("LD_PRELOAD")) {
        const QString appDir = qEnvironmentVariable("APPDIR");
        QStringList kept;
        const QStringList parts =
            env.value("LD_PRELOAD").split(':', Qt::SkipEmptyParts);
        for (const QString &p : parts) {
            if (!appDir.isEmpty() && p.startsWith(appDir + '/'))
                continue;
            kept << p;
        }
        if (kept.isEmpty()) env.remove("LD_PRELOAD");
        else                env.insert("LD_PRELOAD", kept.join(':'));
    }

    return env;
}

// Maps our per-profile ID to the game-folder name LOOT uses on its CLI
// (`--game <name>`). Keep these in sync with LOOT's built-in game list.
// Returns an empty string for profiles LOOT doesn't support - we then
// politely refuse instead of making up a name and confusing LOOT.
static QString lootGameFor(const QString &profileId)
{
    // Morrowind *with OpenMW* has its own LOOT entry; classic Morrowind is
    // a separate one ("Morrowind"). Our Morrowind profile is the OpenMW
    // one, so we use "OpenMW".
    if (profileId == "morrowind")              return "OpenMW";
    if (profileId == "skyrim")                 return "Skyrim";
    if (profileId == "skyrimspecialedition")   return "Skyrim Special Edition";
    if (profileId == "oblivion")               return "Oblivion";
    if (profileId == "oblivionremastered")     return "Oblivion Remastered";
    if (profileId == "starfield")              return "Starfield";
    if (profileId == "fallout3")               return "Fallout3";
    if (profileId == "falloutnewvegas")        return "FalloutNV";
    if (profileId == "fallout4")               return "Fallout4";
    return QString();
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

    const QStringList args{"--auto-sort", "--game", game};
    auto *cmdLbl = new QLabel(
        T("loot_dialog_command").arg(loot + " " + args.join(' ')), &dlg);
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
    // Strip our AppImage's Qt/library env so LOOT (also Qt) loads its
    // own plugins instead of ours. See childProcessEnvironment() above.
    proc->setProcessEnvironment(childProcessEnvironment());

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

    proc->start(loot, args);
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

void MainWindow::saveModList()
{
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

    // Pick up any reorder the user did in the OpenMW Launcher between
    // launches.  Without this, saving the modlist (triggered by any mod-list
    // mutation - checkbox toggle, drag-drop, install) would rewrite
    // openmw.cfg from the stale m_loadOrder and wipe the launcher's reorder.
    // absorbExternalLoadOrder is mtime-gated so it's a no-op when we wrote
    // loadorder more recently than openmw.cfg, which correctly leaves our
    // own reorders (Edit Load Order dialog, LOOT sort) untouched.
    absorbExternalLoadOrder();

    // README disclaims file corruption - snapshot first, write second.
    (void)safefs::snapshotBackup(modlistPath());

    QFile f(modlistPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        statusBar()->showMessage(T("status_modlist_save_failed"), 3000);
        return;
    }

    QTextStream out(&f);
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() == ItemType::Separator) {
            QColor bg = item->data(ModRole::BgColor).value<QColor>();
            QColor fg = item->data(ModRole::FgColor).value<QColor>();
            out << "# " << item->text()
                << " <color>" << bg.name(QColor::HexArgb) << "</color>"
                << "<fgcolor>" << fg.name(QColor::HexArgb) << "</fgcolor>";
            if (item->data(ModRole::Collapsed).toBool())
                out << "<collapsed>1</collapsed>";
            out << "\n";
        } else {
            QString url     = item->data(ModRole::NexusUrl).toString();
            QString dateStr = item->data(ModRole::DateAdded).toDateTime().toString(Qt::ISODate);

            // Mid-install placeholder: persist as not-installed so the URL
            // survives a crash or cancelled download and can be retried later.
            if (item->data(ModRole::InstallStatus).toInt() == 2) {
                if (url.isEmpty()) continue; // nothing useful to save
                QString name = item->data(ModRole::CustomName).toString();
                if (name.isEmpty()) {
                    name = item->text();
                    if (name.startsWith("⠋ ")) name = name.mid(2);
                }
                out << "- \t" << name << "\t\t" << url << "\t" << dateStr << "\n";
                continue;
            }

            QChar prefix     = (item->checkState() == Qt::Checked) ? '+' : '-';
            // IntendedModPath, when set, is the missing-on-this-machine path
            // that Strategy 3 of repairEmptyModPaths is papering over with a
            // sibling (usually an older version of the same Nexus mod).
            // Serialize THAT so the modlist file stays stable across
            // machines - the machine that does have the folder still
            // resolves it normally.  Without this, sync-pulls would cause
            // each machine to rewrite the path to its own older sibling
            // and produce a ping-pong of meaningless diffs.
            QString intended = item->data(ModRole::IntendedModPath).toString();
            QString modPath  = !intended.isEmpty()
                ? intended
                : item->data(ModRole::ModPath).toString();
            QString custName = item->data(ModRole::CustomName).toString();
            QString annot    = item->data(ModRole::Annotation).toString();
            QString depsStr  = item->data(ModRole::DependsOn).toStringList().join(',');
            // parts[7]: whether Mods → Check Updates saw a newer version on
            // Nexus than the locally stamped DateAdded.  Persisted so the
            // green triangle survives a restart - the next `onCheckUpdates`
            // run refreshes it either way.
            int updateFlag   = item->data(ModRole::UpdateAvailable).toBool() ? 1 : 0;
            // parts[8]: user-set "utility mod" flag (framework / library
            // like Skill Framework or OAAB_Data consumed by other mods).
            // Drives the grey-background rendering; purely cosmetic today.
            int utilityFlag   = item->data(ModRole::IsUtility).toBool()  ? 1 : 0;
            // parts[9]: favourite flag - user marked this mod as personally special.
            int favoriteFlag  = item->data(ModRole::IsFavorite).toBool() ? 1 : 0;
            // parts[10]: serialized FOMOD install choices ("si:gi:pi;...").
            // Empty for non-FOMOD mods or mods installed before this feature.
            QString fomodChoices = item->data(ModRole::FomodChoices).toString();
            // parts[11]: user-set video review URL (YouTube, etc.)
            QString videoUrl  = item->data(ModRole::VideoUrl).toString();
            // parts[12]: non-Nexus source URL (GitHub release, Nexus search fallback, etc.)
            QString sourceUrl = item->data(ModRole::SourceUrl).toString();
            out << prefix << " " << modPath
                << "\t" << custName
                << "\t" << encodeAnnot(annot)
                << "\t" << url
                << "\t" << dateStr
                << "\t"        // parts[5]: reserved (was endorsement state)
                << "\t" << depsStr
                << "\t" << updateFlag
                << "\t" << utilityFlag
                << "\t" << favoriteFlag
                << "\t" << fomodChoices
                << "\t" << videoUrl
                << "\t" << sourceUrl
                << "\n";
        }
    }

    // Keep the separate plugin load-order file in sync with the modlist
    // (additions/removals only; order-within-the-list is edited explicitly
    // by the user via Mods → Edit Load Order… or by autoSortLoadOrder()).
    reconcileLoadOrder();
    saveLoadOrder();

    syncGameConfig();
    scanMissingMasters();
    scanMissingDependencies();
    updateSectionCounts();
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
// Strategy: if collectDataFolders() finds nothing at ModPath, try in order:
//   1. One level up - if the parent DOES contain plugins anywhere under it,
//      rebind to the parent.
//   2. A sibling inside the mods directory whose name shares the same Nexus
//      stem (e.g. "OAAB_Data" empty → rebind to "OAAB_Data-49042-2-5-…" full
//      archive). Happens when a reinstall renames the sanitized output folder
//      but leaves the original extraction behind with the real plugins.
// Done silently; user only notices because the missing-master icons clear
// and the launcher stops complaining.
void MainWindow::repairEmptyModPaths()
{
    static const QStringList contentExts{".esp", ".esm", ".omwaddon", ".omwscripts"};

    const QString modsRootAbs = QFileInfo(m_modsDir).absoluteFilePath();

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
        // InstallStatus 2 = currently installing - leave those alone.
        // Crucially we also handle status 0: loadModList() demotes a mod to
        // "not installed" when its ModPath folder exists but is empty (the
        // OAAB_Data / OAAB Grazelands reinstall-leftover case).  Without this
        // branch, the rebind-to-sibling rescue below could never run for
        // exactly the scenario it was written for.
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
            // Must be strictly INSIDE the mods directory.  Binding AT the
            // root would make this item "own" every mod in the library -
            // that's how this feature broke in the first place.
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

        // Strategy 3: sibling-by-modId rescue. With modPath missing and a
        // parseable NexusUrl, scan modsRoot for any folder whose name
        // contains "-<modId>-". Catches cross-machine sync where each
        // machine has a different version of the same modId (Strategy 2
        // misses dive-into-single-subdir installs because its
        // absolutePath==modsRootAbs gate only fires at modsRoot).
        // On hit: rebind, flag UpdateAvailable, stash the original path
        // in IntendedModPath so saveModList keeps writing the canonical
        // path (otherwise machines would ping-pong on every sync).
        if (rebind.isEmpty()) {
            const QString nexusUrl = item->data(ModRole::NexusUrl).toString();
            static const QRegularExpression reModId(
                QStringLiteral(R"(/mods/(\d+)\b)"));
            const auto m = reModId.match(nexusUrl);
            if (m.hasMatch() && !modsRootAbs.isEmpty()) {
                const QString modIdTag =
                    QStringLiteral("-") + m.captured(1) + QStringLiteral("-");
                // Walk up to the child-of-modsRoot ancestor of modPath.
                // For "/mods/X" it's "X"; for "/mods/X/Y/Z" it's still
                // "X".  The inner tail ("Y/Z") is preserved so we can
                // re-apply it under the chosen sibling - preserving a
                // dive-into-single-subdir install's deeper entry point.
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
        const QStringList p = QUrl(url).path().split('/', Qt::SkipEmptyParts);
        if (p.size() < 3 || p[1] != "mods") continue;
        if (!gameLc.isEmpty() && p[0].toLower() != gameLc) continue;
        bool ok; const int rowId = p[2].toInt(&ok);
        if (ok && rowId == modId) return it;
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
            candidate = QDir(m_modsDir).filePath(
                QString("%1_%2.%3")
                    .arg(srcFi.completeBaseName())
                    .arg(++suffix)
                    .arg(srcFi.suffix()));
        }
        if (!QFile::copy(srcFi.absoluteFilePath(), candidate)) {
            QMessageBox::warning(this, T("file_error_title"),
                T("file_error_write").arg(candidate));
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
        placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
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
        placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
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

// Modlist Summary
//
// Shows total disk footprint of the current modlist and the OpenMW binary
// location. Linux-first (OpenMW is the canonical Morrowind engine on Linux),
// but also aware of Windows install locations for users running Nerevarine
// Organizer on Windows.

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

    qint64 totalBytes    = 0;
    qint64 enabledBytes  = 0;
    int    modCount      = 0;
    int    enabledCount  = 0;
    int    sepCount      = 0;

    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        QString type = item->data(ModRole::ItemType).toString();
        if (type == ItemType::Separator) { ++sepCount; continue; }
        if (item->data(ModRole::InstallStatus).toInt() != 1) continue;
        ++modCount;
        qint64 s = item->data(ModRole::ModSize).toLongLong();
        if (s <= 0) {
            // Async scan may not have landed yet - ScanCoordinator::sizeOf
            // checks the cache first, then falls back to a synchronous walk.
            const QString mp = item->data(ModRole::ModPath).toString();
            if (!mp.isEmpty()) s = m_scans->sizeOf(mp);
        }
        if (s > 0) {
            totalBytes += s;
            if (item->checkState() == Qt::Checked) {
                enabledBytes += s;
                ++enabledCount;
            }
        } else if (item->checkState() == Qt::Checked) {
            ++enabledCount;
        }
    }

    auto fmtBytes = [](qint64 b) {
        if (b <= 0) return QString("0 B");
        const double KB = 1024.0;
        const double MB = KB * 1024.0;
        const double GB = MB * 1024.0;
        if (b >= GB) return QString::number(b / GB, 'f', 2) + " GB";
        if (b >= MB) return QString::number(b / MB, 'f', 1) + " MB";
        if (b >= KB) return QString::number(b / KB, 'f', 0) + " KB";
        return QString::number(b) + " B";
    };

    // OpenMW binary location.
    QString openmwPath = locateOpenMWBinary(m_openmwPath);
    QString openmwLine = openmwPath.isEmpty()
        ? T("summary_openmw_not_found")
        : openmwPath;

    QString modsDirPath = m_modsDir;
    QString cfgPath;
#ifdef Q_OS_WIN
    cfgPath = QDir::homePath() + "/Documents/My Games/OpenMW/openmw.cfg";
#else
    cfgPath = QDir::homePath() + "/.config/openmw/openmw.cfg";
#endif

    // Build dialog.
    QDialog dlg(this);
    dlg.setWindowTitle(T("summary_title"));
    dlg.setMinimumWidth(540);

    auto *vlay = new QVBoxLayout(&dlg);

    auto addRow = [&](const QString &label, const QString &value) {
        auto *row = new QHBoxLayout;
        auto *l = new QLabel("<b>" + label + "</b>", &dlg);
        l->setMinimumWidth(190);
        auto *v = new QLabel(value, &dlg);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->setWordWrap(true);
        v->setStyleSheet("font-family: monospace;");
        row->addWidget(l);
        row->addWidget(v, 1);
        vlay->addLayout(row);
    };

    addRow(T("summary_profile"),          currentProfile().displayName);
    addRow(T("summary_platform"),
#ifdef Q_OS_WIN
        QStringLiteral("Windows")
#else
        QStringLiteral("Linux")
#endif
    );
    vlay->addSpacing(10);

    addRow(T("summary_total_mods"),       QString::number(modCount));
    addRow(T("summary_enabled_mods"),
           QString("%1 / %2").arg(enabledCount).arg(modCount));
    addRow(T("summary_separator_count"),  QString::number(sepCount));
    vlay->addSpacing(10);

    addRow(T("summary_total_size"),       fmtBytes(totalBytes));
    addRow(T("summary_enabled_size"),     fmtBytes(enabledBytes));
    vlay->addSpacing(10);

    addRow(T("summary_mods_dir"),         modsDirPath);
    addRow(T("summary_openmw_binary"),    openmwLine);
    addRow(T("summary_openmw_cfg"),       cfgPath);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    auto *moveBtn = new QPushButton(T("summary_move_mods_btn"), &dlg);
    moveBtn->setStyleSheet("color: #8a4a1a; font-weight: bold;");
    moveBtn->setToolTip(T("summary_move_mods_tooltip"));
    btns->addButton(moveBtn, QDialogButtonBox::ActionRole);
    vlay->addWidget(btns);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(moveBtn, &QPushButton::clicked, &dlg, [this, &dlg]{
        dlg.accept();
        onMoveModsDir();
    });

    dlg.exec();
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
        QMessageBox::warning(this, T("move_mods_title"),
            T("move_mods_err_downloads"));
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
        QMessageBox::information(this, T("move_mods_title"),
            T("move_mods_err_nothing"));
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
        QMessageBox::warning(this, T("move_mods_title"),
            T("move_mods_err_dest_not_dir").arg(dest));
        return;
    }
    if (dest == modsRoot) {
        QMessageBox::warning(this, T("move_mods_title"),
            T("move_mods_err_dest_same"));
        return;
    }
    // Reject nesting in either direction - catching someone picking a sub
    // folder of the current mods dir (or accidentally a parent that contains
    // the current mods dir).
    if (dest.startsWith(modsRoot + "/") || modsRoot.startsWith(dest + "/")) {
        QMessageBox::warning(this, T("move_mods_title"),
            T("move_mods_err_dest_nested"));
        return;
    }

    // Writable test: create + delete a stamp file.
    {
        QString stamp = QDir(dest).filePath(".nerev_write_test");
        QFile probe(stamp);
        if (!probe.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, T("move_mods_title"),
                T("move_mods_err_dest_not_writable").arg(dest));
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
        QMessageBox::warning(this, T("move_mods_title"),
            T("move_mods_err_no_space")
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
        QMessageBox::warning(this, T("move_mods_title"),
            T("move_mods_err_collision").arg(list));
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
                if (QDir(c.oldPath).removeRecursively()) {
                    ok = true;
                } else {
                    // Destination is a verified copy, but the source dir
                    // can't be removed (locked file, permissions…).  Roll
                    // back the copy so we're never left with two live
                    // copies of the same mod; caller can retry manually.
                    QDir(newPath).removeRecursively();
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
        currentProfile().modsDir = dest;
        QSettings().setValue("games/" + currentProfile().id + "/mods_dir", dest);
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

void MainWindow::onInspectOpenMWSetup()
{
    // Force a fresh rewrite of openmw.cfg before we report what we wrote, so
    // the user sees the actual current state rather than a stale one.
    syncGameConfig();

    if (m_profiles->isEmpty() || currentProfile().id != "morrowind") {
        QMessageBox::information(this, T("openmw_inspect_title"),
            T("openmw_inspect_not_morrowind"));
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

    // modlist-sync-guard: given modlist_morrowind.txt is often committed
    // to git and synced between machines, flag every modPath that isn't
    // under the configured mods directory.  Those paths will resolve to
    // nothing on the peer host and silently drop out of openmw.cfg.
    // m_modsDir is the sole canonical root for now - can be expanded to
    // a user-configured list later without touching the pure helper.
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

    QDialog dlg(this);
    dlg.setWindowTitle(T("openmw_inspect_title"));
    dlg.setMinimumSize(720, 520);
    auto *v = new QVBoxLayout(&dlg);

    auto *sumLbl = new QLabel(summary, &dlg);
    sumLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sumLbl->setStyleSheet(
        "background: #f4f1ee; padding: 8px; border-radius: 4px; "
        "font-family: monospace;");
    v->addWidget(sumLbl);

    auto *body = new QPlainTextEdit(&dlg);
    body->setReadOnly(true);
    body->setLineWrapMode(QPlainTextEdit::NoWrap);
    body->setFont(QFont("monospace"));
    body->setPlainText(report);
    v->addWidget(body, 1);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    v->addWidget(btns);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
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
// The scan runs on a worker thread (QtConcurrent::run) because a heavily
// modded Morrowind setup can easily have 100k+ loose files + a dozen
// multi-GB BSAs.  A QProgressDialog keeps the UI responsive and gives the
// user a Cancel button; an atomic<bool> is polled inside the scan loop so
// cancellation is prompt.
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

    // --- Phase 2: run scan off-thread, modal progress dialog with cancel -----
    auto cancel = std::make_shared<std::atomic<bool>>(false);

    QProgressDialog progress(T("conflict_inspector_scanning"),
                             T("conflict_inspector_cancel"),
                             0, 0, this);
    progress.setWindowTitle(T("conflict_inspector_title"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(150);   // tiny scans skip the dialog entirely
    progress.setAutoClose(false);
    progress.setAutoReset(false);

    QFutureWatcher<ConflictMap> watcher;
    QObject::connect(&watcher, &QFutureWatcherBase::finished,
                     &progress, &QProgressDialog::accept);
    QObject::connect(&progress, &QProgressDialog::canceled, this,
                     [cancel]{ cancel->store(true); });

    watcher.setFuture(QtConcurrent::run(scanConflicts, snapshot, cancel));
    progress.exec();
    watcher.waitForFinished();   // ensure the worker thread has actually stopped

    if (cancel->load()) return;

    const ConflictMap conflicts = watcher.result();

    QSet<QString> modsInConflicts;
    for (auto it = conflicts.constBegin(); it != conflicts.constEnd(); ++it)
        for (const auto &p : it.value()) modsInConflicts.insert(p.mod);

    QDialog dlg(this);
    dlg.setWindowTitle(T("conflict_inspector_title"));
    dlg.setMinimumSize(820, 560);
    auto *v = new QVBoxLayout(&dlg);

    auto *explainLbl = new QLabel(T("conflict_inspector_explain"), &dlg);
    explainLbl->setWordWrap(true);
    explainLbl->setStyleSheet("color: #444; padding: 4px 2px;");
    v->addWidget(explainLbl);

    if (conflicts.isEmpty()) {
        auto *none = new QLabel(T("conflict_inspector_none"), &dlg);
        none->setStyleSheet("padding: 16px; font-style: italic;");
        v->addWidget(none);
    } else {
        auto *counts = new QLabel(
            T("conflict_inspector_counts")
                .arg(conflicts.size()).arg(modsInConflicts.size()),
            &dlg);
        counts->setStyleSheet("font-weight: bold; padding: 2px;");
        v->addWidget(counts);

        auto *filter = new QLineEdit(&dlg);
        filter->setPlaceholderText(T("conflict_inspector_filter"));
        filter->setClearButtonEnabled(true);
        v->addWidget(filter);

        auto *tree = new QTreeWidget(&dlg);
        tree->setHeaderLabels({"File / Mod", "Data folder"});
        tree->setAlternatingRowColors(true);
        tree->setRootIsDecorated(true);

        // QMap iterates sorted by key, which gives alphabetical relPath order.
        for (auto it = conflicts.constBegin(); it != conflicts.constEnd(); ++it) {
            const auto &providers = it.value();
            auto *top = new QTreeWidgetItem(tree,
                {it.key(), QString("%1 providers").arg(providers.size())});
            for (int p = 0; p < providers.size(); ++p) {
                const bool isWinner = (p == providers.size() - 1);
                QString label = providers[p].mod;
                if (!providers[p].sourceBsa.isEmpty())
                    label += T("conflict_inspector_bsa_marker")
                                 .arg(providers[p].sourceBsa);
                if (isWinner) label += T("conflict_inspector_winner_marker");
                auto *child = new QTreeWidgetItem(top,
                    {label, providers[p].root});
                if (isWinner) {
                    QFont f = child->font(0); f.setBold(true);
                    child->setFont(0, f);
                } else {
                    child->setForeground(0, QBrush(QColor(150, 150, 150)));
                }
            }
        }
        tree->resizeColumnToContents(0);
        v->addWidget(tree, 1);

        connect(filter, &QLineEdit::textChanged, &dlg,
                [tree](const QString &q) {
            const QString needle = q.trimmed().toLower();
            for (int i = 0; i < tree->topLevelItemCount(); ++i) {
                auto *item = tree->topLevelItem(i);
                bool match = needle.isEmpty() ||
                             item->text(0).contains(needle);
                if (!match) {
                    for (int c = 0; c < item->childCount(); ++c) {
                        if (item->child(c)->text(0).toLower().contains(needle)) {
                            match = true; break;
                        }
                    }
                }
                item->setHidden(!match);
            }
        });
    }

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    v->addWidget(btns);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

// OpenMW Log Triage - parse ~/.config/openmw/openmw.log and surface the
// recognisable error shapes grouped by the mod that owns the offending
// plugin.  Mirrors what the user would do by hand: read the log, spot an
// "asks for parent file" or "Failed loading X.esp" line, then chase X.esp
// back to the mod in the modlist.  The pure triage lives in log_triage.cpp
// so this slot stays a thin shell: read file, build TriageMod list, render.
void MainWindow::onTriageOpenMWLog()
{
    const QString logPath = QDir::homePath() + "/.config/openmw/openmw.log";
    QFile lf(logPath);
    if (!lf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::information(this, T("log_triage_title"),
            T("log_triage_no_log").arg(logPath));
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

    // -- Group issues by suspect mod for the display ---
    //
    // Mods that show up in the log get a named group; issues with no
    // resolved suspect land under a single "Unattributed" bucket so the
    // user still sees them.  Within a group we list MissingMaster first
    // (the hardest crashes), then MissingPlugin, then MissingAsset, then
    // OtherError - roughly "most actionable first".
    auto kindRank = [](openmw::LogIssueKind k) {
        switch (k) {
            case openmw::LogIssueKind::MissingMaster:  return 0;
            case openmw::LogIssueKind::MissingPlugin:  return 1;
            case openmw::LogIssueKind::MissingAsset:   return 2;
            case openmw::LogIssueKind::OtherError:     return 3;
        }
        return 4;
    };
    auto kindLabel = [this](openmw::LogIssueKind k) {
        switch (k) {
            case openmw::LogIssueKind::MissingMaster:  return T("log_triage_kind_master");
            case openmw::LogIssueKind::MissingPlugin:  return T("log_triage_kind_plugin");
            case openmw::LogIssueKind::MissingAsset:   return T("log_triage_kind_asset");
            case openmw::LogIssueKind::OtherError:     return T("log_triage_kind_other");
        }
        return QString();
    };

    // QMap<mod display name or unattributed bucket, issues in sort order>.
    const QString unattributed = T("log_triage_unattributed");
    QMap<QString, QList<openmw::LogIssue>> grouped;
    for (const openmw::LogIssue &i : report.issues) {
        const QString key = i.suspectMod.isEmpty() ? unattributed : i.suspectMod;
        grouped[key].append(i);
    }
    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        std::sort(it.value().begin(), it.value().end(),
                  [&](const openmw::LogIssue &a, const openmw::LogIssue &b) {
            const int ra = kindRank(a.kind);
            const int rb = kindRank(b.kind);
            if (ra != rb) return ra < rb;
            return a.target.compare(b.target, Qt::CaseInsensitive) < 0;
        });
    }

    // -- Render ---
    QString summary;
    summary += QString("%1: %2\n").arg(T("log_triage_log_path"), logPath);
    summary += T("log_triage_summary_counts")
               .arg(report.errorLines)
               .arg(report.issues.size())
               .arg(grouped.size()) + "\n";

    QString body;
    if (report.issues.isEmpty()) {
        body = T("log_triage_none");
    } else {
        for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
            const QString &mod = it.key();
            body += QString("\n=== %1 ===\n").arg(mod);
            for (const openmw::LogIssue &i : it.value()) {
                body += "  [" + kindLabel(i.kind) + "] " + i.target;
                if (!i.parent.isEmpty())
                    body += " → " + T("log_triage_needs") + " " + i.parent;
                body += "\n";
                body += "      " + i.detail.trimmed() + "\n";
            }
        }
    }

    QDialog dlg(this);
    dlg.setWindowTitle(T("log_triage_title"));
    dlg.setMinimumSize(820, 560);
    auto *v = new QVBoxLayout(&dlg);

    auto *sumLbl = new QLabel(summary, &dlg);
    sumLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sumLbl->setStyleSheet(
        "background: #f4f1ee; padding: 8px; border-radius: 4px; "
        "font-family: monospace;");
    v->addWidget(sumLbl);

    auto *bodyEdit = new QPlainTextEdit(&dlg);
    bodyEdit->setReadOnly(true);
    bodyEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    bodyEdit->setFont(QFont("monospace"));
    bodyEdit->setPlainText(body);
    v->addWidget(bodyEdit, 1);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    v->addWidget(btns);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}

void MainWindow::onEditLoadOrder()
{
    reconcileLoadOrder(); // make sure the list reflects what's actually installed

    if (m_loadOrder.isEmpty()) {
        QMessageBox::information(this, T("loadorder_title"),
            T("loadorder_empty"));
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

    m_undoStack->pushUndo();

    // Detach all items, then re-add in the new order. Pointers stay valid.
    while (m_modList->count() > 0) m_modList->takeItem(0);
    for (auto *it : newOrder) m_modList->addItem(it);

    saveModList();
    updateSectionCounts();
    scheduleConflictScan();
}

void MainWindow::onSortBySize()
{
    if (sender() == m_sizeSortBtn)
        m_sizeSortAsc = !m_sizeSortAsc;
    m_sizeSortBtn->setText(m_sizeSortAsc ? T("col_size_asc") : T("col_size_desc"));

    modlist_sort::bySize(m_modList, m_sizeSortAsc);

    saveModList();
    updateSectionCounts();
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
        for (const auto &p : collectDataFolders(e.modPath, contentExts)) {
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

    const QString cfgPath = QDir::homePath() + "/.config/openmw/openmw.cfg";
    const QStringList contentExts{".esp", ".esm", ".omwaddon", ".omwscripts"};

    // Build the pure ConfigMod list from modlist UI state + filesystem probes.
    // The writer itself has no Qt-widget / I/O dependencies, so this loop is
    // the only place that needs to talk to m_modList and the disk.
    QList<openmw::ConfigMod> mods;
    mods.reserve(m_modList->count());
    // Canonical mod roots for every mod still present in the list.  Used below
    // to recognise orphan data= lines - paths that still sit under m_modsDir
    // but no longer correspond to a managed mod (typically because the user
    // removed the mod from the list, or the launcher promoted an upgraded
    // mod's pre-upgrade data= line into the preamble).
    QSet<QString> managedModPaths;
    // Display names of all currently-installed mods.  Used by the patch-skip
    // filter a few lines below so that FOMOD-style "01 X for Y" subfolders
    // (e.g. Ashfront's "01 Grass for Remiros' Groundcover") get dropped when
    // their Y mod isn't in the list - OpenMW would otherwise warn / crash
    // loading plugins that reference an absent companion mod.
    QStringList installedModNames;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 1) continue;
        const QString n = item->text();
        if (!n.isEmpty()) installedModNames << n;
    }
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
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;

        openmw::ConfigMod cm;
        cm.enabled   = (item->checkState() == Qt::Checked);
        cm.installed = (item->data(ModRole::InstallStatus).toInt() == 1);

        if (cm.installed) {
            const QString modPath = item->data(ModRole::ModPath).toString();
            if (!modPath.isEmpty())
                managedModPaths.insert(QDir::cleanPath(modPath));
            cm.pluginDirs = collectDataFolders(modPath, contentExts);
            // Drop patch subfolders whose target mod isn't present in the list,
            // and those the user explicitly declined when prompted at mod-add
            // time.  If the target mod is later installed, addModFromPath
            // surfaces the skipped patches in a prompt - accepting clears the
            // declined flag and the next sync reinstates the data= line.
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
                // "grass"/"groundcover" - mirrors the addModFromPath
                // helper list so an unprompted sync still emits the
                // right section.
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

    // Read existing cfg (empty string if absent) - parsing lives in the writer.
    QString existing;
    {
        QFile f(cfgPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            existing = QString::fromUtf8(f.readAll());
    }

    // -- Orphan-managed rescue ---
    //
    // When the modlist is replaced (Import) or a mod is removed without
    // deleting its folder from disk, openmw.cfg can still carry data=
    // lines inside the managed section that no current managed mod
    // claims. The orphan scrub below would drop those lines and the
    // content= scrub would drop the corresponding plugins, wiping the
    // OpenMW Launcher's Content List of mods whose files are still
    // physically present.
    //
    // Rescue any orphan-managed path whose folder still has plugins on
    // disk by re-emitting it OUTSIDE the managed section. The writer
    // discards data= inside BEGIN/END unconditionally (Pass 2), but
    // preserves data= outside as externalDataLines, and the content=
    // entries that those plugins back up survive Pass 3 because
    // allManagedContent doesn't claim them. Folders that no longer
    // exist (or contain no plugins) still drop normally.
    const QString modsRootForRescue = m_modsDir.isEmpty()
        ? QString() : QDir::cleanPath(m_modsDir);
    QStringList rescuedDataLines;
    QSet<QString> rescuedManagedPaths;
    if (!modsRootForRescue.isEmpty()) {
        static const QString kBegin = "# --- Nerevarine Organizer BEGIN ---";
        static const QString kEnd   = "# --- Nerevarine Organizer END ---";
        bool inManaged = false;
        QStringList filters;
        for (const QString &ext : contentExts) filters << "*" + ext;
        for (const QString &raw : existing.split('\n')) {
            QString line = raw;
            if (line.endsWith('\r')) line.chop(1);
            if (line == kBegin) { inManaged = true;  continue; }
            if (line == kEnd)   { inManaged = false; continue; }
            if (!inManaged) continue;
            if (!line.startsWith(QStringLiteral("data="))) continue;
            QString path = line.mid(5);
            if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"'))
                path = path.mid(1, path.size() - 2);
            const QString clean = QDir::cleanPath(path);
            if (clean != modsRootForRescue
                && !clean.startsWith(modsRootForRescue + "/")) continue;
            bool claimed = false;
            for (const QString &mp : managedModPaths) {
                if (clean == mp || clean.startsWith(mp + "/")) {
                    claimed = true;
                    break;
                }
            }
            if (claimed) continue;
            QDir d(path);
            if (!d.exists()) continue;
            if (d.entryList(filters, QDir::Files).isEmpty()) continue;
            rescuedDataLines << QStringLiteral("data=\"") + path
                              + QStringLiteral("\"");
            rescuedManagedPaths.insert(clean);
        }
    }
    if (!rescuedDataLines.isEmpty())
        existing = rescuedDataLines.join('\n') + '\n' + existing;

    // -- Launcher-only externals augmentation ---
    //
    // OpenMW Launcher writes the user's selected data= paths and content=
    // entries into BOTH openmw.cfg and launcher.cfg, but only when the user
    // actually opens the launcher and clicks Save/Play.  A user who set up
    // their game with `openmw-launcher` and then never re-opened it can
    // legitimately end up with vanilla "data=<Morrowind Data Files>" plus
    // "content=Morrowind.esm" present in launcher.cfg but absent from the
    // local openmw.cfg (the global /etc/openmw/openmw.cfg covers OpenMW's
    // own load path, but Nerevarine only reads the per-user file).
    //
    // Without this augmentation the orphan scrub a few lines down would see
    // no data= dir providing Morrowind.esm, drop content=Morrowind.esm, and
    // the launcher.cfg sync at the end of this function would propagate
    // that "drop" back to launcher.cfg - wiping the user's vanilla Content
    // List the next time the OpenMW Launcher is opened.
    //
    // Treat anything in launcher.cfg's current profile that ISN'T already
    // in openmw.cfg AND isn't under our managed mods root as legitimate
    // external state and synthesise it as if it had been written outside
    // the BEGIN/END markers.
    {
        const QString launcherPath =
            QDir::homePath() + "/.config/openmw/launcher.cfg";
        QFile lf(launcherPath);
        if (lf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString launcherText = QString::fromUtf8(lf.readAll());
            lf.close();

            const QStringList lcDataPaths   =
                openmw::readLauncherCfgDataPaths(launcherText);
            const QStringList lcContentFiles =
                openmw::readLauncherCfgContentOrder(launcherText);

            // Index what's already represented in openmw.cfg so we don't
            // emit duplicates that would compound on every sync.
            QSet<QString> existingDataPaths;
            QSet<QString> existingContent;
            for (const QString &raw : existing.split('\n')) {
                QString l = raw;
                if (l.endsWith('\r')) l.chop(1);
                if (l.startsWith(QStringLiteral("data="))) {
                    QString p = l.mid(5);
                    if (p.size() >= 2 && p.startsWith('"') && p.endsWith('"'))
                        p = p.mid(1, p.size() - 2);
                    existingDataPaths.insert(QDir::cleanPath(p));
                } else if (l.startsWith(QStringLiteral("content="))) {
                    existingContent.insert(l.mid(8));
                }
            }

            // Filenames any managed mod knows about (regardless of enabled
            // state).  Disabled-mod plugins still belong to Nerevarine and
            // must NOT be carried back as fake externals - that would make
            // disabling a mod inside Nerevarine fail to remove its content=
            // line from the launcher's Selected list.
            QSet<QString> allManagedFilenames;
            for (const auto &cm : mods)
                for (const auto &p : cm.pluginDirs)
                    for (const QString &cf : p.second)
                        allManagedFilenames.insert(cf);

            const QString cleanModsRoot = m_modsDir.isEmpty()
                ? QString() : QDir::cleanPath(m_modsDir);

            QStringList synth;
            for (const QString &p : lcDataPaths) {
                const QString clean = QDir::cleanPath(p);
                if (existingDataPaths.contains(clean)) continue;
                // Paths under our mods dir are Nerevarine-managed; the
                // writer rebuilds them from the modlist.
                if (!cleanModsRoot.isEmpty() &&
                    (clean == cleanModsRoot ||
                     clean.startsWith(cleanModsRoot + "/")))
                    continue;
                synth << QStringLiteral("data=\"") + p + QStringLiteral("\"");
            }
            for (const QString &cf : lcContentFiles) {
                if (existingContent.contains(cf)) continue;
                if (allManagedFilenames.contains(cf)) continue;
                synth << QStringLiteral("content=") + cf;
            }

            if (!synth.isEmpty()) {
                // Prepend so the synthetic lines land outside the managed
                // section (they sit before any BEGIN/END markers parsed by
                // the writer).  Trailing newline ensures the first existing
                // line keeps its place.
                existing = synth.join('\n') + '\n' + existing;
            }
        }
    }

    // -- Orphan-plugin scrub ---
    //
    // Before handing off to the writer, drop `content=` lines that point at
    // plugins no data= directory provides.  The OpenMW Launcher lets users
    // tick any plugin visible in its Data Files tab; if they tick one whose
    // providing mod isn't in Nerevarine's modlist (common when a mod is
    // downloaded but never added, or after an uninstall that left stragglers
    // in the cfg), the resulting `content=X.esp` has no matching `data=`
    // path.  OpenMW aborts at launch with:
    //     Fatal error: Failed loading X.esp: the content file does not exist
    //
    // The writer can't filter these out on its own - Phase A treats any
    // non-managed content= entry as an external base-game plugin and keeps
    // it.  So we pre-scrub here, where filesystem scans are fair game, and
    // also prune m_loadOrder of the same orphans so they don't come back on
    // the next absorb cycle.
    QSet<QString> providedPlugins;
    for (const auto &cm : mods) {
        for (const auto &p : cm.pluginDirs)
            for (const QString &cf : p.second) providedPlugins.insert(cf);
    }
    // data= line refers to a path under m_modsDir that no longer belongs to any
    // mod in the list.  Covers two compounding cases:
    //   1. User removed a mod (e.g. Remiros Groundcover) - its data= line can
    //      linger if the OpenMW Launcher rewrote openmw.cfg between the last
    //      sync and the remove, promoting the data= line out of the managed
    //      section where Pass 2 of the writer would otherwise discard it.
    //   2. User upgraded a mod to a new timestamp-suffixed folder - the old
    //      pre-upgrade path gets promoted into the preamble the same way and
    //      no longer matches any mod's ModPath.
    // Either way the right thing is to drop the line entirely so the writer
    // can rebuild authoritative data= lines from the current modlist.
    const QString modsRoot = modsRootForRescue;
    auto isOrphanedManagedPath = [&](const QString &rawPath) -> bool {
        if (modsRoot.isEmpty()) return false;
        const QString p = QDir::cleanPath(rawPath);
        if (p != modsRoot && !p.startsWith(modsRoot + "/")) return false;
        // Rescued in the block above - keep so the launcher still sees the
        // mod's content= entries instead of silently dropping them.
        if (rescuedManagedPaths.contains(p)) return false;
        for (const QString &mp : managedModPaths)
            if (p == mp || p.startsWith(mp + "/")) return false;
        return true;
    };
    // Scan each external data= path (base-game install, outside our managed
    // section) for plugin files - those are legitimately external and must
    // stay.  Everything else referenced as content= is an orphan.
    {
        static const QString kBegin = "# --- Nerevarine Organizer BEGIN ---";
        static const QString kEnd   = "# --- Nerevarine Organizer END ---";
        bool inManaged = false;
        for (QString line : existing.split('\n')) {
            if (line.endsWith('\r')) line.chop(1);
            if (line == kBegin) { inManaged = true;  continue; }
            if (line == kEnd)   { inManaged = false; continue; }
            if (inManaged)        continue;
            if (!line.startsWith(QStringLiteral("data="))) continue;
            QString path = line.mid(5);
            if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"'))
                path = path.mid(1, path.size() - 2);
            if (isOrphanedManagedPath(path)) continue;
            QDir d(path);
            if (!d.exists()) continue;
            QStringList filters;
            for (const QString &ext : contentExts) filters << "*" + ext;
            for (const QString &f : d.entryList(filters, QDir::Files))
                providedPlugins.insert(f);
        }
    }

    // Filter plugins whose TES3 masters aren't satisfied.
    //
    // content= plugins: OpenMW aborts at launch with "File X asks for parent
    // file Y, but it is not available or has been loaded in the wrong order"
    // - common trigger is a mod that ships optional patch ESPs for companion
    // mods the user didn't install (e.g. Hlaalu Seyda Neen bundles
    // HlaaluSeydaNeen_AFFresh_Patch.ESP which needs AFFresh.esm).
    //
    // groundcover= plugins: same crash, no graceful fallback at all.
    //
    // Detection logic lives in openmw::findUnsatisfiedMasters so it is pure
    // and testable against real TES3 fixtures; this block just collects the
    // candidate plugins, calls the helper, and routes the result back into
    // each mod's suppressedPlugins.
    {
        static const QSet<QString> baseMasters = {
            "morrowind.esm", "tribunal.esm", "bloodmoon.esm"
        };
        QList<openmw::PluginRef> candidates;
        QSet<QString>            availableLower;
        for (const QString &cf : providedPlugins) availableLower.insert(cf.toLower());
        for (const QString &bm : baseMasters)     availableLower.insert(bm);

        for (const auto &cm : mods) {
            if (!cm.enabled || !cm.installed) continue;
            for (const auto &p : cm.pluginDirs)
                for (const QString &cf : p.second) {
                    if (cm.suppressedPlugins.contains(cf)) continue;
                    candidates.append({cf, QDir(p.first).filePath(cf)});
                }
        }

        const QSet<QString> unsatisfied =
            openmw::findUnsatisfiedMasters(candidates, availableLower);

        if (!unsatisfied.isEmpty()) {
            for (auto &cm : mods) {
                QSet<QString> hits;
                for (const auto &p : cm.pluginDirs)
                    for (const QString &cf : p.second)
                        if (unsatisfied.contains(cf)) hits.insert(cf);
                if (hits.isEmpty()) continue;
                cm.groundcoverFiles  -= hits;
                cm.suppressedPlugins += hits;
            }
        }
    }

    // Collect groundcover and suppressed filenames so we can exclude them
    // from the load order (otherwise absorbExternalLoadOrder hangs onto
    // stale entries, and the launcher re-displays the bad patch on every
    // run) and scrub orphan groundcover= lines the same way we scrub content=.
    QSet<QString> groundcoverPlugins;
    QSet<QString> suppressedFromLoadOrder;
    for (const auto &cm : mods) {
        for (const QString &gf : cm.groundcoverFiles)
            groundcoverPlugins.insert(gf);
        for (const QString &sp : cm.suppressedPlugins)
            suppressedFromLoadOrder.insert(sp);
    }

    QStringList scrubbedLines;
    QStringList droppedOrphans;
    for (QString line : existing.split('\n')) {
        QString probe = line;
        if (probe.endsWith('\r')) probe.chop(1);
        if (probe.startsWith(QStringLiteral("data="))) {
            QString path = probe.mid(5);
            if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"'))
                path = path.mid(1, path.size() - 2);
            if (isOrphanedManagedPath(path)) continue;
        }
        if (probe.startsWith(QStringLiteral("content="))) {
            QString cf = probe.mid(8);
            if (!providedPlugins.contains(cf)) {
                droppedOrphans << cf;
                continue;
            }
        }
        if (probe.startsWith(QStringLiteral("groundcover="))) {
            QString cf = probe.mid(12);
            if (!providedPlugins.contains(cf)) continue;
        }
        scrubbedLines << line;
    }
    const QString scrubbedExisting = scrubbedLines.join('\n');

    // Groundcover plugins must not appear in the load order - OpenMW loads
    // them via the separate groundcover= mechanism.  Suppressed plugins
    // (missing masters) are dropped too: keeping them here means the next
    // absorb cycle resurrects them and the user keeps hitting the same
    // "asks for parent file X" crash.
    QStringList effectiveLoadOrder;
    effectiveLoadOrder.reserve(m_loadOrder.size());
    for (const QString &cf : m_loadOrder)
        if (providedPlugins.contains(cf)
         && !groundcoverPlugins.contains(cf)
         && !suppressedFromLoadOrder.contains(cf))
            effectiveLoadOrder << cf;
    if (effectiveLoadOrder != m_loadOrder) {
        m_loadOrder = effectiveLoadOrder;
        saveLoadOrder();
    }

    if (!droppedOrphans.isEmpty()) {
        statusBar()->showMessage(
            T("status_orphan_content_dropped").arg(droppedOrphans.size()),
            6000);
    }

    const QString rendered =
        openmw::renderOpenMWConfig(mods, m_loadOrder, scrubbedExisting);

    // README disclaims file corruption - snapshot first, write second.
    (void)safefs::snapshotBackup(cfgPath);

    QFile f(cfgPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    f.write(rendered.toUtf8());
    f.close();

    // -- launcher.cfg sync ---
    //
    // The OpenMW Launcher keeps its own per-profile cache of data= and
    // content= lines in ~/.config/openmw/launcher.cfg, and its Data Files
    // tab renders from that cache, not from openmw.cfg.  So after an
    // uninstall/remove the mod vanishes from openmw.cfg but the launcher
    // still shows it, and the user can re-tick a plugin that has no
    // provider - instant "content file does not exist" crash.
    //
    // Keep only the CURRENT profile's data=/content= in lockstep with the
    // openmw.cfg we just wrote.  Other profiles are left untouched so the
    // user can still switch between saved load-outs by hand.
    {
        const QString launcherPath =
            QDir::homePath() + "/.config/openmw/launcher.cfg";

        // Mtime gate: if launcher.cfg is newer than the openmw.cfg we
        // just wrote, the user has made changes in the launcher's Data
        // Files tab that haven't been absorbed into m_loadOrder yet.
        // Writing here would clobber the reorder. absorbExternalLoadOrder
        // picks the signal up on the next saveModList cycle.
        const QFileInfo launcherInfo(launcherPath);
        const QFileInfo newCfgInfo(cfgPath);
        if (launcherInfo.exists() && newCfgInfo.exists()
            && launcherInfo.lastModified() > newCfgInfo.lastModified()) {
            return;
        }

        QFile lf(launcherPath);
        if (lf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString before = QString::fromUtf8(lf.readAll());
            lf.close();

            // Extract unquoted data= paths and content= filenames in the
            // same order they appear in the rendered openmw.cfg - the
            // launcher expects load-priority order for data= and activation
            // order for content=, and that's what renderOpenMWConfig emits.
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

            const QString after =
                openmw::renderLauncherCfg(before, lDataPaths, lContent);
            if (!after.isEmpty() && after != before) {
                (void)safefs::snapshotBackup(launcherPath);
                QFile wf(launcherPath);
                if (wf.open(QIODevice::WriteOnly | QIODevice::Text))
                    wf.write(after.toUtf8());
            }
        }
    }
}

void MainWindow::loadModList(const QString &path,
                             const QString &remapFrom,
                             const QString &remapTo)
{
    QFile f(path.isEmpty() ? modlistPath() : path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    m_modList->clear();

    // Each of these matches anywhere in the line - we pick the LAST
    // occurrence per kind so that a previously-corrupted separator
    // (earlier build's `</fgcolor>$` anchored regex missed `<collapsed>`
    // and wrapped a second set of tags around the first on next save)
    // self-heals to the user's most recent colours on load.
    static const QRegularExpression colorRe(
        R"(<color>(#[0-9a-fA-F]+)</color>)");
    static const QRegularExpression fgColorRe(
        R"(<fgcolor>(#[0-9a-fA-F]+)</fgcolor>)");
    static const QRegularExpression stripTagsRe(
        R"(<color>#[0-9a-fA-F]+</color>|<fgcolor>#[0-9a-fA-F]+</fgcolor>|<collapsed>\d+</collapsed>)");

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.isEmpty()) continue;

        if (line.startsWith("# ")) {
            QString rest = line.mid(2);
            QColor bg(55, 55, 75);
            QColor fg(Qt::white);

            {
                auto it = colorRe.globalMatch(rest);
                QRegularExpressionMatch last;
                while (it.hasNext()) last = it.next();
                if (last.hasMatch()) bg = QColor(last.captured(1));
            }
            {
                auto it = fgColorRe.globalMatch(rest);
                QRegularExpressionMatch last;
                while (it.hasNext()) last = it.next();
                if (last.hasMatch()) fg = QColor(last.captured(1));
            }
            const bool collapsed = rest.contains(QStringLiteral("<collapsed>1</collapsed>"));

            QString name = rest;
            name.remove(stripTagsRe);
            name = name.trimmed();

            auto *item = new QListWidgetItem(name);
            item->setData(ModRole::ItemType,  ItemType::Separator);
            item->setData(ModRole::BgColor,   bg);
            item->setData(ModRole::FgColor,   fg);
            item->setData(ModRole::Collapsed, collapsed);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
            m_modList->addItem(item);

        } else if (line.size() >= 2 && (line[0] == '+' || line[0] == '-') && line[1] == ' ') {
            bool enabled = (line[0] == '+');
            QStringList parts    = line.mid(2).split('\t');
            QString modPath      = parts[0];
            if (!remapFrom.isEmpty() && !remapTo.isEmpty()
                && modPath.startsWith(remapFrom))
                modPath = remapTo + modPath.mid(remapFrom.size());
            QString custName     = parts.size() > 1 ? parts[1] : QString();
            QString annot        = parts.size() > 2 ? decodeAnnot(parts[2]) : QString();
            QString url          = parts.size() > 3 ? parts[3] : QString();
            QDateTime dateAdded  = parts.size() > 4
                                   ? QDateTime::fromString(parts[4], Qt::ISODate)
                                   : QDateTime();
            // parts[5] was once the endorsement state (0/+1/-1) - no longer tracked.
            // parts[6] is the comma-separated list of Nexus URLs this mod depends on.
            QStringList deps = parts.size() > 6
                               ? parts[6].split(',', Qt::SkipEmptyParts)
                               : QStringList();
            // parts[7]: cached UpdateAvailable flag so the green update
            // triangle survives app restart.  Missing (older files) → false.
            bool updateAvailable = parts.size() > 7 && parts[7].toInt() == 1;
            // parts[8]: user-set "utility mod" flag.  Missing → false.
            bool isUtility  = parts.size() > 8 && parts[8].toInt() == 1;
            // parts[9]: favourite flag.  Missing (older files) → false.
            bool isFavorite = parts.size() > 9 && parts[9].toInt() == 1;
            // parts[10]: serialized FOMOD install choices.  Missing → empty (not a FOMOD mod,
            // or installed before this feature was added).
            QString fomodChoices = parts.size() > 10 ? parts[10] : QString();
            // parts[11]: user-set video review URL.  Missing → empty.
            QString videoUrl = parts.size() > 11 ? parts[11] : QString();
            // parts[12]: non-Nexus source URL (GitHub release, Nexus search, etc.).  Missing → empty.
            QString sourceUrl = parts.size() > 12 ? parts[12] : QString();

            QFileInfo fi(modPath);
            QString displayName  = custName.isEmpty() ? fi.fileName() : custName;
            auto *item = new QListWidgetItem(displayName);
            item->setData(ModRole::ItemType,      ItemType::Mod);
            item->setData(ModRole::ModPath,       modPath);
            item->setData(ModRole::CustomName,    custName);
            item->setData(ModRole::Annotation,    annot);
            item->setData(ModRole::NexusUrl,      url);
            item->setData(ModRole::DateAdded,     dateAdded);
            if (!deps.isEmpty())
                item->setData(ModRole::DependsOn, deps);
            if (updateAvailable)
                item->setData(ModRole::UpdateAvailable, true);
            if (isUtility)
                item->setData(ModRole::IsUtility, true);
            if (isFavorite)
                item->setData(ModRole::IsFavorite, true);
            if (!fomodChoices.isEmpty())
                item->setData(ModRole::FomodChoices, fomodChoices);
            if (!videoUrl.isEmpty())
                item->setData(ModRole::VideoUrl, videoUrl);
            if (!sourceUrl.isEmpty())
                item->setData(ModRole::SourceUrl, sourceUrl);
            // An empty directory isn't a working install: collectDataFolders
            // will find nothing in it, so masters it's supposed to provide
            // will never be seen by the missing-master scan.  Treat it the
            // same as a missing path so the UI shows "not installed" and the
            // reinstall option is offered rather than silently lying.
            {
                QDir d(modPath);
                bool installed = !modPath.isEmpty()
                                 && d.exists()
                                 && !d.isEmpty();
                item->setData(ModRole::InstallStatus, installed ? 1 : 0);
            }
            item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
            item->setToolTip(annot.isEmpty() ? modPath : modPath + "\n\n" + annot);
            m_modList->addItem(item);
        }
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

void MainWindow::launchProgram(QString &storedPath, const QString &settingsKey,
                                const QString &execName, const QString &locateTitle,
                                bool monitored)
{
    // Auto-detect via PATH if not configured or binary moved
    if (storedPath.isEmpty() || !QFile::exists(storedPath)) {
        storedPath = QStandardPaths::findExecutable(execName);
        if (!storedPath.isEmpty())
            QSettings().setValue(settingsKey, storedPath);
    }

    // Still not found - ask user
    if (storedPath.isEmpty() || !QFile::exists(storedPath)) {
        storedPath = QFileDialog::getOpenFileName(
            this, locateTitle, "/usr/bin");
        if (storedPath.isEmpty()) return;
        QSettings().setValue(settingsKey, storedPath);
    }

    if (monitored) {
        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, this,
                [this, proc](int code, QProcess::ExitStatus) {
            proc->deleteLater();
            if (code != 0)
                QTimer::singleShot(0, this, &MainWindow::onTriageOpenMWLog);
        });
        proc->start(storedPath, {});
        if (!proc->waitForStarted(3000)) {
            proc->deleteLater();
            QMessageBox::warning(this, T("launch_error_title"),
                                 T("launch_error_body").arg(storedPath));
        }
    } else {
        if (!QProcess::startDetached(storedPath, {}))
            QMessageBox::warning(this, T("launch_error_title"),
                                 T("launch_error_body").arg(storedPath));
    }
}

// Pre-launch sanity check.  Aggregates the four warning signals that
// are already maintained per-row and, if any fired, shows a summary
// dialog with a Launch-anyway / Cancel choice.  Returns true when the
// caller should proceed with the launch.
//
// The check is OpenMW-specific (non-Morrowind profiles skip it).  Data
// is read from the roles populated by scanMissingMasters and
// scanMissingDependencies + an on-the-fly `collectDataFolders` call -
// no background work, no fresh scans.  If every bucket is empty the
// dialog isn't shown at all.
bool MainWindow::confirmLaunchIfWarnings()
{
    if (m_suppressLaunchSanityCheck) return true;

    const QString gameId =
        m_profiles->isEmpty() ? QString() : currentProfile().id;
    const auto warnings = launch_warnings::scan(m_modList, m_forbidden, gameId);
    if (warnings.total() == 0) return true;

    const auto choice = launch_warnings::showDialog(this, warnings);
    if (choice.suppress) m_suppressLaunchSanityCheck = true;
    return choice.proceed;
}

bool MainWindow::refuseLaunchIfRebootPending()
{
    return launch_warnings::refuseIfRebootPending(this);
}

void MainWindow::onLaunchOpenMW()
{
    if (refuseLaunchIfRebootPending()) return;
    if (!confirmLaunchIfWarnings()) return;
    launchProgram(m_openmwPath,
                  "games/" + currentProfile().id + "/openmw_path",
                  "openmw", T("launch_locate_openmw"),
                  /*monitored=*/true);
    currentProfile().openmwPath = m_openmwPath;
}

void MainWindow::onLaunchOpenMWLauncher()
{
    if (refuseLaunchIfRebootPending()) return;
    if (!confirmLaunchIfWarnings()) return;
    launchProgram(m_openmwLauncherPath,
                  "games/" + currentProfile().id + "/openmw_launcher_path",
                  "openmw-launcher", T("launch_locate_launcher"));
    currentProfile().openmwLauncherPath = m_openmwLauncherPath;
}

// Launch non-Morrowind games (via Steam URL or auto-detected executable)

// File-local: reads Heroic's installed.json and returns the GOG numeric app-ID
// for the entry whose install_path contains the given hint.
// Returns the Heroic GOG appName for the entry whose install_path is a prefix
// of (or equals) exeOrDirPath.  Handles games whose exe lives in a subdirectory
// (e.g. Cyberpunk 2077's bin/x64/Cyberpunk2077.exe).
static QString heroicGogAppId(const QString &exeOrDirPath)
{
    const QString home = QDir::homePath();
    const QStringList heroicConfigs = {
        home + "/.config/heroic",
        home + "/.var/app/com.heroicgameslauncher.hgl/config/heroic",
    };
    for (const QString &cfg : heroicConfigs) {
        QFile f(cfg + "/gog_store/installed.json");
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        QJsonArray arr;
        if (doc.isArray())
            arr = doc.array();
        else if (doc.isObject() && doc.object().contains("installed"))
            arr = doc.object().value("installed").toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject obj = v.toObject();
            const QString installPath = obj.value("install_path").toString();
            if (installPath.isEmpty()) continue;
            // Match if the exe path starts with the install_path (handles subdirs)
            if (exeOrDirPath.startsWith(installPath, Qt::CaseInsensitive))
                return obj.value("appName").toString();
        }
    }
    return {};
}

// Helper used by both launch functions: tries Heroic URL → Heroic binary →
// Heroic Flatpak → direct exe, in that order.  Returns true if anything started.
static bool launchViaGog(const QString &gogExe)
{
    if (gogExe.isEmpty() || !QFile::exists(gogExe)) return false;

    // Ask Heroic to handle it so Wine/Proton and runtime env are set up correctly
    // Pass the full exe path - heroicGogAppId matches entries whose install_path
    // is a prefix of the exe path, so subdir exes (e.g. bin/x64/...) work too.
    const QString appId = heroicGogAppId(gogExe);
    if (!appId.isEmpty()) {
        const QString url = "heroic://launch/gog/" + appId;
        if (QProcess::startDetached("xdg-open",  {url}))              return true;
        if (QProcess::startDetached("heroic",     {"launch", appId}))  return true;
        if (QProcess::startDetached("flatpak",    {"run",
                "com.heroicgameslauncher.hgl", "launch", appId}))      return true;
    }
    // Fallback: run the exe directly (works for native Linux builds and setups
    // where Wine/Proton is already in the environment - e.g. Lutris, Bottles)
    return QProcess::startDetached(gogExe, {});
}

void MainWindow::onLaunchSteamLauncher()
{
    const QString &id    = currentProfile().id;
    const QString appId  = GameProfileRegistry::steamAppId(id);

    // -- 1. GOG / Heroic - always wins when the game is present there ---
    const QString gogExe = GameProfileRegistry::findGogGameExe(id, /*wantLauncher=*/true);
    if (!gogExe.isEmpty() && QFile::exists(gogExe)) {
        if (!launchViaGog(gogExe))
            QMessageBox::warning(this, T("launch_error_title"),
                                 T("launch_error_body").arg(gogExe));
        return;
    }

    // -- 2. Steam launcher exe (SkyrimSELauncher.exe, Fallout4Launcher.exe…) --
    const QString launcherPath = GameProfileRegistry::findSteamLauncherExe(id);
    if (!launcherPath.isEmpty() && QFile::exists(launcherPath)) {
        if (!QProcess::startDetached(launcherPath, {}))
            QMessageBox::warning(this, T("launch_error_title"),
                                 T("launch_error_body").arg(launcherPath));
        return;
    }

    // -- 3. Steam URL (launcher exe not in known paths) ---
    const QString steamExe = GameProfileRegistry::findSteamGameExe(id);
    if (!appId.isEmpty() && !steamExe.isEmpty() && QFile::exists(steamExe)) {
        if (!QProcess::startDetached("xdg-open", {"steam://launch/" + appId}))
            QProcess::startDetached("steam",     {"steam://launch/" + appId});
        return;
    }

    // -- 4. Steam URL last resort ---
    if (!appId.isEmpty()) {
        if (!QProcess::startDetached("xdg-open", {"steam://launch/" + appId}))
            QProcess::startDetached("steam",     {"steam://launch/" + appId});
        return;
    }

    // -- 5. Ask user ---
    QString path = QFileDialog::getOpenFileName(
        this, T("launch_locate_game").arg(currentProfile().displayName),
        QDir::homePath());
    if (path.isEmpty()) return;
    QSettings().setValue("games/" + id + "/launcher_exe_path", path);
    if (!QProcess::startDetached(path, {}))
        QMessageBox::warning(this, T("launch_error_title"),
                             T("launch_error_body").arg(path));
}

void MainWindow::onLaunchGame()
{
    const QString &id   = currentProfile().id;
    const QString appId = GameProfileRegistry::steamAppId(id);

    // -- 1. GOG / Heroic - always wins when the game is present there ---
    const QString gogExe = GameProfileRegistry::findGogGameExe(id);
    if (!gogExe.isEmpty() && QFile::exists(gogExe)) {
        if (!launchViaGog(gogExe))
            QMessageBox::warning(this, T("launch_error_title"),
                                 T("launch_error_body").arg(gogExe));
        return;
    }

    // -- 2. Steam - confirmed installed (exe found in Steam library) ---
    const QString steamExe = GameProfileRegistry::findSteamGameExe(id);
    if (!appId.isEmpty() && !steamExe.isEmpty() && QFile::exists(steamExe)) {
        if (!QProcess::startDetached("xdg-open", {"steam://rungameid/" + appId}))
            QProcess::startDetached("steam",     {"steam://rungameid/" + appId});
        return;
    }

    // -- 3. Steam URL last resort (non-standard library path) ---
    if (!appId.isEmpty()) {
        if (!QProcess::startDetached("xdg-open", {"steam://rungameid/" + appId}))
            QProcess::startDetached("steam",     {"steam://rungameid/" + appId});
        return;
    }

    // -- 4. Ask user ---
    QString exePath = QFileDialog::getOpenFileName(
        this, T("launch_locate_game").arg(currentProfile().displayName),
        QDir::homePath());
    if (exePath.isEmpty()) return;
    QSettings().setValue("games/" + id + "/exe_path", exePath);
    if (!QProcess::startDetached(exePath, {}))
        QMessageBox::warning(this, T("launch_error_title"),
                             T("launch_error_body").arg(exePath));
}

void MainWindow::exportModList()
{
    QString path = QFileDialog::getSaveFileName(
        this, T("export_dialog_title"),
        QDir::homePath() + "/modlist_export.txt",
        "Mod List (*.txt);;All Files (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        statusBar()->showMessage(T("status_export_failed"), 3000);
        return;
    }

    int modCount = 0, sepCount = 0;
    for (int i = 0; i < m_modList->count(); ++i) {
        if (m_modList->item(i)->data(ModRole::ItemType).toString() == ItemType::Separator)
            ++sepCount;
        else
            ++modCount;
    }

    QTextStream out(&f);
    out << "## Nerevarine Organizer Modlist\n";
    out << "## Game: OpenMW\n";
    out << "## Exported: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
    out << "## Mods: " << modCount << "  Separators: " << sepCount << "\n";
    out << "##\n";

    for (int i = 0; i < m_modList->count(); ++i) {
        auto *item = m_modList->item(i);
        if (item->data(ModRole::ItemType).toString() == ItemType::Separator) {
            QColor bg = item->data(ModRole::BgColor).value<QColor>();
            QColor fg = item->data(ModRole::FgColor).value<QColor>();
            out << "# " << item->text()
                << " <color>" << bg.name(QColor::HexArgb) << "</color>"
                << "<fgcolor>" << fg.name(QColor::HexArgb) << "</fgcolor>\n";
        } else {
            QChar prefix     = (item->checkState() == Qt::Checked) ? '+' : '-';
            QString modPath  = item->data(ModRole::ModPath).toString();
            QString custName = item->data(ModRole::CustomName).toString();
            QString annot    = item->data(ModRole::Annotation).toString();
            QString url      = item->data(ModRole::NexusUrl).toString();
            QString dateStr  = item->data(ModRole::DateAdded).toDateTime().toString(Qt::ISODate);
            out << prefix << " " << modPath
                << "\t" << custName
                << "\t" << encodeAnnot(annot)
                << "\t" << url
                << "\t" << dateStr
                << "\n";
        }
    }

    statusBar()->showMessage(T("status_exported").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::onImportMO2ModList()
{
    QString path = QFileDialog::getOpenFileName(
        this, T("import_mo2_dialog_title"),
        QDir::homePath(),
        "MO2 modlist (*.txt);;All Files (*)");
    if (!path.isEmpty()) doImportMO2ModList(path);
}

// Returns the Nexus mod ID from an MO2 meta.ini, or 0 if not a Nexus mod.
static int readMO2MetaId(const QString &metaIniPath)
{
    QSettings meta(metaIniPath, QSettings::IniFormat);
    const int id = meta.value("General/modid", 0).toInt();
    if (id <= 0) return 0;
    const QString repo = meta.value("General/repository").toString().trimmed().toLower();
    return (repo == "nexus" || repo.isEmpty()) ? id : 0;
}

// Import a Mod Organizer 2 `modlist.txt`. MO2's file lists mods in REVERSE priority
// order (last line = highest priority) and uses prefixes: `+` enabled, `-` disabled,
// `*` unmanaged (skipped). Separators have names ending in `_separator`.
// Imported mods have no on-disk path - they're created as not-installed placeholders
// for the user to either point at a directory or re-install from Nexus.
void MainWindow::doImportMO2ModList(const QString &path)
{
    if (!confirmReplaceModList()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, T("import_title"),
            T("import_mo2_read_error").arg(path));
        return;
    }

    // Collect lines in file order, then reverse so the first entry is the top of
    // MO2's list (highest priority) - matching our top-to-bottom display convention.
    QStringList lines;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        lines << line;
    }
    std::reverse(lines.begin(), lines.end());

    // Optionally locate the MO2 mods folder so we can read meta.ini files and
    // pre-fill Nexus URLs.  Default to a sibling "mods/" next to the modlist;
    // if that doesn't exist, ask the user once.
    QString modsDir;
    {
        const QString sibling = QFileInfo(path).dir().filePath("../mods");
        if (QDir(sibling).exists())
            modsDir = QDir(sibling).absolutePath();
    }
    if (modsDir.isEmpty()) {
        QMessageBox ask(this);
        ask.setWindowTitle(T("import_mo2_dialog_title"));
        ask.setIcon(QMessageBox::Question);
        ask.setText(T("import_mo2_mods_folder_prompt"));
        auto *yesBtn  = ask.addButton(T("import_mo2_mods_folder_yes"), QMessageBox::AcceptRole);
        ask.addButton(T("import_mo2_mods_folder_skip"), QMessageBox::RejectRole);
        ask.exec();
        if (ask.clickedButton() == yesBtn) {
            modsDir = QFileDialog::getExistingDirectory(
                this, T("import_mo2_mods_folder_pick"), QDir::homePath());
        }
    }

    const QString nexusGame = m_profiles->isEmpty() ? QStringLiteral("morrowind")
                                                 : currentProfile().id;

    m_undoStack->pushUndo();
    m_modList->clear();

    int added = 0, skipped = 0, linked = 0;
    for (const QString &raw : lines) {
        QChar prefix = raw[0];
        QString name = raw.mid(1).trimmed();
        if (name.isEmpty()) continue;

        // `*` entries are MO2 "unmanaged" / overwrite markers - nothing to import.
        if (prefix == '*') { ++skipped; continue; }

        bool enabled = (prefix == '+');
        bool isSep   = name.endsWith("_separator");
        if (isSep) name.chop(QString("_separator").size());

        auto *item = new QListWidgetItem(name);
        if (isSep) {
            item->setData(ModRole::ItemType, ItemType::Separator);
            item->setData(ModRole::BgColor,  QColor(55, 55, 75));
            item->setData(ModRole::FgColor,  QColor(Qt::white));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        } else {
            item->setData(ModRole::ItemType,      ItemType::Mod);
            item->setData(ModRole::CustomName,    name);
            item->setData(ModRole::InstallStatus, 0);
            item->setData(ModRole::DateAdded,     QDateTime::currentDateTime());
            item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);

            int modId = 0;
            if (!modsDir.isEmpty())
                modId = readMO2MetaId(modsDir + "/" + name + "/meta.ini");

            if (modId > 0) {
                const QString url = QString("https://www.nexusmods.com/%1/mods/%2")
                                        .arg(nexusGame).arg(modId);
                item->setData(ModRole::NexusId,  modId);
                item->setData(ModRole::NexusUrl, url);
                ++linked;
            } else {
                // No mod ID - point at the Nexus search page for this mod name
                // so the user can find and install it from the context menu.
                const QString search = QString("https://www.nexusmods.com/%1/search/?q=%2")
                    .arg(nexusGame,
                         QString::fromUtf8(QUrl::toPercentEncoding(name)));
                item->setData(ModRole::SourceUrl, search);
            }
        }
        m_modList->addItem(item);
        ++added;
    }

    saveModList();
    updateModCount();
    updateSectionCounts();
    statusBar()->showMessage(
        T("status_imported_mo2").arg(added).arg(skipped)
        + (linked > 0 ? QStringLiteral(" (%1 Nexus links)").arg(linked) : QString()),
        6000);
}

void MainWindow::onImportWabbajack()
{
    QString path = QFileDialog::getOpenFileName(
        this, T("import_wabbajack_dialog_title"),
        QDir::homePath(),
        "Wabbajack modlist (*.wabbajack);;All Files (*)");
    if (!path.isEmpty()) doImportWabbajack(path);
}

void MainWindow::doImportWabbajack(const QString &path)
{
    if (!confirmReplaceModList()) return;

    // -- Background worker result ---
    struct WJRaw {
        QString errorKey;
        QString errorArg;
        QJsonObject root;
    };

    // -- Progress dialog (indeterminate while extracting + parsing) ---
    auto *progress = new QProgressDialog(
        T("import_wabbajack_extracting"), QString(), 0, 0, this);
    progress->setWindowTitle(T("import_wabbajack_title"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);

    // -- Run extraction + JSON parse off the GUI thread ---
    auto *watcher = new QFutureWatcher<WJRaw>(this);

    // tmpDir must outlive the lambda; heap-allocate and transfer ownership
    // to the watcher so it's cleaned up when the watcher is destroyed.
    auto *tmpDir = new QTemporaryDir();

    QFuture<WJRaw> future = QtConcurrent::run([path, tmpDir]() -> WJRaw {
        if (!tmpDir->isValid())
            return { "import_wabbajack_extract_error", {}, {} };

        // Extract 'modlist' JSON entry from the ZIP.
        auto tryExtract = [&](const QString &prog, const QStringList &args) {
            QProcess proc;
            proc.start(prog, args);
            proc.waitForStarted(5000);
            proc.waitForFinished(120000);
            return proc.exitCode() == 0
                && QFile::exists(tmpDir->path() + "/modlist");
        };

        const bool ok =
            tryExtract("7z",    {"e", QString("-o") + tmpDir->path(),
                                 "-y", path, "modlist"})
         || tryExtract("unzip", {"-o", path, "modlist",
                                 "-d", tmpDir->path()});
        if (!ok)
            return { "import_wabbajack_extract_error", {}, {} };

        QFile f(tmpDir->path() + "/modlist");
        if (!f.open(QIODevice::ReadOnly))
            return { "import_wabbajack_extract_error", {}, {} };
        const QByteArray jsonData = f.readAll();
        f.close();

        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject())
            return { "import_wabbajack_parse_error",
                     parseErr.errorString(), {} };

        return { {}, {}, doc.object() };
    });

    watcher->setFuture(future);

    // When the worker finishes, close the progress dialog and continue.
    connect(watcher, &QFutureWatcher<WJRaw>::finished, this,
            [this, watcher, progress, tmpDir]() {
        progress->close();
        progress->deleteLater();
        watcher->deleteLater();
        const QScopedPointer<QTemporaryDir> tmpGuard(tmpDir);

        const WJRaw result = watcher->result();
        if (!result.errorKey.isEmpty()) {
            QMessageBox::warning(this, T("import_wabbajack_title"),
                T(result.errorKey).arg(result.errorArg));
            return;
        }
        finishWabbajackImport(result.root);
    });

    progress->show();
}

void MainWindow::finishWabbajackImport(const QJsonObject &root)
{

    // -- 4. Read list-level metadata ---
    const QString listName = root["Name"].toString().trimmed();
    const QString author   = root["Author"].toString().trimmed();
    const QString gameType = root["GameType"].toString();

    // Wabbajack game names → Nexus game slugs (lowercase; most are direct
    // lower-case but a few need explicit remapping).
    static const QHash<QString, QString> kGameSlug {
        { "SkyrimSpecialEdition",  "skyrimspecialedition"   },
        { "SkyrimVR",              "skyrimspecialedition"   },
        { "Skyrim",                "skyrim"                 },
        { "Fallout4",              "fallout4"               },
        { "Fallout4VR",            "fallout4"               },
        { "FalloutNV",             "falloutnewvegas"        },
        { "Fallout3",              "fallout3"               },
        { "Morrowind",             "morrowind"              },
        { "Oblivion",              "oblivion"               },
        { "Enderal",               "enderal"                },
        { "EnderalSpecialEdition", "enderalspecialedition"  },
        { "Starfield",             "starfield"              },
        { "DarkestDungeon",        "darkestdungeon"         },
        { "DarkestDungeon2",       "darkestdungeon2"        },
    };
    const QString defaultSlug = kGameSlug.value(gameType, gameType.toLower());

    // -- 5. Parse archives ---
    struct WJMod {
        QString name;
        QString nexusUrl;
        QString sourceUrl; // non-Nexus download page (GitHub, direct HTTP, etc.)
        int     nexusId   = 0;
        qint64  sizeBytes = 0;
        bool    isSep     = false;
    };

    QList<WJMod> mods;
    mods.reserve(512);

    for (const QJsonValue &val : root["Archives"].toArray()) {
        const QJsonObject arch  = val.toObject();
        const QJsonObject state = arch["State"].toObject();
        const QString stateType = state["$type"].toString();

        // Skip entries that are vanilla game files, not downloadable mods.
        if (stateType.contains(QLatin1String("GameFileSource"), Qt::CaseInsensitive))
            continue;

        // Skip tools that are irrelevant when installing via Nerevarine:
        // MO2, MWSE, MGE XE, xEdit, LOOT, etc. are launcher/tool entries
        // that Wabbajack bundles for its own install flow.
        {
            const QString aname = arch["Name"].toString().trimmed().toLower();
            const QString sname = state["Name"].toString().trimmed().toLower();
            static const QStringList kToolPrefixes = {
                "mod organizer", "modorganizer", "mod.organizer",
                "mwse", "mge xe", "mge-xe", "mgexe",
                "xedit", "tes3edit", "openmw-csh", "mlox",
                "loot", "wrye mash", "wryemash",
            };
            bool isTool = false;
            for (const QString &p : kToolPrefixes) {
                if (aname.startsWith(p) || sname.startsWith(p)) {
                    isTool = true; break;
                }
            }
            if (isTool) continue;
        }

        // Display name: State.Name is the human-readable title (Nexus page
        // name). Fall back to the archive filename with extension stripped.
        QString name = state["Name"].toString().trimmed();
        if (name.isEmpty()) {
            name = arch["Name"].toString().trimmed();
            for (const QLatin1String ext :
                     { QLatin1String(".7z"), QLatin1String(".zip"),
                       QLatin1String(".rar"), QLatin1String(".fomod") }) {
                if (name.endsWith(ext, Qt::CaseInsensitive)) {
                    name.chop(ext.size());
                    break;
                }
            }
        }
        if (name.isEmpty()) continue;

        // MO2-style separator: some WJ lists embed separator mods.
        const bool isSep = name.endsWith(QLatin1String("_separator"));
        if (isSep) name.chop(QStringLiteral("_separator").size());

        // Nexus identity (both 2.x and 3.x type strings contain "Nexus").
        QString nexusUrl;
        QString sourceUrl;
        int nexusId = 0;
        if (stateType.contains(QLatin1String("Nexus"), Qt::CaseInsensitive)) {
            nexusId = state["ModID"].toInt();
            if (nexusId > 0) {
                // State.GameName may differ from the list's top-level GameType
                // (e.g. a Skyrim SE list that links a mod for plain Skyrim).
                const QString modSlug = kGameSlug.value(
                    state["GameName"].toString(), defaultSlug);
                nexusUrl = QString("https://www.nexusmods.com/%1/mods/%2")
                           .arg(modSlug).arg(nexusId);
            }
        } else {
            // Non-Nexus sources: extract a human-visitable download URL so
            // the user knows where to get the file manually.
            // GitHub release: construct from User + Repository + Tag.
            const QString user = state["User"].toString().trimmed();
            const QString repo = state["Repository"].toString().trimmed();
            const QString tag  = state["Tag"].toString().trimmed();
            if (!user.isEmpty() && !repo.isEmpty()) {
                sourceUrl = tag.isEmpty()
                    ? QString("https://github.com/%1/%2").arg(user, repo)
                    : QString("https://github.com/%1/%2/releases/tag/%3")
                          .arg(user, repo, tag);
            }
            // Direct HTTP / Manual: use the Url field if present.
            if (sourceUrl.isEmpty()) {
                const QString url = state["Url"].toString().trimmed();
                if (!url.isEmpty() && url.startsWith("http"))
                    sourceUrl = url;
            }
        }

        mods.append({ name, nexusUrl, sourceUrl, nexusId,
                      arch["Size"].toVariant().toLongLong(), isSep });
    }

    // Deduplicate by Nexus mod ID: a single mod page may supply several
    // archives (main file + update, multiple texture resolutions, etc.).
    // Keep the entry with the largest file size - that's the primary
    // download.  Mods without a Nexus ID are kept as-is (no key to dedup on).
    {
        QHash<int, int> idxById; // nexusId → index into mods
        QList<WJMod> deduped;
        deduped.reserve(mods.size());
        for (const WJMod &m : std::as_const(mods)) {
            if (m.nexusId <= 0 || m.isSep) {
                deduped.append(m);
                continue;
            }
            auto it = idxById.find(m.nexusId);
            if (it == idxById.end()) {
                idxById.insert(m.nexusId, deduped.size());
                deduped.append(m);
            } else {
                WJMod &kept = deduped[it.value()];
                // Prefer the shorter name: the base mod title is shorter than
                // "ModName - Update" / "ModName - 2K Textures" variants.
                if (!m.name.isEmpty() && m.name.length() < kept.name.length())
                    kept.name = m.name;
                // Prefer the larger file as the canonical size.
                if (m.sizeBytes > kept.sizeBytes)
                    kept.sizeBytes = m.sizeBytes;
            }
        }
        mods = std::move(deduped);
    }

    if (mods.isEmpty()) {
        QMessageBox::warning(this, T("import_wabbajack_title"),
            T("import_wabbajack_no_mods"));
        return;
    }

    // -- 6. Confirm with a summary dialog ---
    {
        const int nexusCount = static_cast<int>(
            std::count_if(mods.begin(), mods.end(),
                          [](const WJMod &m){ return !m.nexusUrl.isEmpty(); }));

        QDialog dlg(this);
        dlg.setWindowTitle(T("import_wabbajack_title"));
        auto *layout = new QVBoxLayout(&dlg);

        QString info;
        if (!listName.isEmpty()) info += "<b>" + listName.toHtmlEscaped() + "</b>";
        if (!author.isEmpty())   info += " " + T("import_wabbajack_by")
                                             + " " + author.toHtmlEscaped();
        if (!info.isEmpty())     info += "<br><br>";
        info += T("import_wabbajack_summary").arg(mods.size()).arg(nexusCount);

        auto *lbl = new QLabel(info, &dlg);
        lbl->setWordWrap(true);
        lbl->setTextFormat(Qt::RichText);
        layout->addWidget(lbl);

        auto *bb = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        layout->addWidget(bb);

        if (dlg.exec() != QDialog::Accepted) return;
    }

    // -- 7. Populate the mod list ---
    m_undoStack->pushUndo();
    m_modList->clear();

    for (const WJMod &mod : mods) {
        auto *item = new QListWidgetItem(mod.name);
        if (mod.isSep) {
            item->setData(ModRole::ItemType, ItemType::Separator);
            item->setData(ModRole::BgColor,  QColor(55, 55, 75));
            item->setData(ModRole::FgColor,  QColor(Qt::white));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                           | Qt::ItemIsDragEnabled);
        } else {
            item->setData(ModRole::ItemType,      ItemType::Mod);
            item->setData(ModRole::CustomName,    mod.name);
            item->setData(ModRole::NexusUrl,      mod.nexusUrl);
            item->setData(ModRole::NexusId,       mod.nexusId);
            if (!mod.sourceUrl.isEmpty())
                item->setData(ModRole::SourceUrl, mod.sourceUrl);
            item->setData(ModRole::InstallStatus, 0);
            item->setData(ModRole::DateAdded,     QDateTime::currentDateTime());
            if (mod.sizeBytes > 0)
                item->setData(ModRole::ExpectedSize, mod.sizeBytes);
            item->setCheckState(Qt::Checked);
        }
        m_modList->addItem(item);
    }

    saveModList();
    updateModCount();
    updateSectionCounts();
    statusBar()->showMessage(
        T("status_imported_wabbajack").arg(mods.size()).arg(listName),
        5000);
}

void MainWindow::handleDroppedImportFile(const QString &path)
{
    const QFileInfo fi(path);

    if (fi.suffix().toLower() == QLatin1String("wabbajack")) {
        doImportWabbajack(path);
        return;
    }

    if (fi.suffix().toLower() != QLatin1String("txt")) return;

    // Peek at the first non-comment line to identify the format.  We accept any
    // .txt filename (was previously locked to "modlist.txt"), since users
    // routinely rename their exports - e.g. "JAUME modlist_export.txt".
    //
    // MO2 lines:        +ModName           - no tab, no leading slash after prefix
    // Nerevarine lines: +/path/to\tname\t… - tab-separated, body starts with /
    //                   (or "+\tname\t..."  for not-installed entries with no path)
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);
    QString firstBody;
    bool looksLikeAnyModlist = false;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        if (line.size() < 2) continue;
        const QChar c = line[0];
        if (c != '+' && c != '-' && c != '*') continue;
        firstBody = line.mid(1); // strip +/-/* prefix
        looksLikeAnyModlist = true;
        break;
    }
    f.close();
    if (!looksLikeAnyModlist) {
        QMessageBox::warning(this, T("import_title"),
            T("import_unknown_format").arg(fi.fileName()));
        return;
    }

    // Nerevarine format: tab-separated.  MO2 format: bare mod name, no tabs.
    const bool looksLikeMO2 = !firstBody.contains('\t');
    if (looksLikeMO2) {
        doImportMO2ModList(path);
    } else {
        doImportNerevarineModList(path);
    }
}

void MainWindow::onImportMO2Profile()
{
    if (!confirmReplaceModList()) return;

    // -- 1. Pick the MO2 instance folder ---
    QString instanceDir = QFileDialog::getExistingDirectory(
        this, T("import_mo2_profile_pick_instance"), QDir::homePath());
    if (instanceDir.isEmpty()) return;

    QDir iDir(instanceDir);
    bool hasIni      = QFile::exists(instanceDir + "/ModOrganizer.ini");
    bool hasMods     = iDir.exists("mods");
    bool hasProfiles = iDir.exists("profiles");

    if (!hasIni && !(hasMods && hasProfiles)) {
        QMessageBox::warning(this, T("import_mo2_profile_pick_title"),
            T("import_mo2_profile_not_instance"));
        return;
    }

    // -- 2. Resolve mods + profiles dirs (ModOrganizer.ini overrides) -----
    QString modsDir     = instanceDir + "/mods";
    QString profilesDir = instanceDir + "/profiles";

    if (hasIni) {
        QSettings ini(instanceDir + "/ModOrganizer.ini", QSettings::IniFormat);

        // MO2 stores paths as @ByteArray(...); strip the wrapper, then normalise
        // Windows backslashes. If the resolved path doesn't exist (e.g. a Wine
        // drive-letter path), fall back to the instance-relative default.
        auto readPath = [&](const QString &key, const QString &fallback) -> QString {
            QString v = ini.value(key).toString();
            if (v.startsWith(QLatin1String("@ByteArray(")) && v.endsWith(')'))
                v = v.mid(11, v.size() - 12);
            v.replace('\\', '/');
            v = v.trimmed();
            if (v.isEmpty())             return fallback;
            if (QDir::isRelativePath(v)) return instanceDir + '/' + v;
            if (QDir(v).exists())        return v;
            return fallback;   // Wine drive-letter path not reachable on this host
        };

        modsDir     = readPath("Settings/mods_directory",     modsDir);
        profilesDir = readPath("Settings/profiles_directory", profilesDir);
    }

    // -- 3. Enumerate profiles ---
    QDir profDir(profilesDir);
    QStringList profiles = profDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    if (profiles.isEmpty()) {
        QMessageBox::warning(this, T("import_mo2_profile_pick_title"),
            T("import_mo2_profile_no_profiles").arg(profilesDir));
        return;
    }

    // -- 4. Profile-picker dialog (skip if only one profile) ---
    QString selectedProfile = profiles.first();
    bool importPlugins = false;

    {
        QDialog dlg(this);
        dlg.setWindowTitle(T("import_mo2_profile_pick_title"));
        auto *layout = new QVBoxLayout(&dlg);

        QListWidget *profileList = nullptr;
        if (profiles.size() > 1) {
            layout->addWidget(new QLabel(T("import_mo2_profile_pick_label"), &dlg));
            profileList = new QListWidget(&dlg);
            profileList->addItems(profiles);
            profileList->setCurrentRow(0);
            layout->addWidget(profileList);
        }

        bool anyPlugins = std::any_of(profiles.begin(), profiles.end(), [&](const QString &p) {
            return QFile::exists(profilesDir + "/" + p + "/plugins.txt");
        });
        QCheckBox *pluginsCb = nullptr;
        if (anyPlugins) {
            pluginsCb = new QCheckBox(T("import_mo2_profile_import_plugins"), &dlg);
            pluginsCb->setChecked(true);
            layout->addWidget(pluginsCb);
        }

        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        layout->addWidget(bb);

        if (dlg.exec() != QDialog::Accepted) return;

        if (profileList && profileList->currentItem())
            selectedProfile = profileList->currentItem()->text();
        if (pluginsCb)
            importPlugins = pluginsCb->isChecked();
    }

    // -- 5. Read modlist.txt ---
    QString modlistFile = profilesDir + "/" + selectedProfile + "/modlist.txt";
    QFile f(modlistFile);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, T("import_mo2_profile_pick_title"),
            T("import_mo2_profile_no_modlist").arg(modlistFile));
        return;
    }

    QStringList lines;
    {
        QTextStream in(&f);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            lines << line;
        }
    }
    // MO2 modlist.txt is highest-priority-last; reverse to get top-to-bottom order.
    std::reverse(lines.begin(), lines.end());

    m_undoStack->pushUndo();
    m_modList->clear();

    int added = 0, linked = 0, skipped = 0;
    for (const QString &raw : lines) {
        QChar prefix = raw[0];
        QString name = raw.mid(1).trimmed();
        if (name.isEmpty()) continue;

        // '*' = unmanaged / Overwrite virtual mod - nothing to import.
        if (prefix == '*') { ++skipped; continue; }

        bool enabled = (prefix == '+');
        bool isSep   = name.endsWith(QLatin1String("_separator"));
        if (isSep) name.chop(QStringLiteral("_separator").size());

        auto *item = new QListWidgetItem(name);
        if (isSep) {
            item->setData(ModRole::ItemType, ItemType::Separator);
            item->setData(ModRole::BgColor,  QColor(55, 55, 75));
            item->setData(ModRole::FgColor,  QColor(Qt::white));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        } else {
            // Link to the actual mod folder when it exists in the MO2 mods dir.
            QString modPath;
            QDir candidate(modsDir + "/" + name);
            if (candidate.exists()) {
                modPath = candidate.absolutePath();
                ++linked;
            }

            item->setData(ModRole::ItemType,      ItemType::Mod);
            item->setData(ModRole::CustomName,    name);
            item->setData(ModRole::ModPath,       modPath);
            item->setData(ModRole::InstallStatus, modPath.isEmpty() ? 0 : 1);
            item->setData(ModRole::DateAdded,     QDateTime::currentDateTime());
            item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);

            // Read meta.ini from the mod folder (present whether linked or not).
            const QString nexusGame = m_profiles->isEmpty()
                ? QStringLiteral("morrowind") : currentProfile().id;
            const int modId = readMO2MetaId(modsDir + "/" + name + "/meta.ini");
            if (modId > 0) {
                const QString url = QString("https://www.nexusmods.com/%1/mods/%2")
                                        .arg(nexusGame).arg(modId);
                item->setData(ModRole::NexusId,  modId);
                item->setData(ModRole::NexusUrl, url);
            } else {
                const QString search = QString("https://www.nexusmods.com/%1/search/?q=%2")
                    .arg(nexusGame,
                         QString::fromUtf8(QUrl::toPercentEncoding(name)));
                item->setData(ModRole::SourceUrl, search);
            }
        }
        m_modList->addItem(item);
        ++added;
    }

    // -- 6. Optional: import plugin load order from plugins.txt ---
    if (importPlugins) {
        QString pluginsFile = profilesDir + "/" + selectedProfile + "/plugins.txt";
        QFile pf(pluginsFile);
        if (pf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QStringList order;
            QTextStream in(&pf);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.isEmpty() || line.startsWith('#')) continue;
                if (line.startsWith('*')) line = line.mid(1).trimmed();
                if (!line.isEmpty()) order << line;
            }
            if (!order.isEmpty()) {
                m_loadOrder = order;
                saveLoadOrder();
            }
        }
    }

    saveModList();
    updateModCount();
    updateSectionCounts();
    statusBar()->showMessage(
        T("status_imported_mo2_profile").arg(added).arg(linked).arg(skipped).arg(selectedProfile),
        6000);
}

void MainWindow::onViewChangelog(QListWidgetItem *item)
{
    if (!item) return;

    if (m_apiKey.isEmpty()) {
        QMessageBox::information(this, T("nxm_api_key_required_title"),
            T("nxm_api_key_required_body"));
        onSetApiKey();
        if (m_apiKey.isEmpty()) return;
    }

    const QString nexusUrl = item->data(ModRole::NexusUrl).toString();
    const QStringList parts = QUrl(nexusUrl).path().split('/', Qt::SkipEmptyParts);
    if (parts.size() < 3 || parts[1] != QLatin1String("mods")) return;
    bool ok = false;
    const int modId = parts[2].toInt(&ok);
    if (!ok) return;
    const QString game = parts[0];

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

    if (QMessageBox::question(this, T("import_title"), T("import_confirm"))
            != QMessageBox::Yes)
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

void MainWindow::doImportNerevarineModList(const QString &path)
{
    if (!confirmReplaceModList()) return;

    const QString foreignBase = detectModsBaseFromFile(path);

    QString remapFrom, remapTo;
    if (!foreignBase.isEmpty() && foreignBase != m_modsDir) {
        QMessageBox info(this);
        info.setWindowTitle(T("import_remap_title"));
        info.setText(T("import_remap_prompt").arg(foreignBase));
        info.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        info.setDefaultButton(QMessageBox::Ok);
        if (info.exec() != QMessageBox::Ok) return;

        QString chosen = QFileDialog::getExistingDirectory(
            this, T("import_remap_pick"), m_modsDir);
        if (chosen.isEmpty()) return;

        remapFrom = foreignBase;
        remapTo   = chosen;
    }

    loadModList(path, remapFrom, remapTo);
    // Persist the imported state immediately so openmw.cfg / launcher.cfg
    // reflect the imported mods' enabled/disabled flags right away - without
    // this, the user would see "every ESP inactive" in the OpenMW Launcher
    // until something else (closing the app, toggling a mod) triggered a
    // saveModList → syncOpenMWConfig pass.
    saveModList();
    statusBar()->showMessage(T("status_imported"), 3000);
}

void MainWindow::onImportModList()
{
    QString path = QFileDialog::getOpenFileName(
        this, T("import_dialog_title"),
        QDir::homePath(),
        "Mod List (*.txt);;All Files (*)");
    if (path.isEmpty()) return;

    doImportNerevarineModList(path);
}

void MainWindow::onNewModList()
{
    QString currentPath = modlistPath();
    int count = m_modList->count();
    QString msg = count > 0
        ? T("menu_new_modlist_confirm_body").arg(QFileInfo(currentPath).fileName())
        : T("menu_new_modlist_confirm_body").arg(QFileInfo(currentPath).fileName());

    if (QMessageBox::question(this, T("menu_new_modlist_confirm_title"), msg)
            != QMessageBox::Yes)
        return;

    QFile::remove(currentPath);
    m_modList->clear();
    updateModCount();
    updateSectionCounts();
    statusBar()->showMessage(T("status_new_modlist"), 3000);
}

// Sort by date added

void MainWindow::onSortByDate()
{
    // Menu actions pre-set m_dateSortAsc; only the header button toggles.
    if (sender() == m_dateSortBtn)
        m_dateSortAsc = !m_dateSortAsc;
    m_dateSortBtn->setText(m_dateSortAsc
        ? T("col_date_added_asc")
        : T("col_date_added_desc"));

    modlist_sort::byDate(m_modList, m_dateSortAsc);

    saveModList();
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

struct BuiltinGameDef { QString id; QString name; QString defaultModsDirName; };
static const BuiltinGameDef kBuiltinGames[] = {
    // -- OpenMW (Morrowind) ---
    {"morrowind",            "OpenMW (Morrowind)",                  "nerevarine_mods"},
};

// Adapter for firstrun::runWizard - defined down here so the BuiltinGameDef
// type is complete.  Declaration of the forward stub is at the top of the
// file alongside other forward helpers.
static QList<firstrun::GameChoice> builtinGameChoices()
{
    QList<firstrun::GameChoice> out;
    out.reserve(sizeof(kBuiltinGames) / sizeof(kBuiltinGames[0]));
    for (const BuiltinGameDef &g : kBuiltinGames)
        out.append({g.id, g.name, g.defaultModsDirName});
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

    // These always appear at the top in this fixed order.
    // If a game hasn't been added as a profile yet it is shown greyed out.
    static const QList<QPair<QString,QString>> kPinned = {
        {"morrowind",            "OpenMW (Morrowind)"},
        // Disabled in v0.3 - FNV support is in progress, not ready to ship.
        // Re-enable by uncommenting once detection + per-game install paths are tested.
        // {"falloutnewvegas",      "Fallout: New Vegas"},
    };

    QSet<int> pinnedIdx;
    for (const auto &[pid, fallbackName] : kPinned) {
        int found = -1;
        for (int i = 0; i < m_profiles->size(); ++i)
            if (m_profiles->games()[i].id == pid) { found = i; break; }

        if (found >= 0) {
            // Always use the canonical pinned name, not whatever is stored in the profile
            auto *act = menu->addAction(fallbackName);
            act->setCheckable(true);
            act->setChecked(found == m_profiles->currentIndex());
            connect(act, &QAction::triggered, this, [this, found]() { switchToGame(found); });
            pinnedIdx.insert(found);
        } else if (pid == "morrowind") {
            // OpenMW must always be configured (created on first run); if it
            // somehow isn't, fall back to a disabled placeholder.
            auto *act = menu->addAction(fallbackName);
            act->setEnabled(false);
        } else {
            // Click to detect + add this game on first use.
            const QString idCopy = pid;
            const QString nameCopy = fallbackName;
            auto *act = menu->addAction(fallbackName);
            connect(act, &QAction::triggered, this,
                    [this, idCopy, nameCopy]() { addAndDetectGame(idCopy, nameCopy); });
        }
    }

    // Remaining (non-pinned) games the user has added
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

    // Replace the old menu (avoid memory leak)
    delete m_gameBtn->menu();
    m_gameBtn->setMenu(menu);

    // Show the right launch button(s) for the current game.
    // setProfileVis() sets the profile-visibility gate; user-visibility
    // (from the Customize Toolbar dialog) is ANDed separately.
    bool isMorrowind    = (gp.id == "morrowind");
    bool hasNoLauncher  = (gp.id == "falloutlondon");   // GOG total conversion - no separate launcher
    auto setProfileVis = [this](QAction *a, bool v) {
        if (!a) return;
        a->setProperty("nerev_profile_visible", v);
        m_tbCustom->applyVisibility(a);
    };
    setProfileVis(m_actLaunchOpenMW,        isMorrowind);
    setProfileVis(m_actLaunchLauncher,      isMorrowind);
    setProfileVis(m_actLaunchGame,          !isMorrowind);
    setProfileVis(m_actLaunchSteamLauncher, !isMorrowind && !hasNoLauncher);
    setProfileVis(m_actTuneSkyrimIni,       gp.id == "skyrimspecialedition");
    setProfileVis(m_actSortLoot,            !lootGameFor(gp.id).isEmpty());
    // Mods-menu mirror of the toolbar LOOT action: unlike toolbar entries it
    // isn't subject to the user-visibility gate in m_tbCustom->applyVisibility(),
    // so we toggle it with the plain QAction API.
    if (m_actMenuSortLoot)
        m_actMenuSortLoot->setVisible(!lootGameFor(gp.id).isEmpty());

    // Featured Modlists dropdown is parked behind a "Work in progress"
    // dialog (see setupToolbar). When the feature is revived, restore the
    // per-game menu build here - the old logic is intentionally preserved
    // in version control history.
}

void MainWindow::switchToGame(int idx)
{
    if (idx < 0 || idx >= m_profiles->size() || idx == m_profiles->currentIndex()) return;

    saveModList(); // persist current game's list before switching

    m_profiles->setCurrentIndex(idx);
    applyCurrentProfileToMirrors();

    m_modList->clear();
    loadModList();
    updateGameButton();

    statusBar()->showMessage(
        T("status_switched_game").arg(m_profiles->current().displayName), 3000);
}

void MainWindow::onAddGame()
{
    // Disable adding other games in this release - OpenMW only
    QMessageBox::information(this, T("add_game_title"),
        "Only OpenMW (Morrowind) is supported in this release.");
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
        QMessageBox::information(this,
            QString("Locate %1").arg(displayName),
            QString("%1 was not found in Steam, Heroic, or Lutris.\n"
                    "Please point to the game's executable.").arg(displayName));
        const QString picked = QFileDialog::getOpenFileName(this,
            QString("Locate %1 executable").arg(displayName),
            QDir::homePath(),
            "Executables (*.exe);;All files (*)");
        if (picked.isEmpty()) return;
        exe = picked;
    }

    // Confirm to the user where it was found.
    QMessageBox::information(this, displayName,
        QString("%1 detected at:\n%2").arg(displayName, QDir::toNativeSeparators(exe)));

    // Per-game mods directory: each profile gets its own root so users can
    // park heavy installs (FNV, Skyrim, etc) on a different mount than OpenMW.
    const QString defaultModsDir = QDir::homePath() + "/Games/" + gameId + "_mods";
    QString modsDir = QFileDialog::getExistingDirectory(this,
        QString("Choose mods directory for %1").arg(displayName),
        defaultModsDir,
        QFileDialog::ShowDirsOnly | QFileDialog::DontConfirmOverwrite);
    if (modsDir.isEmpty()) modsDir = defaultModsDir;
    QDir().mkpath(modsDir);

    // Create the profile and switch.
    GameProfile gp;
    gp.id          = gameId;
    gp.displayName = displayName;
    gp.modsDir     = modsDir;
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
    saveModList();
    QSettings s;
    s.setValue("window/geometry",  saveGeometry());
    s.setValue("window/maximized", isMaximized());
    QMainWindow::closeEvent(event);
}
