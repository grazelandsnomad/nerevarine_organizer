// mainwindow_setup - toolbar/menu/central-widget construction, game + profile
// switching, and the read-only summary/diagnostic dialogs. The connect-dense
// part of MainWindow; split out so it compiles in parallel. The ModListWidget
// QListWidget subclass (used only when building the central widget) lives here.

#include "mainwindow.h"
#include "mainwindow_internal.h"
#include "settings.h"
#include "theme.h"
#include "separatordialog.h"
#include "modlistdelegate.h"
#include "modroles.h"
#include "translator.h"
#include "placeholder_state.h"
#include "fomodwizard.h"
#include "bainwizard.h"
#include "modlist_model_widget_bridge.h"
#include "modlist_serializer.h"
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
#include "game_adapter.h"
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
#include "firstrunwizard.h"
#include "fs_utils.h"
#include "pluginparser.h"
#include "mod_cleanup.h"
#include "log_triage_dialog.h"
#include "plugin_collisions.h"
#include "asset_collisions.h"
#include "modlist_sync_guard.h"
#include <QFutureWatcher>
using plugins::collectDataFolders;
using plugins::readTes3Masters;
#include <QDropEvent>
#include <QMimeData>
using fsutils::sanitizeFolderName;
#include "mainwindow_internal.h"

// Moved file-static, forward-declared.
static QString locateOpenMWBinary(const QString &profileHint) ;

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
    modsMenu->addAction(T("menu_cleanup_folders"), this, &MainWindow::onCleanUpModFolders);
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

    // Pins OpenMW + FNV + Starfield in the dropdown. OpenMW and FNV are the
    // exercised ones; Starfield is classified for deploy + Plugins.txt but the
    // real-install pass has not been run, so treat it as experimental. This
    // toggle surfaces the rest of the legacy game list. Off by default.
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
        // them into one saveModList avoids N redundant writes on the save queue.
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

// Clean Up Mod Folders. Upgrades used to leave every previous build on disk
// (mod_naming::findStaleSiblings only matched a literal re-download of the same
// file), so libraries accumulated folders nothing points at - one real one had
// 96 of them holding 8.6 GiB. The install path no longer creates them; this
// reclaims what already piled up.
//
// Deliberately manual and preview-first: it is a recursive delete over the
// user's mod library, so it never runs on its own and never deletes without an
// explicit confirm.
void MainWindow::onCleanUpModFolders()
{
    // An in-flight download's extract dir is not in the modlist yet, so it would
    // read as an orphan. Same for an extraction still running behind a
    // still-spinning row.
    if (!m_downloadQueue->isEmpty()) {
        ui::warn(this, T("cleanup_folders_title"), T("move_mods_err_downloads"));
        return;
    }
    for (int i = 0; i < m_modList->count(); ++i) {
        if (m_modList->item(i)->data(ModRole::InstallStatus).toInt() == 2) {
            ui::warn(this, T("cleanup_folders_title"), T("move_mods_err_downloads"));
            return;
        }
    }
    if (m_modsDir.isEmpty() || !QDir(m_modsDir).exists()) {
        ui::warn(this, T("cleanup_folders_title"), T("cleanup_folders_no_dir"));
        return;
    }

    const QString modsRoot = QFileInfo(m_modsDir).absoluteFilePath();

    // Three independent reference sources, so one unreadable file cannot make
    // the sweep think the library is empty.
    QStringList referenced;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        for (int role : { ModRole::ModPath, ModRole::IntendedModPath,
                          ModRole::MergeTargetPath, ModRole::PrevModPath }) {
            const QString p = it->data(role).toString();
            if (!p.isEmpty()) referenced << p;
        }
    }
    for (const auto &prof : otherProfileModlists())
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
        ui::info(this, T("cleanup_folders_title"), T("cleanup_folders_none"));
        return;
    }
    // A modlist that failed to parse looks exactly like a huge pile-up. Refuse
    // rather than act on it. (Calibration: the observed real case was 26%.)
    if (!onDisk.isEmpty() && orphans.size() * 100 / onDisk.size() > 60) {
        ui::warn(this, T("cleanup_folders_title"),
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
    if (!ui::confirmMonospaceReport(this, T("cleanup_folders_title"), body,
                                    720, 480, summary,
                                    T("cleanup_folders_accept").arg(orphans.size())))
        return;

    QStringList toDelete;
    for (const auto &e : sized)
        toDelete << QDir(modsRoot).filePath(e.second);
    removeModFoldersAsync(toDelete);

    m_scans->scheduleSizeScan();
    statusBar()->showMessage(
        T("cleanup_folders_done").arg(QString::number(orphans.size()),
                                      cleanupFmtBytes(totalBytes)),
        8000);
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
