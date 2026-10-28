// deploy_flow - Bethesda deploy/undeploy, the deploy hint, the script-
// extender check and the game launches, over the WindowShell. The Bethesda
// path-resolution file-statics live here with the flow that uses them.

#include "deploy_flow.h"
#include "window_shell.h"
#include "modlist_model.h"
#include "modrow.h"
#include "settings.h"
#include "separatordialog.h"
#include "mod_row.h"
#include "modroles.h"
#include "translator.h"
#include "placeholder_state.h"
#include "fomodwizard.h"
#include "bainwizard.h"
#include "bethesda_deploy.h"
#include "pe_info.h"
#include "skse_check.h"
#include "bethesda_loadorder.h"
#include "bethesda_archives.h"
#include "bethesda_custom_ini.h"
#include "opengothic.h"
#include "store_scan.h"
#include <functional>
#include "proton_paths.h"
#include "dll_overrides.h"
#include "modlist_summary_dialog.h"
#include "nexuscontroller.h"
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
#include "conflict_inspector.h"
#include "deployment_report.h"
#include "report_dialog.h"
#include "toolbar_customization.h"
#include "scan_coordinator.h"
#include "backup_manager.h"
#include "bulk_install_queue.h"
#include "review_updates_dialog.h"
#include "launch_warnings.h"
#include "modlist_sort.h"
#include "send_to_dialog.h"
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
#include <QSaveFile>
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
#include "async_guarded.h"
#include "firstrunwizard.h"
#include "fs_utils.h"
#include "install_layout.h"
#include "scan_overlay.h"

#include <atomic>
#include <memory>
#include "logging.h"
#include "pluginparser.h"
#include "log_triage_dialog.h"
#include <QFutureWatcher>
using plugins::collectDataFolders;
using plugins::readTes3Masters;
#include <QDropEvent>
#include <QMimeData>
using fsutils::sanitizeFolderName;
#include "mainwindow_internal.h"
#include "modlist_model.h"
#include "user_state_paths.h"
#include "root_routes.h"
#include "steam_client.h"

// What writing the load order did: how many plugins it activated, and which it
// left out because a master is not installed - each with the mod it came from.
struct HeldPlugin {
    QString     plugin;
    QStringList missing;       // in no file in Data/
    QStringList heldMasters;   // in Data/, but left out themselves
    QString     mod;
};
struct ActivateResult {
    int               activated = 0;
    QList<HeldPlugin> held;
    // The same, by the file actually missing (bethesda_loadorder::missingMasters).
    QList<bethesda_loadorder::MissingMaster> missingFiles;
};
// What a deploy reports: the summary the Deploy dialog shows, the long lists
// it keeps behind Show Details, and how many plugins it had to leave out, for
// the pre-launch re-sync's one status line.
struct DeployOutcome {
    QString summary;
    QString details;
    int     heldBack = 0;
};

// Forward-declare the moved file-statics so intra-TU call order is irrelevant.
static ActivateResult bethesdaActivate(const QString &id, const GameAdapter *adapter, const QString &dataDir, const bethesda_deploy::Manifest &manifest, const QStringList &loadOrder);
static DeployOutcome bethesdaApplyDeploy(const QString &id, const GameAdapter *adapter, const QString &dataDir, const QList<bethesda_deploy::DeploySource> &sources, const QList<bethesda_deploy::DeploySource> &rootSources, const QString &modlistFile, const QStringList &loadOrder, const QString &fingerprint, const bethesda_deploy::ProgressFn &progress, const QString &movedFrom);
static QString bethesdaApplyUndeploy(const QString &id, const GameAdapter *adapter, const QString &dataDir, const QString &modlistFile, int *removedOut = nullptr);
static int bethesdaLeaveOldFolder(const QString &id, const GameAdapter *adapter, const QString &oldDataDir, const QString &modlistFile);
static QStringList gameExeNames(const GameAdapter *adapter);
static QStringList bethesdaConfigureArchives(const QString &id, const GameAdapter *adapter, const QString &dataDir, const bethesda_deploy::Manifest &manifest);
static QStringList bethesdaPrefixUserDirs(const GameAdapter *adapter, const QString &dataDir);
static QString bethesdaResolveDataDir(QWidget *parent, const QString &id, const GameAdapter *adapter, bool allowPrompt);
static void bethesdaStatePaths(const QString &modlistFile, QString &manifestPath, QString &backupDir);
static QString findScriptExtenderLoader(const GameAdapter *adapter, const QString &installDir);
static bool launchViaProton(QWidget *parent, const QString &appId, const QString &exePath);
static QList<bethesda_deploy::DeploySource> gatherBethesdaSources(const ModlistModel &model, const QList<int> &order, const GameAdapter *adapter, const QString &modsDir);
static bool launchViaGog(const QString &gogExe);
static QString resolveBethesdaIniDir(const QString &id, const GameAdapter *adapter, const QString &dataDir);
static QString resolveBethesdaPluginsTxt(const QString &id, const GameAdapter *adapter, const QString &dataDir);
static void restoreNerevarineBak(const QString &path);


void DeployFlow::onTuneSkyrimIni()
{
    // Which game's prefs ini, and where it lives, are adapter data - this used
    // to hardcode Skyrim SE's id, app id and My-Games folder, so a second
    // profile on the same engine (Skyrim AE) had no way to reach the dialog and
    // would have written into SE's ini dir if it had.
    if (!m_shell.hasProfile()) return;
    const QString id = m_shell.currentGameId();
    const GameAdapter *adapter = GameAdapterRegistry::find(id);
    // Same shape as the deploy guards below: bail on no adapter OR no tuner,
    // rather than deriving one from the other (the action is hidden for both).
    if (!adapter || adapter->prefsIniName().isEmpty()) return;
    const QString prefsName = adapter->prefsIniName();

    QString iniDir = Settings::iniDir(id);

    auto hasIni = [&prefsName](const QString &dir) {
        return !dir.isEmpty() && QFileInfo::exists(QDir(dir).filePath(prefsName));
    };

    if (!hasIni(iniDir)) {
        // Probe the game's own Proton prefix + the plain $HOME layout before
        // asking. Same My-Games variants the deploy path walks.
        const QString appId  = adapter->steamAppId();
        const QString folder = adapter->myGamesName();
        QStringList candidates;
        if (!appId.isEmpty() && !folder.isEmpty()) {
            const QString prefixUser = proton::prefixUserDir(
                QDir::homePath() + "/.local/share/Steam/steamapps/compatdata", appId);
            candidates += proton::myGamesDirs(prefixUser, folder);
        }
        if (!folder.isEmpty())
            candidates << QDir::homePath() + "/Documents/My Games/" + folder;
        for (const QString &p : candidates) if (hasIni(p)) { iniDir = p; break; }
    }

    if (!hasIni(iniDir)) {
        ui::info(m_shell.dialogParent(), T("skyini_locate_title"), T("skyini_locate_body"));
        QString picked = QFileDialog::getExistingDirectory(
            m_shell.dialogParent(), T("skyini_locate_dialog"), QDir::homePath());
        if (picked.isEmpty()) return;
        if (!hasIni(picked)) {
            ui::warn(m_shell.dialogParent(), T("skyini_locate_title"), T("skyini_locate_missing"));
            return;
        }
        iniDir = picked;
    }
    Settings::setIniDir(id, iniDir);

    QString prefsPath = QDir(iniDir).filePath(prefsName);

    IniDoc prefs;
    if (!prefs.load(prefsPath)) {
        ui::warn(m_shell.dialogParent(), T("skyini_error_title"), T("skyini_read_error").arg(prefsPath));
        return;
    }

    // -- Build dialog ---
    QDialog dlg(m_shell.dialogParent());
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

    // -- Apply: write back into the prefs ini ---
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
        ui::warn(m_shell.dialogParent(), T("skyini_error_title"), T("skyini_write_error").arg(prefsPath));
        return;
    }

    m_shell.showStatus(T("skyini_status_saved"), 4000);
}

// Candidate compatdata roots holding this game's Proton prefix, best guess
// first: derived from the actual install location (which handles a game on a
// second Steam library), then the standard Steam locations.
static QStringList bethesdaCompatdataRoots(const QString &dataDir)
{
    QStringList out;
    const QString marker = QStringLiteral("/steamapps/common/");
    const int idx = dataDir.indexOf(marker);
    if (idx > 0) out << dataDir.left(idx) + "/steamapps/compatdata";
    const QString home = QDir::homePath();
    out << home + "/.local/share/Steam/steamapps/compatdata"
        << home + "/.steam/steam/steamapps/compatdata";
    out.removeDuplicates();
    return out;
}

// Candidate Proton-prefix "users/steamuser" dirs for a Bethesda profile, best
// guess first.  Empty entries are dropped.
static QStringList bethesdaPrefixUserDirs(const GameAdapter *adapter,
                                          const QString &dataDir)
{
    const QString appId = adapter ? adapter->steamAppId() : QString();
    if (appId.isEmpty()) return {};
    QStringList out;
    for (const QString &root : bethesdaCompatdataRoots(dataDir))
        out << proton::prefixUserDir(root, appId);
    out.removeAll(QString());
    return out;
}

// The prefix registry to record DLL overrides in: the first user.reg that
// actually exists.  Empty when the game has never been run under Proton (no
// prefix yet) or isn't a Steam title - in both cases there is nothing to write
// to, and deploy carries on without it.
static QString resolveUserRegPath(const GameAdapter *adapter, const QString &dataDir)
{
    const QString appId = adapter ? adapter->steamAppId() : QString();
    if (appId.isEmpty()) return {};
    for (const QString &root : bethesdaCompatdataRoots(dataDir)) {
        const QString reg = proton::userRegPath(root, appId);
        if (QFileInfo::exists(reg)) return reg;
    }
    return {};
}

// Tell the game's Proton prefix to prefer the wrapper DLLs this deployment put
// beside the .exe (dxgi.dll and friends - see dll_overrides for why the files
// alone do nothing under Wine).  Returns the names actually added, which the
// caller records in the manifest so undeploy removes exactly those.
//
// Best-effort throughout: no prefix, an unreadable registry or a failed write
// leaves the deployment itself intact, just not hooked up.
static QStringList bethesdaRegisterDllOverrides(const GameAdapter *adapter,
                                                const QString &dataDir,
                                                const bethesda_deploy::Manifest &manifest)
{
    QStringList rels;
    for (const auto &f : manifest.files) rels << f.rel;
    const QStringList dlls = dll_overrides::wrapperDllsIn(rels);
    if (dlls.isEmpty()) return {};

    const QString regPath = resolveUserRegPath(adapter, dataDir);
    if (regPath.isEmpty()) return {};

    QFile in(regPath);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QString before = QString::fromUtf8(in.readAll());
    in.close();

    QStringList added;
    const QString after = dll_overrides::addOverrides(before, dlls, &added);
    if (added.isEmpty()) return {};             // already set - nothing to undo

    // QSaveFile: user.reg holds the whole prefix's configuration, so a partial
    // write would be far worse than not writing at all.
    QSaveFile out(regPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    out.write(after.toUtf8());
    if (!out.commit()) return {};
    return added;
}

// Take back exactly the overrides a deployment added.  Names the user had set
// themselves were never recorded, so they survive.
static QStringList bethesdaUnregisterDllOverrides(const GameAdapter *adapter,
                                                  const QString &dataDir,
                                                  const QStringList &dlls)
{
    if (dlls.isEmpty()) return {};
    const QString regPath = resolveUserRegPath(adapter, dataDir);
    if (regPath.isEmpty()) return {};

    QFile in(regPath);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QString before = QString::fromUtf8(in.readAll());
    in.close();

    QStringList removed;
    const QString after = dll_overrides::removeOverrides(before, dlls, &removed);
    if (removed.isEmpty()) return {};

    QSaveFile out(regPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    out.write(after.toUtf8());
    if (!out.commit()) return {};
    return removed;
}

// The folder the game-root pass deploys into: beside the game .exe for most
// games (Data/'s parent), Oblivion Remastered's install folder for that one.
static QString bethesdaGameRoot(const GameAdapter *adapter, const QString &dataDir)
{
    if (dataDir.isEmpty()) return {};
    const QString up = adapter ? adapter->gameRootFromData() : QStringLiteral("..");
    return QDir::cleanPath(dataDir + QLatin1Char('/') + up);
}

static root_routes::Routes bethesdaRoutes(const GameAdapter *adapter)
{
    if (!adapter) return {};
    return {adapter->pakModsSubdir(), adapter->extenderPluginsSubdir()};
}

// Resolve the prefix Plugins.txt path (active set / order).  Manual override
// wins; else AppData/Local/<localAppDataName>/Plugins.txt under the first prefix
// that exists; else the best-guess derived path (caller mkpaths the parent).
static QString resolveBethesdaPluginsTxt(const QString &id,
                                         const GameAdapter *adapter,
                                         const QString &dataDir)
{
    const QString override = Settings::pluginsTxtPath(id);
    if (!override.isEmpty()) return override;
    if (adapter && adapter->pluginsTxtInDataDir())
        return dataDir.isEmpty() ? QString() : QDir(dataDir).filePath(QStringLiteral("Plugins.txt"));
    const QString name = adapter ? adapter->localAppDataName() : QString();
    if (name.isEmpty()) return {};

    QString firstCandidate;
    for (const QString &pu : bethesdaPrefixUserDirs(adapter, dataDir)) {
        const QString dir = proton::localAppData(pu, name);
        const QString plugins = dir + "/Plugins.txt";
        if (firstCandidate.isEmpty()) firstCandidate = plugins;
        if (QFileInfo::exists(plugins) || QDir(dir).exists())
            return plugins;   // prefix exists: the game has run at least once
    }
    return firstCandidate;    // best guess; caller mkpaths the parent
}

// Resolve the prefix "Documents/My Games/<myGamesName>" dir, where the engine
// .ini lives.  Manual override (Settings::iniDir, shared with the Skyrim INI
// tuner) wins; else the first existing My-Games dir across the layout variants;
// else the best guess.
static QString resolveBethesdaIniDir(const QString &id,
                                     const GameAdapter *adapter,
                                     const QString &dataDir)
{
    const QString override = Settings::iniDir(id);
    if (!override.isEmpty() && QDir(override).exists()) return override;
    const QString name = adapter ? adapter->myGamesName() : QString();
    if (name.isEmpty()) return {};

    QString firstCandidate;
    for (const QString &pu : bethesdaPrefixUserDirs(adapter, dataDir))
        for (const QString &dir : proton::myGamesDirs(pu, name)) {
            if (firstCandidate.isEmpty()) firstCandidate = dir;
            if (QDir(dir).exists()) return dir;
        }
    return firstCandidate;
}

// Restore a file we replaced from its one-time ".nerevarine-bak", consuming the
// backup so a later deploy re-backs-up the restored original afresh.
static void restoreNerevarineBak(const QString &path)
{
    const QString bak = path + QStringLiteral(".nerevarine-bak");
    if (!QFileInfo::exists(bak)) return;
    QFile::remove(path);
    if (!QFile::rename(bak, path) && QFile::copy(bak, path))
        QFile::remove(bak);
}

// Bethesda engines keep their content in Data/ and their active plugins in
// Plugins.txt; the Souls games overlay the exe's own folder and have neither.
// The prompts have to know which, or they walk the user through steps that
// never happen ("locate the folder with the .esm files" for a game that has no
// .esm at all).  Picks the "_overlay" variant of a string for those.
static bool writesPluginList(const GameAdapter *adapter)
{
    return adapter && listsPluginsTxt(adapter->loadOrderStyle());
}

static QString deployText(const GameAdapter *adapter, const char *key)
{
    const QString k = QString::fromLatin1(key);
    return writesPluginList(adapter) ? T(k) : T(k + QStringLiteral("_overlay"));
}

// Resolve a deployable profile's data dir: Settings override, else the located
// install + the adapter's data subdir ("Data", or "." for the Souls games),
// else (allowPrompt) a folder picker.  Persists whatever it settles on; empty
// if unresolved.
static QString bethesdaResolveDataDir(QWidget *parent, const QString &id,
                                      const GameAdapter *adapter, bool allowPrompt)
{
    auto ok = [](const QString &d) { return !d.isEmpty() && QDir(d).exists(); };
    // Where the stores (GOG, then Steam) say the game is now.
    auto storeData = [&]() -> QString {
        QString exe = GameProfileRegistry::findGogGameExe(id, /*wantLauncher=*/false);
        if (exe.isEmpty()) exe = GameProfileRegistry::findSteamGameExe(id);
        if (exe.isEmpty() || !adapter) return {};
        // cleanPath so a "." subdir (Souls games load out of the exe's own
        // folder) resolves to that folder rather than a trailing "/.".
        return QDir::cleanPath(QFileInfo(exe).absolutePath() + "/" + adapter->dataSubdir());
    };
    QString dataDir = Settings::dataDir(id);
    if (!ok(dataDir)) {
        dataDir = storeData();
    } else if (adapter) {
        // A saved folder that still exists but no longer holds the game: the
        // game was reinstalled somewhere else, and an uninstall leaves its
        // old folder standing. Follow it; the next deploy takes the mods
        // back out of the old one (Settings::movedFromDataDir).
        const QStringList exes = gameExeNames(adapter);
        if (!bethesda_deploy::missingGameExe(bethesdaGameRoot(adapter, dataDir), exes).isEmpty()) {
            const auto loc = bethesda_deploy::followMovedGame(
                dataDir, storeData(), exes, adapter->gameRootFromData());
            if (!loc.movedFrom.isEmpty() && ok(loc.dataDir)) {
                qCInfo(logging::lcLaunch).noquote()
                    << "game" << id << "moved:" << loc.movedFrom << "->" << loc.dataDir;
                // The first old folder is the one holding the deployment.
                if (Settings::movedFromDataDir(id).isEmpty())
                    Settings::setMovedFromDataDir(id, loc.movedFrom);
                dataDir = loc.dataDir;
            }
        }
    }
    if (!ok(dataDir) && allowPrompt) {
        ui::info(parent, T("deploy_title"), deployText(adapter, "deploy_locate_data"));
        dataDir = QFileDialog::getExistingDirectory(
            parent, deployText(adapter, "deploy_locate_data_dialog"), QDir::homePath());
    }
    if (!ok(dataDir)) return {};
    Settings::setDataDir(id, dataDir);
    return dataDir;
}

// Per game+modlist-profile deploy state paths, keyed by the modlist filename so
// separate profiles don't share a manifest.
static void bethesdaStatePaths(const QString &modlistFile,
                               QString &manifestPath, QString &backupDir)
{
    const QString key = QFileInfo(modlistFile).completeBaseName();
    manifestPath = resolveUserStatePath("deploy_manifest_" + key + ".json");
    backupDir    = resolveUserStatePath("deploy_backup_" + key);
}

// A SECOND manifest, for the handful of files that belong beside the game .exe
// rather than inside Data/.
//
// A manifest's paths are relative to the root it was deployed into, and
// deploy() refuses a rel starting with ".." so a mod can never escape Data/.
// Script-extender loaders have to land one level up, so they get their own
// root, their own backup store and their own manifest instead of weakening
// that rule. Undeploy walks both.
static void bethesdaRootStatePaths(const QString &modlistFile,
                                   QString &manifestPath, QString &backupDir)
{
    const QString key = QFileInfo(modlistFile).completeBaseName();
    manifestPath = resolveUserStatePath("deploy_manifest_" + key + ".root.json");
    backupDir    = resolveUserStatePath("deploy_backup_" + key + "__root");
}

// What the deployment beside it was made from - bethesda_deploy::
// sourceFingerprint, or the word "changed" once markDeployStale has said a
// mod's files moved under it. Absent for deployments made before it existed.
static QString bethesdaFingerprintPath(const QString &modlistFile)
{
    const QString key = QFileInfo(modlistFile).completeBaseName();
    return resolveUserStatePath("deploy_manifest_" + key + ".fingerprint");
}

static QString readSmallFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.read(256)).trimmed();
}

static void writeSmallFile(const QString &path, const QString &text)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(text.toUtf8());
    f.commit();
}

// Mods that ship a script-extender loader, as sources for the game-root deploy.
//
// SKSE (and OBSE/F4SE/…) put skse64_loader.exe plus its runtime DLL beside the
// game .exe, and their Papyrus scripts in Data/. The normal gather only knows
// about Data/, so the loader was silently dropped: 7483 files deployed and not
// one .exe. Nothing reported it, and the game simply started without SKSE.
//
// Only root-level .exe and .dll are taken. The readme and the src/ tree that
// SKSE also ships are not the game's business, and a recursive sweep here
// would additionally re-deploy the mod's own Data/ into the game root.
static QList<bethesda_deploy::DeploySource> gatherScriptExtenderSources(
    const ModlistModel &model, const QList<int> &order, const GameAdapter *adapter,
    const QString &modsDir)
{
    QList<bethesda_deploy::DeploySource> out;
    if (!adapter) return out;
    const QStringList loaders = adapter->scriptExtenderLoaders();
    const root_routes::Routes routes = bethesdaRoutes(adapter);
    if (loaders.isEmpty() && routes.pakModsSubdir.isEmpty()
        && routes.extenderPluginsSubdir.isEmpty()) return out;

    for (int i : order) {
        const ModEntry &item = model.at(i);
        if (!item.isMod() || !item.checked || !item.isInstalled()) continue;
        if (item.modPath.isEmpty() || !QDir(item.modPath).exists()) continue;

        // Same data-root repair as the Data/ gather: a mod installed one level
        // too deep would otherwise hide its own root files.
        const QString modPath = install_layout::deployRoot(item.modPath, modsDir);

        QString label = item->data(ModRole::CustomName).toString();
        if (label.isEmpty()) label = item->text();

        // Pak mods and a relocated OBSE/ folder, wherever the mod keeps them.
        out += root_routes::sourcesFor(label, modPath, routes);

        const QDir dir(modPath);
        const QStringList rootFiles =
            dir.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        // Matched by file name: a loader's own path is where it GOES
        // (Oblivion Remastered's sits in Binaries/Win64), while mods ship it
        // at their top level.
        QString loaderDir;
        bool hasLoader = false;
        for (const QString &l : loaders)
            if (rootFiles.contains(QFileInfo(l).fileName(), Qt::CaseInsensitive)) {
                hasLoader = true;
                loaderDir = QFileInfo(l).path();
                break;
            }
        if (!hasLoader) continue;

        QStringList payload;
        for (const QString &f : rootFiles)
            if (f.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)
             || f.endsWith(QLatin1String(".dll"), Qt::CaseInsensitive))
                payload << f;
        if (payload.isEmpty()) continue;

        out.append({label, modPath, payload,
                    loaderDir == QLatin1String(".") ? QString() : loaderDir});
    }
    return out;
}

// Other game profiles whose persisted data dir is this same folder.
//
// Deploy state is per game+modlist-profile, so two games pointed at one install
// keep two manifests that know nothing of each other: the second deploy sees
// the first's linked files as vanilla and backs them up as such, and undeploying
// either then "restores" the other's mod files over the real originals.
//
// Nothing prevented this before, but nothing tripped it either - the pairs that
// share a folder (Skywind/Skyblivion on Skyrim SE) are not deploy-classified.
// Skyrim SE and Skyrim AE are the first two that are AND auto-detect to the
// identical install, since AE is literally SE plus Creation Club content.
static QStringList gamesSharingDataDir(const QList<GameProfile> &games,
                                       const QString &id, const QString &dataDir)
{
    QStringList out;
    if (dataDir.isEmpty()) return out;
    const QString target = QDir::cleanPath(dataDir);
    for (const GameProfile &gp : games) {
        if (gp.id == id) continue;
        const QString other = Settings::dataDir(gp.id);
        if (other.isEmpty()) continue;
        if (QDir::cleanPath(other) == target) out << gp.displayName;
    }
    return out;
}

// Revert a deployment: undeploy the manifest (restoring displaced vanilla
// files), restore the Plugins.txt / Oblivion.ini we backed up on deploy, and
// clear the manifest so nothing is "deployed" any more.  Returns the summary.
static QString bethesdaApplyUndeploy(const QString &id, const GameAdapter *adapter,
                                     const QString &dataDir, const QString &modlistFile,
                                     int *removedOut)
{
    QString manifestPath, backupDir;
    bethesdaStatePaths(modlistFile, manifestPath, backupDir);
    QFile mf(manifestPath);
    if (!mf.open(QIODevice::ReadOnly | QIODevice::Text))
        return T("undeploy_none");
    const auto man = bethesda_deploy::manifestFromJson(QString::fromUtf8(mf.readAll()));
    mf.close();

    auto u = bethesda_deploy::undeploy(dataDir, backupDir, man);

    // The game-root pass (script-extender loaders) has its own manifest and
    // has to come out too, or "Remove deployed mods" leaves skse64_loader.exe
    // sitting next to the game .exe forever. Folded into the same counts so
    // the summary still reports one number.
    {
        QString rootManifestPath, rootBackupDir;
        bethesdaRootStatePaths(modlistFile, rootManifestPath, rootBackupDir);
        QFile rf(rootManifestPath);
        if (rf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const auto rootMan =
                bethesda_deploy::manifestFromJson(QString::fromUtf8(rf.readAll()));
            rf.close();
            const auto ru = bethesda_deploy::undeploy(bethesdaGameRoot(adapter, dataDir),
                                                      rootBackupDir, rootMan);
            u.removed  += ru.removed;
            u.restored += ru.restored;
            u.errors   += ru.errors;
            QFile::remove(rootManifestPath);
        }
    }

    const QStringList unset =
        bethesdaUnregisterDllOverrides(adapter, dataDir, man.dllOverrides);

    // The generated -game: ini is ours and is not in the manifest (nothing
    // deployed it), so it has to be taken back out by name. Leaving it would
    // point the engine at a VDF list of archives that are no longer there.
    if (adapter && adapter->isOpenGothic())
        QFile::remove(QDir(dataDir).filePath(QStringLiteral("system/")
                                             + opengothic::generatedIniName()));

    const QString pluginsTxt = resolveBethesdaPluginsTxt(id, adapter, dataDir);
    if (!pluginsTxt.isEmpty()) restoreNerevarineBak(pluginsTxt);
    if (id == QLatin1String("oblivion")) {
        const QString iniDir = resolveBethesdaIniDir(id, adapter, dataDir);
        if (!iniDir.isEmpty())
            restoreNerevarineBak(QDir(iniDir).filePath("Oblivion.ini"));
    }
    QFile::remove(manifestPath);
    QFile::remove(bethesdaFingerprintPath(modlistFile));
    if (removedOut) *removedOut = u.removed;
    QString summary = T("undeploy_done").arg(u.removed).arg(u.restored).arg(u.errors.size());
    if (!unset.isEmpty())
        summary += QStringLiteral("\n\n")
                 + T("undeploy_dll_overrides").arg(unset.join(QStringLiteral(", ")));
    return summary;
}

// The game moved (see bethesdaResolveDataDir): take this list's deployment
// back out of the folder it was made into, before anything goes into the new
// one. Against the OLD root - undeployed against the new one, the old
// install's backed-up originals would be "restored" over the fresh game's
// files. An old folder that is gone already has nothing to take out; its
// manifests go, the backup store (the user's own files) stays. Returns how
// many files came out.
static int bethesdaLeaveOldFolder(const QString &id, const GameAdapter *adapter,
                                  const QString &oldDataDir, const QString &modlistFile)
{
    int removed = 0;
    if (QDir(oldDataDir).exists()) {
        bethesdaApplyUndeploy(id, adapter, oldDataDir, modlistFile, &removed);
    } else {
        QString manifestPath, backupDir, rootManifestPath, rootBackupDir;
        bethesdaStatePaths(modlistFile, manifestPath, backupDir);
        bethesdaRootStatePaths(modlistFile, rootManifestPath, rootBackupDir);
        QFile::remove(manifestPath);
        QFile::remove(rootManifestPath);
        QFile::remove(bethesdaFingerprintPath(modlistFile));
    }
    qCInfo(logging::lcLaunch).noquote() << "deploy: took" << removed
                                        << "file(s) out of the old folder" << oldDataDir;
    return removed;
}

// Enabled, installed mods' data roots in load order (top to bottom; later
// overrides earlier).  collectDataFolders finds plugin-bearing roots;
// collectResourceFolders catches pure asset mods (retextures); de-nested so a
// root nested inside another isn't deployed at the wrong relative path.
//
// Overlay engines (dataSubdir ".", the Souls games) skip that detection
// entirely: there is no data root to find, because the mod's top level IS the
// game folder's top level - that is exactly how those mods are packaged
// ("copy these into your Game folder"). Running the detection there would be
// actively wrong. collectResourceFolders keys off OpenMW asset names, so a mod
// that happens to contain, say, shader/shaders/ would be deployed from the
// inner folder: everything at the wrong depth, and the wrapper .dll sitting
// beside it dropped on the floor.
// `order` is the persisted row order (DeployFlow::rowOrderForPersist): a
// temporary Size/Date view sort reorders the rows on screen, and deploying in
// that order would hand every conflict to whichever mod happens to sort last.
static QList<bethesda_deploy::DeploySource> gatherBethesdaSources(const ModlistModel &model,
                                                                  const QList<int> &order,
                                                                  const GameAdapter *adapter,
                                                                  const QString &modsDir)
{
    static const QStringList kExts{".esp", ".esm", ".esl"};
    const bool overlay = adapter && adapter->overlayDeploy();
    QList<bethesda_deploy::DeploySource> sources;
    for (int i : order) {
        const ModEntry &item = model.at(i);
        if (!item.isMod() || !item.checked || !item.isInstalled()) continue;
        const QString modPath = item.modPath;
        if (modPath.isEmpty() || !QDir(modPath).exists()) continue;

        if (overlay) {
            const QString label = item.effectiveName();
            // Gothic mods come in two shapes: the game's own layout (Data/,
            // system/), which overlays unchanged, and a bare archive with
            // nothing around it, which has to be routed into Data/ or the
            // engine never scans past it. See opengothic::mapModFolder.
            if (adapter && adapter->isOpenGothic()) {
                const auto m = opengothic::mapModFolder(modPath);
                if (!m.overlay) {
                    if (!m.archives.isEmpty())
                        sources.append({label, modPath, m.archives, QStringLiteral("Data")});
                    if (!m.inis.isEmpty())
                        sources.append({label, modPath, m.inis, QStringLiteral("system")});
                    continue;
                }
            }
            sources.append({label, modPath});
            continue;
        }

        // A mod whose own root IS a Data-level folder deploys from its parent,
        // or the folder name is lost.
        //
        // install_layout::diveTarget used to dive into a lone top-level SKSE/
        // (its no-dive list was OpenMW-only), so Address Library installed with
        // its root at <mod>/SKSE. Deploying THAT merges its contents into Data/
        // and the files land in Data/Plugins/ rather than Data/SKSE/Plugins/,
        // where no SKSE plugin looks. diveTarget knows better now, but existing
        // installs still carry the deep path and re-downloading does not move
        // it, so repair it here rather than making the user reinstall.
        //
        // Guarded on the parent not being the mods dir itself: climbing out of
        // a top-level mod folder that merely happened to be named "meshes"
        // would deploy every mod in the library at once.
        const QString effectiveRoot = install_layout::deployRoot(modPath, modsDir);

        QStringList roots;
        for (const auto &p : plugins::collectDataFolders(effectiveRoot, kExts))
            roots << p.first;
        roots += plugins::collectResourceFolders(effectiveRoot);
        if (roots.isEmpty()) {
            // A pak-only or OBSE-plugin-only mod goes entirely through the
            // game-root pass; dumping its folder into Data/ as well would only
            // litter the game's data with files nothing reads there.
            if (root_routes::onlyRoutedContent(effectiveRoot, bethesdaRoutes(adapter)))
                continue;
            roots << effectiveRoot;  // fallback: the folder itself
        }
        roots.removeDuplicates();

        std::sort(roots.begin(), roots.end(),
                  [](const QString &a, const QString &b) { return a.size() < b.size(); });
        QStringList topMost;
        for (const QString &r : roots) {
            bool nested = false;
            for (const QString &k : topMost)
                if (r == k || r.startsWith(k + "/")) { nested = true; break; }
            if (!nested) topMost << r;
        }
        QString label = item->data(ModRole::CustomName).toString();
        if (label.isEmpty()) label = item->text();
        for (const QString &r : topMost) sources.append({label, r});
    }
    return sources;
}

// The game's own executables, as the adapter knows them: its Steam build and
// its GOG ones, relative to the game folder.
static QStringList gameExeNames(const GameAdapter *adapter)
{
    QStringList names;
    if (const QString exe = adapter->steamLayout().exe; !exe.isEmpty()) names << exe;
    for (const auto &g : adapter->gogLayouts())
        if (!g.exe.isEmpty() && !names.contains(g.exe, Qt::CaseInsensitive)) names << g.exe;
    return names;
}

// "A, B, C and 4 more": a list that fits on a line of a message box.
static QString namesAndMore(const QStringList &names, int shown)
{
    QString s = names.mid(0, shown).join(QStringLiteral(", "));
    if (names.size() > shown)
        s += QLatin1Char(' ') + T("list_and_more").arg(names.size() - shown);
    return s;
}

// Write the load order for the plugins just linked into Data/: Plugins.txt, and
// for the timestamp engines (Oblivion/FO3/FNV) the files' mtimes. Masters first,
// each plugin after its own masters, and none whose master is not installed
// (bethesda_loadorder::settle). Returns what it activated and what it left out;
// a no-op for games without a plugin list.
static ActivateResult bethesdaActivate(const QString &id, const GameAdapter *adapter,
                                       const QString &dataDir,
                                       const bethesda_deploy::Manifest &manifest,
                                       const QStringList &loadOrder)
{
    ActivateResult result;
    const LoadOrderStyle style = adapter->loadOrderStyle();
    if (!writesPluginList(adapter)) return result;
    // By name in any case, as the game matches them: the list spells a
    // plugin as its mod does, Data/ as the deploy settled on (one spelling
    // per name), and a translation's respelt plugin is the same plugin.
    QHash<QString, QString> active;   // lower-cased -> as placed in Data/
    for (const auto &f : manifest.files) {
        if (f.rel.contains('/')) continue;        // plugins load from Data/ root only
        const QString low = f.rel.toLower();
        if (low.endsWith(".esp") || low.endsWith(".esm") || low.endsWith(".esl"))
            active.insert(low, f.rel);
    }
    QStringList ordered;
    QSet<QString> placed;   // lower-cased
    for (const QString &p : loadOrder) {
        const QString low = p.toLower();
        const auto it = active.constFind(low);
        if (it == active.cend() || placed.contains(low)) continue;
        ordered << it.value();
        placed.insert(low);
    }
    for (const auto &f : manifest.files) {
        const QString low = f.rel.toLower();
        if (!active.contains(low) || placed.contains(low)) continue;
        ordered << f.rel;
        placed.insert(low);
    }
    ordered = bethesda_loadorder::mastersFirst(ordered);

    // Masters, read from the files as they now sit in Data/. A plugin whose
    // master is in no file there would stop the game while it loads, so it is
    // left out; what Data/ holds of the game's own (masters, DLC, Creation
    // Club, files put there by hand) counts as present, and nothing else is
    // assumed about it.
    {
        static const QStringList kPlugins = {QStringLiteral("*.esp"),
                                             QStringLiteral("*.esm"),
                                             QStringLiteral("*.esl")};
        const QDir data(dataDir);
        QSet<QString> present;
        for (const QString &f : data.entryList(kPlugins, QDir::Files))
            present.insert(f.toLower());
        QHash<QString, QStringList> mastersOf;
        for (const QString &p : std::as_const(ordered)) {
            const plugins::Masters m = plugins::readMasters(data.filePath(p));
            if (m.ok) mastersOf.insert(p.toLower(), m.names);
        }
        const bethesda_loadorder::Settled settled =
            bethesda_loadorder::settle(ordered, mastersOf, present);
        ordered = settled.order;

        QHash<QString, QString> modOf;
        for (const auto &f : manifest.files) modOf.insert(f.rel.toLower(), f.sourceMod);
        for (const auto &h : settled.held) {
            result.held.append({h.plugin, h.missing, h.heldMasters,
                                modOf.value(h.plugin.toLower())});
            QStringList why;
            if (!h.missing.isEmpty())
                why << h.missing.join(QStringLiteral(", ")) + QStringLiteral(" (not installed)");
            if (!h.heldMasters.isEmpty())
                why << h.heldMasters.join(QStringLiteral(", ")) + QStringLiteral(" (left out too)");
            qCWarning(logging::lcLaunch).noquote()
                << "deploy: left" << h.plugin << "out of the load order - it needs"
                << why.join(QStringLiteral("; "));
        }
        result.missingFiles = bethesda_loadorder::missingMasters(settled.held);
    }
    if (ordered.isEmpty()) return result;

    const QString pluginsTxt = resolveBethesdaPluginsTxt(id, adapter, dataDir);
    int activated = 0;
    QString body;
    if (style == LoadOrderStyle::PlainListPluginsTxt) {
        // Added to the game's own list, never written over it. The base is
        // the file as it was before our first deploy, so a mod removed since
        // drops out; with no backup yet, the file on disk is that original.
        QString original;
        const QString pBak = pluginsTxt + QStringLiteral(".nerevarine-bak");
        QFile of(QFileInfo::exists(pBak) ? pBak : pluginsTxt);
        if (!pluginsTxt.isEmpty() && of.open(QIODevice::ReadOnly)) {
            original = QString::fromUtf8(of.readAll());
            of.close();
        }
        // No list to add to means we are not looking at the game's Data/:
        // writing a fresh one would load the mods and none of the game.
        if (original.trimmed().isEmpty()) return result;
        activated = int(ordered.size());
        body = bethesda_loadorder::mergedPluginsTxtContent(original, ordered);
    } else if (style == LoadOrderStyle::TimestampPluginsTxt) {
        // Oblivion/FO3/FNV: load order is file mtime in Data/; Plugins.txt is the
        // plain active list.
        const qint64 step = 2000;
        const qint64 base = QDateTime::currentMSecsSinceEpoch() - qint64(ordered.size()) * step;
        activated = bethesda_loadorder::applyTimestampOrder(dataDir, ordered, base, step).stamped;
        body = bethesda_loadorder::pluginsTxtContent(ordered);
    } else {
        // Skyrim SE/FO4: the Plugins.txt order *is* the load order; active
        // plugins are '*'-prefixed.  No mtime stamping needed.
        activated = int(ordered.size());
        body = bethesda_loadorder::asteriskPluginsTxtContent(ordered);
    }

    if (!pluginsTxt.isEmpty()) {
        QDir().mkpath(QFileInfo(pluginsTxt).absolutePath());
        const QString pBak = pluginsTxt + ".nerevarine-bak";
        if (QFileInfo::exists(pluginsTxt) && !QFileInfo::exists(pBak))
            QFile::copy(pluginsTxt, pBak);
        QFile pf(pluginsTxt);
        if (pf.open(QIODevice::WriteOnly)) {   // binary: keep CRLFs verbatim
            pf.write(body.toUtf8());
            pf.close();
            Settings::setPluginsTxtPath(id, pluginsTxt);
        }
    }
    result.activated = activated;
    return result;
}

// Rewrite one game ini through `transform`, backing the original up once.
//
// `createIfMissing` is the difference between the two games: Oblivion.ini ships
// with the game, so a missing one means we resolved the wrong directory and
// must not invent it. StarfieldCustom.ini is a user override file Starfield
// does NOT ship, so the common case is that we have to create it.
static bool rewriteGameIni(const QString &iniPath, bool createIfMissing,
                           const std::function<QString(const QString &)> &transform)
{
    if (iniPath.isEmpty()) return false;
    const bool exists = QFileInfo::exists(iniPath);
    if (!exists && !createIfMissing) return false;

    QString iniText;
    if (exists) {
        QFile inf(iniPath);
        if (inf.open(QIODevice::ReadOnly)) {
            iniText = QString::fromUtf8(inf.readAll());
            inf.close();
        }
        const QString iniBak = iniPath + ".nerevarine-bak";
        if (!QFileInfo::exists(iniBak)) QFile::copy(iniPath, iniBak);
    } else {
        QDir().mkpath(QFileInfo(iniPath).absolutePath());
    }

    QFile outf(iniPath);
    if (!outf.open(QIODevice::WriteOnly)) return false;   // binary: keep CRLFs verbatim
    outf.write(transform(iniText).toUtf8());
    outf.close();
    return true;
}

// Make the deployed mods' assets actually load. Both supported engines need an
// ini nudge, for different reasons:
//   Oblivion  - a .bsa is invisible until it is listed in SArchiveList, and
//               loose replacers lose to the vanilla BSA without invalidation.
//   Starfield - loose files in Data/ are ignored outright until archive
//               invalidation is on; .ba2 matching a plugin name auto-loads.
// Returns the deployed archives the engine will NOT load by itself, for the
// caller to surface. Empty for engines with no archive config.
static QStringList bethesdaConfigureArchives(const QString &id, const GameAdapter *adapter,
                                             const QString &dataDir,
                                             const bethesda_deploy::Manifest &manifest)
{
    if (!adapter) return {};
    // Driven entirely by adapter data. It used to be `if (id != "oblivion")
    // return;`, which silently skipped every other engine: Fallout 4 deployed
    // its mods and then loaded none of the loose ones because nothing ever
    // wrote Fallout4Custom.ini.
    using Style = GameAdapter::ArchiveConfig::Style;
    const GameAdapter::ArchiveConfig cfg = adapter->archiveConfig();
    if (cfg.style == Style::None || cfg.iniName.isEmpty()) return {};

    const QString iniDir = resolveBethesdaIniDir(id, adapter, dataDir);
    if (iniDir.isEmpty()) return {};

    // Plugins and archives load from Data/ root only.
    QStringList archives, plugins;
    for (const auto &f : manifest.files) {
        if (f.rel.contains('/')) continue;
        if (!cfg.archiveSuffix.isEmpty()
            && f.rel.endsWith(cfg.archiveSuffix, Qt::CaseInsensitive)) {
            archives << f.rel;
        } else if (f.rel.endsWith(QLatin1String(".esp"), Qt::CaseInsensitive)
                || f.rel.endsWith(QLatin1String(".esm"), Qt::CaseInsensitive)
                || f.rel.endsWith(QLatin1String(".esl"), Qt::CaseInsensitive)) {
            plugins << f.rel;
        }
    }

    const QString iniPath = QDir(iniDir).filePath(cfg.iniName);
    bool wrote = false;
    QStringList stray;
    if (cfg.style == Style::GamebryoArchiveList) {
        wrote = rewriteGameIni(iniPath, cfg.createIfMissing,
            [&archives, &cfg](const QString &t) {
                return bethesda_archives::configureArchives(t, archives, cfg.vanillaSeed);
            });
    } else {
        // Report archives the engine will not auto-load rather than registering
        // them: the archive-list keys replace the base value, so writing one
        // would unload every vanilla archive we failed to re-list.
        stray = bethesda_custom_ini::strayArchives(archives, plugins);
        wrote = rewriteGameIni(iniPath, cfg.createIfMissing,
            &bethesda_custom_ini::configureCustomIni);
    }
    if (wrote) Settings::setIniDir(id, iniDir);
    return stray;
}

// -- OpenGothic (Gothic II) --------------------------------------------
//
// The step without which a Gothic deploy does nothing at all.
//
// Copying a mod's Data/ and system/ into the game folder is only half of it:
// the engine throws away every .mod archive that is not named in the [FILES]
// VDF list of the ini it was started with, and with no ini at all it throws
// away ALL of them (see opengothic.h). So the ini is generated here from what
// was actually deployed, and conflicts between archives are settled by stamping
// their headers in list order, because that is the only thing the engine looks
// at when two archives carry the same file.
//
// Returns a line for the deploy summary, empty for every other engine.
static QString opengothicActivate(const GameAdapter *adapter, const QString &gameRoot,
                                  const bethesda_deploy::Manifest &manifest)
{
    if (!adapter || !adapter->isOpenGothic()) return {};

    // Manifest order IS load order: deploy walks the sources top to bottom.
    QStringList rels;
    rels.reserve(manifest.files.size());
    for (const auto &f : manifest.files) rels << f.rel;

    const auto plan = opengothic::planActivation(gameRoot, rels);
    if (plan.isEmpty()) return {};

    const auto stamps = opengothic::applyOrder(plan.archivePaths);

    const opengothic::ModIni merged =
        opengothic::mergeModInis(plan.modInis, plan.archiveNames);
    const QString iniPath = QDir(gameRoot).filePath(QStringLiteral("system/")
                                                    + opengothic::generatedIniName());
    QDir().mkpath(QFileInfo(iniPath).absolutePath());
    bool wrote = false;
    QFile out(iniPath);
    if (out.open(QIODevice::WriteOnly)) {      // binary: the file is CRLF
        out.write(opengothic::buildModIni(merged).toUtf8());
        out.close();
        wrote = true;
    }

    QString note = T("deploy_gothic_ini")
                       .arg(QString::number(merged.vdf.size()),
                            opengothic::generatedIniName(),
                            QString::number(stamps.stamped));
    if (!wrote) note = T("deploy_gothic_ini_failed").arg(iniPath);
    if (!plan.unusable.isEmpty())
        note += QStringLiteral("\n") + T("deploy_gothic_space_in_name")
                                            .arg(plan.unusable.join(QStringLiteral(", ")));
    if (!stamps.errors.isEmpty())
        note += QStringLiteral("\n") + stamps.errors.join(QStringLiteral("\n"));
    return note;
}

// Undeploy the previous manifest, deploy `sources`, persist the new manifest,
// then activate (load order + Plugins.txt) and configure archives.  Returns the
// human summary and what was left out.  Shared by the Deploy action and
// deploy-on-launch.
static DeployOutcome bethesdaApplyDeploy(const QString &id, const GameAdapter *adapter,
                                   const QString &dataDir,
                                   const QList<bethesda_deploy::DeploySource> &sources,
                                   const QList<bethesda_deploy::DeploySource> &rootSources,
                                   const QString &modlistFile, const QStringList &loadOrder,
                                   const QString &fingerprint,
                                   const bethesda_deploy::ProgressFn &progress,
                                   const QString &movedFrom)
{
    QString manifestPath, backupDir;
    bethesdaStatePaths(modlistFile, manifestPath, backupDir);

    // The game moved: the previous deployment is in the old folder.
    QString movedNote;
    if (!movedFrom.isEmpty()) {
        const int out = bethesdaLeaveOldFolder(id, adapter, movedFrom, modlistFile);
        movedNote = T("deploy_moved_from").arg(adapter->displayName(),
                                               QDir::toNativeSeparators(dataDir),
                                               QDir::toNativeSeparators(movedFrom))
                                          .arg(out);
    }

    // Game-root pass (script-extender loaders). Its own root, backup store and
    // manifest - see bethesdaRootStatePaths. Undeployed first for the same
    // reason as the Data/ one: take the old set out before placing the new.
    QString rootManifestPath, rootBackupDir;
    bethesdaRootStatePaths(modlistFile, rootManifestPath, rootBackupDir);
    const QString gameRoot = bethesdaGameRoot(adapter, dataDir);
    int rootPlaced = 0;
    {
        QFile prevRoot(rootManifestPath);
        if (prevRoot.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const auto prev = bethesda_deploy::manifestFromJson(
                QString::fromUtf8(prevRoot.readAll()));
            prevRoot.close();
            bethesda_deploy::undeploy(gameRoot, rootBackupDir, prev);
        }
        if (!rootSources.isEmpty() && !gameRoot.isEmpty()) {
            const auto rootRes =
                bethesda_deploy::deploy(gameRoot, rootBackupDir, rootSources);
            rootPlaced = rootRes.filesDeployed;
            QFile outRoot(rootManifestPath);
            if (outRoot.open(QIODevice::WriteOnly | QIODevice::Text)) {
                outRoot.write(bethesda_deploy::manifestToJson(rootRes.manifest).toUtf8());
                outRoot.close();
            }
        } else {
            QFile::remove(rootManifestPath);
        }
    }

    QFile prevFile(manifestPath);
    if (prevFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const auto prev = bethesda_deploy::manifestFromJson(
            QString::fromUtf8(prevFile.readAll()));
        prevFile.close();
        // Taking the old deployment back out is half the work on a re-deploy,
        // so it reports too - otherwise the bar sits at zero through it.
        bethesda_deploy::undeploy(dataDir, backupDir, prev, progress);
        // Clear the previous run's overrides too, so this one re-adds them and
        // records them afresh. Skipping it would leak: addOverrides leaves an
        // existing entry alone, so the new manifest would list nothing and the
        // eventual undeploy would leave the override behind for good.
        bethesdaUnregisterDllOverrides(adapter, dataDir, prev.dllOverrides);
    }
    auto res = bethesda_deploy::deploy(dataDir, backupDir, sources,
                                       bethesda_deploy::LinkMethod::Hardlink,
                                       progress);
    // Before the manifest is written: a wrapper DLL we placed is inert under
    // Proton until the prefix is told to prefer it, and the manifest is what
    // undeploy reads to take the override back out again.
    res.manifest.dllOverrides = bethesdaRegisterDllOverrides(adapter, dataDir, res.manifest);
    QFile outFile(manifestPath);
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        outFile.write(bethesda_deploy::manifestToJson(res.manifest).toUtf8());
        outFile.close();
    }
    writeSmallFile(bethesdaFingerprintPath(modlistFile), fingerprint);
    const ActivateResult act = bethesdaActivate(id, adapter, dataDir, res.manifest, loadOrder);
    const QStringList stray =
        bethesdaConfigureArchives(id, adapter, dataDir, res.manifest);
    const QString gothicNote = opengothicActivate(adapter, dataDir, res.manifest);

    QString summary = writesPluginList(adapter)
        ? T("deploy_done").arg(res.filesDeployed).arg(res.vanillaBackedUp)
                          .arg(act.activated).arg(res.errors.size())
        : T("deploy_done_overlay").arg(res.filesDeployed).arg(res.vanillaBackedUp)
                                 .arg(res.errors.size());
    QStringList details;   // the long lists, behind Show Details
    if (!movedNote.isEmpty()) summary += QStringLiteral("\n\n") + movedNote;

    // Right after the counts: these are plugins the user enabled that the
    // game will not load, and the reason is something only they can fix.
    // Said once per missing file rather than once per plugin - 22 plugins
    // that all need Skyrim.esm are one fact - and first of all, whether the
    // game is there at all: an uninstall leaves the Data folder standing.
    const QString missingExe = writesPluginList(adapter)
        ? bethesda_deploy::missingGameExe(gameRoot, gameExeNames(adapter)) : QString();
    if (!missingExe.isEmpty()) {
        summary += QStringLiteral("\n\n")
                 + T("deploy_game_missing").arg(adapter->displayName(), missingExe,
                                                QDir::toNativeSeparators(gameRoot));
        if (!act.held.isEmpty())
            summary += QLatin1Char(' ') + T("deploy_game_missing_held").arg(act.held.size());
    } else if (!act.held.isEmpty()) {
        summary += QStringLiteral("\n\n") + T("deploy_held_summary").arg(act.held.size());
        constexpr int kFilesShown = 5;
        const auto &files = act.missingFiles;
        for (int i = 0; i < std::min(int(files.size()), kFilesShown); ++i)
            summary += QLatin1Char('\n')
                     + T("deploy_held_group").arg(files[i].master,
                                                  namesAndMore(files[i].neededBy, 3));
        if (files.size() > kFilesShown)
            summary += QLatin1Char('\n')
                     + T("deploy_held_more_groups").arg(int(files.size()) - kFilesShown);
    }
    if (!act.held.isEmpty()) {
        QStringList lines;
        for (const HeldPlugin &h : act.held) {
            QStringList needs = h.missing;
            for (const QString &m : h.heldMasters)
                needs << T("deploy_held_left_out_too").arg(m);
            lines << T("deploy_held_line")
                         .arg(h.plugin, h.mod.isEmpty() ? QStringLiteral("?") : h.mod,
                              needs.join(QStringLiteral(", ")));
        }
        details << T("deploy_held_back").arg(lines.join(QLatin1Char('\n')));
    }
    // Worth its own line: it is what makes Start able to run the extender at
    // all, and it is the one part of the deploy that touches the game folder
    // rather than Data/.
    if (rootPlaced > 0)
        summary += QStringLiteral("\n\n") + T("deploy_script_extender").arg(rootPlaced);
    // An archive named after no plugin never loads, and nothing else would say
    // so: registering it would mean rewriting the engine's whole archive list.
    if (!stray.isEmpty()) {
        constexpr int kStrayShown = 5;
        summary += QStringLiteral("\n\n")
                 + T("deploy_stray_archives").arg(namesAndMore(stray, kStrayShown));
        if (stray.size() > kStrayShown)
            details << T("deploy_stray_archives").arg(stray.join(QStringLiteral(", ")));
    }
    // Say so plainly: this is a change to the game's Proton prefix, not just to
    // its folder, and it is the step that makes a wrapper mod do anything.
    if (!res.manifest.dllOverrides.isEmpty())
        summary += QStringLiteral("\n\n")
                 + T("deploy_dll_overrides")
                       .arg(res.manifest.dllOverrides.join(QStringLiteral(", ")));
    // The Gothic line goes last because it is the one that decides what the
    // engine will actually load.
    if (!gothicNote.isEmpty()) summary += QStringLiteral("\n\n") + gothicNote;
    return {summary, details.join(QStringLiteral("\n\n")), int(act.held.size())};
}

DeployFlow::DeployFlow(WindowShell &shell, QObject *parent)
    : QObject(parent), m_shell(shell)
{
}

void DeployFlow::scheduleDeployHint()
{
    // Coalesced: it rides every "list changed" edge, and a save fires one
    // per row through itemChanged; evaluated once per turn of the event loop
    // instead, and - which is what matters - after a game switch has
    // finished rather than halfway through it.
    if (!m_deployHintTimer) {
        m_deployHintTimer = new QTimer(this);
        m_deployHintTimer->setSingleShot(true);
        m_deployHintTimer->setInterval(0);
        connect(m_deployHintTimer, &QTimer::timeout, this, &DeployFlow::updateDeployHint);
    }
    m_deployHintTimer->start();
}

QString DeployFlow::deployFingerprint() const
{
    // Same rows, same filter and same order as gatherBethesdaSources - enabled,
    // installed mods, persisted order - so it describes what a deploy uses.
    // Path AND install date: an update refreshes the date even when it lands
    // in the same folder.
    QStringList mods;
    const ModlistModel &model = const_cast<WindowShell &>(m_shell).model();
    for (int i : m_shell.rowOrderForPersist()) {
        const ModEntry &it = model.at(i);
        if (!it.isMod() || !it.checked || !it.isInstalled()) continue;
        if (it.modPath.isEmpty()) continue;
        mods << it.modPath + QLatin1Char('\t') + it.dateAdded.toString(Qt::ISODate);
    }
    return bethesda_deploy::sourceFingerprint(mods, m_shell.loadOrder());
}

void DeployFlow::markDeployStale(const QString &modlistFile)
{
    // Only a recorded fingerprint can be contradicted. With none, either
    // nothing was deployed or it predates the record, and the mtime rule the
    // save that follows every install already trips covers it.
    const QString path = bethesdaFingerprintPath(modlistFile);
    if (!QFileInfo::exists(path)) return;
    writeSmallFile(path, QStringLiteral("changed"));
    updateDeployHint();
}

// A Bethesda modlist that was never deployed does nothing whatsoever.
//
// This is the single most confusing thing about managing a non-OpenMW game
// here, and until now nothing said a word about it: OpenMW reads mods from
// wherever they sit via data= lines in openmw.cfg, so an installed mod is a
// working mod, while Skyrim and friends only load what is physically inside
// the game's own Data folder. Someone can therefore install thirty mods, tick
// them all, see them listed and enabled, launch the game, and get vanilla -
// with no error anywhere, because nothing is actually wrong. It just has not
// been copied in yet.
//
// So say so, in a banner that offers to fix it. Only while the profile is
// deploy-capable, has something to deploy, and has never been deployed at all;
// once a manifest exists the deploy-on-launch sync keeps it current and the
// banner would be noise.
void DeployFlow::updateDeployHint()
{
    NotifyBanner *notify = m_shell.notify();
    if (!notify || !m_shell.hasProfile()) return;

    auto clear = [this, notify] {
        if (m_shell.stickyKind() == StickyKind::DeployHint) {
            m_shell.setStickyKind(StickyKind::ViewSort);
            notify->hideSticky();
        }
    };

    const QString id = m_shell.currentGameId();
    const GameAdapter *adapter = GameAdapterRegistry::find(id);
    if (!adapter || adapter->dataSubdir().isEmpty()) { clear(); return; }

    int pending = 0;
    const QList<ModEntry> modRows = m_shell.model().all();
    for (int i = 0; i < modRows.size(); ++i) {
        const ModEntry &it = modRows.at(i);
        if (!it.isMod()) continue;
        if (!it.checked) continue;
        if (!it.isInstalled()) continue;
        ++pending;
    }
    if (pending == 0) { clear(); return; }

    // Two things to warn about, not one.
    //
    // Never deployed is the obvious case. The one that actually bit: a list
    // deployed once and then CHANGED. The manifest exists, so an
    // exists()-only check went quiet, while Data/ still held the old set - a
    // reinstalled mod stayed broken on disk with nothing on screen suggesting
    // a redeploy. Deploy-on-launch only re-syncs when the game is started from
    // here, which is not what happens if the user launches from Steam.
    //
    // Staleness used to be "modlist file newer than manifest", on the theory
    // that every edit rewrites the list. So does every save that edits
    // nothing - switching games, quitting, a background scan touching a row -
    // so a list deployed and then left alone read as changed after the next
    // restart. What the deploy was made FROM is recorded beside the manifest
    // now and compared, and the cases no list shows (a reinstall into the
    // same folder, a swapped variant) mark the record by hand. The mtime rule
    // stays only for deployments made before the record existed, until their
    // next deploy writes one. A false positive still costs one redeploy and a
    // false negative a broken game, so where this cannot tell, it says stale.
    QString manifestPath, backupDir;
    bethesdaStatePaths(m_shell.modlistPath(), manifestPath, backupDir);
    const QFileInfo manifest(manifestPath);
    const bool deployed = manifest.exists();
    bool stale = false;
    if (deployed) {
        const QString recorded = readSmallFile(bethesdaFingerprintPath(m_shell.modlistPath()));
        if (recorded.isEmpty()) {
            const QFileInfo modlist(m_shell.modlistPath());
            stale = modlist.exists() && modlist.lastModified() > manifest.lastModified();
        } else {
            stale = recorded != deployFingerprint();
        }
    }
    if (deployed && !stale) { clear(); return; }

    m_shell.setStickyKind(StickyKind::DeployHint);
    notify->showSticky(stale ? T("deploy_hint_stale")
                             : T("deploy_hint_banner").arg(pending),
                       QStringLiteral("#8a4a12"));
}

// Experimental: deploy the enabled mods of a Bethesda profile into the game's
// Data/ folder.  Bethesda engines only load content physically present there
// (unlike OpenMW's data= paths), so a Skyrim/Oblivion mod list does nothing
// until its files are linked in.  The heavy lifting - hardlink/symlink/copy,
// full vanilla backup, reversible manifest - lives in bethesda_deploy; this
// resolves the paths, gathers the load-ordered mod roots, and reports the
// outcome.  Activation (Plugins.txt + load order) is a later phase; this only
// places the files.
void DeployFlow::onDeployBethesda()
{
    if (!m_shell.hasProfile()) return;
    const QString id = m_shell.currentGameId();
    const GameAdapter *adapter = GameAdapterRegistry::find(id);
    if (!adapter || adapter->dataSubdir().isEmpty()) {
        ui::info(m_shell.dialogParent(), T("deploy_title"), T("deploy_not_supported"));
        return;
    }

    const QString dataDir = bethesdaResolveDataDir(m_shell.dialogParent(), id, adapter, /*allowPrompt=*/true);
    if (dataDir.isEmpty()) return;

    // Deploy the load order the list implies now, as a save would write it.
    // the load order is only brought up to date by saveModList, so straight after
    // a switch or a launch it can still lack plugins the list has - and the
    // deploy would write Plugins.txt from that, then be stale by its own
    // record the moment the next save caught up.
    m_shell.reconcileLoadOrder();
    m_shell.saveLoadOrder();
    const QList<int> order = m_shell.rowOrderForPersist();
    const auto sources = gatherBethesdaSources(m_shell.model(), order, adapter, m_shell.modsDir());
    if (sources.isEmpty()) {
        ui::info(m_shell.dialogParent(), T("deploy_title"), T("deploy_none"));
        return;
    }

    // Two profiles deploying into one folder corrupt each other's backups;
    // warn before the fact rather than leave the user to discover it at
    // undeploy time.  Typically Skyrim SE and AE both auto-detected to the
    // same install because the user only has one copy of the game.
    const QStringList shared = gamesSharingDataDir(m_shell.profiles()->games(), id, dataDir);
    if (!shared.isEmpty()
        && !ui::confirm(m_shell.dialogParent(), T("deploy_title"),
                        T("deploy_shared_data_dir")
                            .arg(shared.join(QStringLiteral(", ")), dataDir)))
        return;

    if (!ui::confirm(m_shell.dialogParent(), T("deploy_title"),
                     deployText(adapter, "deploy_confirm")
                         .arg(sources.size()).arg(dataDir)))
        return;

    // Off the UI thread from here.
    //
    // A real list is thousands of hardlinks plus a full pre-existing-file
    // backup pass; doing that inline froze the window for the whole run, which
    // reads as a crash rather than as work in progress. Everything the worker
    // touches is captured by value (paths, the gathered sources, the load
    // order) - no widgets, no members - which is what async::guarded requires,
    // since it explicitly does NOT keep the object alive across work().
    //
    // Progress therefore goes through a shared counter the worker writes and a
    // UI-side timer reads, rather than the worker posting to `this`: the window
    // can be closed mid-deploy, and a captured MainWindow* would dangle. The
    // shared_ptr outlives whichever side goes first. Same shape as the
    // translation scan's atomic + poll.
    auto progressCell = std::make_shared<std::atomic<qint64>>(0);
    if (ScanOverlay *overlay = m_shell.deployOverlay()) overlay->begin(T("deploy_progress"));
    if (!m_deployProgressTimer) {
        m_deployProgressTimer = new QTimer(this);
        m_deployProgressTimer->setInterval(100);
    }
    QObject::disconnect(m_deployProgressTimer, nullptr, nullptr, nullptr);
    connect(m_deployProgressTimer, &QTimer::timeout, this, [this, progressCell] {
        // done in the high 32 bits, total in the low - one atomic, so the pair
        // is always consistent (two atomics could be read mid-update and show
        // a done from after a total from before).
        const qint64 packed = progressCell->load(std::memory_order_relaxed);
        const int done  = int(packed >> 32);
        const int total = int(packed & 0xffffffff);
        if (ScanOverlay *overlay = m_shell.deployOverlay(); total > 0 && overlay)
            overlay->setPercent(int(100LL * done / total));
    });
    m_deployProgressTimer->start();

    const QString modlistFile = m_shell.modlistPath();
    const QStringList loadOrder = m_shell.loadOrder();
    // Gathered on the UI thread with everything else - it reads the rows.
    const auto rootSources = gatherScriptExtenderSources(m_shell.model(), order, adapter, m_shell.modsDir());
    // Taken with the sources, on the same thread and from the same rows, so it
    // describes exactly what this deploy is about to place.
    const QString fingerprint = deployFingerprint();
    // A move the resolver noticed: this deploy takes the mods out of the old
    // folder first, so it is no longer pending.
    const QString movedFrom = Settings::movedFromDataDir(id);
    Settings::setMovedFromDataDir(id, {});
    async::guarded(this,
        [id, adapter, dataDir, sources, rootSources, modlistFile, loadOrder, fingerprint,
         progressCell, movedFrom]
        (DeployFlow *) -> DeployOutcome {
            return bethesdaApplyDeploy(
                id, adapter, dataDir, sources, rootSources, modlistFile, loadOrder,
                fingerprint,
                [progressCell](int done, int total) {
                    progressCell->store((qint64(done) << 32) | quint32(total),
                                  std::memory_order_relaxed);
                },
                movedFrom);
        },
        [](DeployFlow *self, const DeployOutcome &out) {
            if (self->m_deployProgressTimer) self->m_deployProgressTimer->stop();
            if (ScanOverlay *overlay = self->m_shell.deployOverlay()) overlay->finish();
            ui::info(self->m_shell.dialogParent(), T("deploy_title"), out.summary, out.details);
            self->updateDeployHint();  // a manifest exists now; banner comes down
            // Data/ just changed, so which plugins can load may have too.
            self->refreshScriptExtenderFlags();
            self->m_shell.scanMissingMasters();
        });
}

// Experimental: undo a Bethesda deployment - take our files back out of Data/,
// restore the original game files (including Plugins.txt / Oblivion.ini), and
// forget the deployment.  bethesda_deploy's reversible manifest is what makes
// this safe: only files we actually placed are touched.
void DeployFlow::onUndeployBethesda()
{
    if (!m_shell.hasProfile()) return;
    const QString id = m_shell.currentGameId();
    const GameAdapter *adapter = GameAdapterRegistry::find(id);
    if (!adapter || adapter->dataSubdir().isEmpty()) {
        ui::info(m_shell.dialogParent(), T("undeploy_title"), T("deploy_not_supported"));
        return;
    }
    QString manifestPath, backupDir;
    bethesdaStatePaths(m_shell.modlistPath(), manifestPath, backupDir);
    if (!QFileInfo::exists(manifestPath)) {
        ui::info(m_shell.dialogParent(), T("undeploy_title"), T("undeploy_none"));
        return;
    }
    QString dataDir = bethesdaResolveDataDir(m_shell.dialogParent(), id, adapter, /*allowPrompt=*/true);
    if (dataDir.isEmpty()) return;
    // The deployment is where it was made: in the old folder, if the game
    // has moved since.
    const QString movedFrom = Settings::movedFromDataDir(id);
    if (!movedFrom.isEmpty()) dataDir = movedFrom;
    if (!ui::confirm(m_shell.dialogParent(), T("undeploy_title"),
                     deployText(adapter, "undeploy_confirm").arg(dataDir)))
        return;
    if (!movedFrom.isEmpty()) Settings::setMovedFromDataDir(id, {});
    const QString summary = QDir(dataDir).exists()
        ? bethesdaApplyUndeploy(id, adapter, dataDir, m_shell.modlistPath())
        : T("undeploy_done").arg(bethesdaLeaveOldFolder(id, adapter, dataDir,
                                                        m_shell.modlistPath()))
                            .arg(0).arg(0);
    ui::info(m_shell.dialogParent(), T("undeploy_title"), summary);
    updateDeployHint();   // manifest is gone, so the banner comes back
}

// Deploy-on-launch: if this Bethesda profile has been deployed before (a
// manifest exists), re-sync Data/ to the current enabled-mods list right before
// the game starts.  Best-effort and silent - it must never block a launch, and
// only fires when a manifest exists so it never surprises a user who hasn't
// opted into deployment.
void DeployFlow::maybeDeployBeforeLaunch(const QString &id)
{
    const GameAdapter *adapter = GameAdapterRegistry::find(id);
    if (!adapter || adapter->dataSubdir().isEmpty()) return;
    QString manifestPath, backupDir;
    bethesdaStatePaths(m_shell.modlistPath(), manifestPath, backupDir);
    if (!QFileInfo::exists(manifestPath)) return;
    const QString dataDir = bethesdaResolveDataDir(m_shell.dialogParent(), id, adapter, /*allowPrompt=*/false);
    if (dataDir.isEmpty()) return;
    m_shell.reconcileLoadOrder();   // see onDeployBethesda
    m_shell.saveLoadOrder();
    const QList<int> order = m_shell.rowOrderForPersist();
    const auto sources = gatherBethesdaSources(m_shell.model(), order, adapter, m_shell.modsDir());
    if (sources.isEmpty()) return;
    // No progress panel here: this is the pre-launch re-sync, it only runs when
    // a deployment already exists, and the user is on their way into the game
    // rather than watching the window.
    const QString movedFrom = Settings::movedFromDataDir(id);
    Settings::setMovedFromDataDir(id, {});
    const DeployOutcome out = bethesdaApplyDeploy(
        id, adapter, dataDir, sources,
        gatherScriptExtenderSources(m_shell.model(), order, adapter, m_shell.modsDir()),
        m_shell.modlistPath(), m_shell.loadOrder(), deployFingerprint(), {}, movedFrom);
    updateDeployHint();
    // The summary is not shown here - the user is on their way into the game -
    // but a plugin left out is worth a line: the game starts without it.
    if (out.heldBack > 0)
        m_shell.showStatus(T("deploy_relaunch_synced_held").arg(out.heldBack), 10000);
    else
        m_shell.showStatus(T("deploy_relaunch_synced"), 3000);
    // Worth saying even on the way into the game: the mods changed folders.
    if (!movedFrom.isEmpty())
        m_shell.showStatus(T("deploy_moved_status").arg(QDir::toNativeSeparators(dataDir)), 10000);
    refreshScriptExtenderFlags();
    m_shell.scanMissingMasters();
}

// Read-only diagnostic for the experimental Bethesda deployment: shows every
// resolved path (Data/, Plugins.txt, Oblivion.ini) and whether it exists, the
// manifest state, what would be deployed, and the Proton-prefix candidates
// probed - so a failed path resolution is obvious when testing.  Copyable.
void DeployFlow::onInspectDeployment()
{
    if (!m_shell.hasProfile()) return;
    const QString id = m_shell.currentGameId();
    const GameAdapter *adapter = GameAdapterRegistry::find(id);
    if (!adapter || adapter->dataSubdir().isEmpty()) {
        ui::info(m_shell.dialogParent(), T("deploy_inspect_title"), T("deploy_not_supported"));
        return;
    }

    // Resolve every path via the same bethesda* helpers the real deploy path
    // uses, gather into a plain Facts, then let deployment_report format it and
    // report_dialog show it - the two testable/reusable halves.
    deployment_report::Facts f;
    f.gameName   = m_shell.currentGameName();
    f.gameId     = id;
    f.steamAppId = adapter->steamAppId();
    switch (adapter->loadOrderStyle()) {
    case LoadOrderStyle::TimestampPluginsTxt: f.loadOrderStyle = "timestamp + Plugins.txt (Oblivion/FO3/FNV)"; break;
    case LoadOrderStyle::AsteriskPluginsTxt:  f.loadOrderStyle = "*-prefixed Plugins.txt (Skyrim SE/FO4)"; break;
    case LoadOrderStyle::PlainListPluginsTxt: f.loadOrderStyle = "Plugins.txt in Data/, added to the game's list (Oblivion Remastered)"; break;
    case LoadOrderStyle::OpenMW:              f.loadOrderStyle = "OpenMW"; break;
    default:                                  f.loadOrderStyle = "unknown"; break;
    }

    const QString dataDir = bethesdaResolveDataDir(m_shell.dialogParent(), id, adapter, /*allowPrompt=*/false);
    f.dataFolder = { dataDir, !dataDir.isEmpty() && QDir(dataDir).exists() };

    const QString installDir = bethesdaGameRoot(adapter, dataDir);
    f.installDirKnown = !installDir.isEmpty();
    if (f.installDirKnown) {
        const QString se = findScriptExtenderLoader(adapter, installDir);
        f.scriptExtender = se.isEmpty() ? QString() : QFileInfo(se).fileName();
    }

    const QString pluginsTxt = dataDir.isEmpty() ? QString()
                                                 : resolveBethesdaPluginsTxt(id, adapter, dataDir);
    f.pluginsTxt = { pluginsTxt, !pluginsTxt.isEmpty() && QFileInfo::exists(pluginsTxt) };

    if (id == QLatin1String("oblivion") && !dataDir.isEmpty()) {
        f.showOblivionIni = true;
        const QString iniDir = resolveBethesdaIniDir(id, adapter, dataDir);
        const QString iniPath = iniDir.isEmpty() ? QString() : QDir(iniDir).filePath("Oblivion.ini");
        f.oblivionIni = { iniPath, !iniPath.isEmpty() && QFileInfo::exists(iniPath) };
    }

    QString manifestPath, backupDir;
    bethesdaStatePaths(m_shell.modlistPath(), manifestPath, backupDir);
    f.manifestPath  = manifestPath;
    f.backupDir     = backupDir;
    f.haveManifest  = QFileInfo::exists(manifestPath);
    if (f.haveManifest) {
        QFile mf(manifestPath);
        if (mf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            f.deployedFileCount = int(bethesda_deploy::manifestFromJson(
                                      QString::fromUtf8(mf.readAll())).files.size());
            mf.close();
        }
    }

    const QList<ModEntry> modRows = m_shell.model().all();
    for (int i = 0; i < modRows.size(); ++i) {
        const ModEntry &it = modRows.at(i);
        if (it.isMod()
            && it.checked
            && it.isInstalled())
            ++f.enabledInstalledMods;
    }
    f.dataRootCount = int(gatherBethesdaSources(m_shell.model(), m_shell.rowOrderForPersist(),
                                                adapter, m_shell.modsDir()).size());

    f.prefixCandidates = bethesdaPrefixUserDirs(adapter, dataDir);
    for (const QString &p : f.prefixCandidates)
        f.prefixExists << (QDir(p).exists() ? QStringLiteral("  [found]")
                                            : QStringLiteral("  [MISSING]"));

    ui::showMonospaceReport(m_shell.dialogParent(), T("deploy_inspect_title"),
                            deployment_report::format(f), 760, 520);
}

void DeployFlow::launchProgram(QString &storedPath,
                                std::function<void(const QString&)> savePath,
                                const QString &execName, const QString &locateTitle,
                                bool monitored)
{
    // Auto-detect via PATH if not configured or binary moved
    if (storedPath.isEmpty() || !QFile::exists(storedPath)) {
        storedPath = QStandardPaths::findExecutable(execName);
        if (!storedPath.isEmpty() && savePath)
            savePath(storedPath);
    }

    // Still not found - ask user
    if (storedPath.isEmpty() || !QFile::exists(storedPath)) {
        storedPath = QFileDialog::getOpenFileName(
            m_shell.dialogParent(), locateTitle, "/usr/bin");
        if (storedPath.isEmpty()) return;
        if (savePath) savePath(storedPath);
    }

    if (monitored) {
        auto *proc = new QProcess(this);
        subprocess::applyEnv(*proc);  // scrub AppImage Qt env for the launched program
        connect(proc, &QProcess::finished, this,
                [this, proc](int code, QProcess::ExitStatus) {
            proc->deleteLater();
            if (code != 0)
                QTimer::singleShot(0, this, [this] { m_shell.triageOpenMWLog(); });
        });
        proc->start(storedPath, {});
        if (!proc->waitForStarted(3000)) {
            proc->deleteLater();
            ui::warn(m_shell.dialogParent(), T("launch_error_title"), T("launch_error_body").arg(storedPath));
        }
    } else {
        if (!subprocess::startDetached(storedPath, {}))
            ui::warn(m_shell.dialogParent(), T("launch_error_title"), T("launch_error_body").arg(storedPath));
    }
}

const game_runtime::Probe &DeployFlow::gameRuntimeProbe()
{
    const QString id = m_shell.currentGameId();
    if (id == m_gameRuntimeFor) return m_gameRuntime;   // cached per profile

    m_gameRuntimeFor = id;
    m_gameRuntime    = {};
    if (id.isEmpty()) return m_gameRuntime;

    // Same exe resolution the script-extender check performs, for the same
    // reason: the path the profile launches is the one whose version counts.
    QString exe = Settings::gameExePath(id);
    if (!QFileInfo::exists(exe)) exe = GameProfileRegistry::findSteamGameExe(id);
    if (!QFileInfo::exists(exe)) exe = GameProfileRegistry::findGogGameExe(id);
    if (exe.isEmpty()) return m_gameRuntime;

    const QFileInfo fi(exe);
    // The loader's stem, from the adapter's own list: "f4se_loader.exe" ->
    // "f4se", which is what the runtime DLL beside it is named after.
    QString prefix;
    if (const GameAdapter *ad = GameAdapterRegistry::find(id)) {
        const QStringList loaders = ad->scriptExtenderLoaders();
        if (!loaders.isEmpty())
            prefix = QFileInfo(loaders.first()).completeBaseName()
                         .section(QStringLiteral("_loader"), 0, 0);
    }
    m_gameRuntime = game_runtime::probe(fi.absolutePath(), fi.fileName(), prefix);
    return m_gameRuntime;
}

// Read where skseFindingsFor reads it: the resolved Data/ (no prompt) and the
// profile's deploy manifest. Not cached - a deploy changes the answer, and
// the reading is one small JSON file.
DeployFlow::GameData DeployFlow::gameData()
{
    GameData out;
    if (!m_shell.hasProfile()) return out;
    const QString id = m_shell.currentGameId();
    const GameAdapter *adapter = GameAdapterRegistry::find(id);
    if (!adapter || adapter->dataSubdir().isEmpty()) return out;
    out.dataDir = bethesdaResolveDataDir(m_shell.dialogParent(), id, adapter,
                                         /*allowPrompt=*/false);
    if (out.dataDir.isEmpty()) return out;

    QString manifestPath, backupDir;
    bethesdaStatePaths(m_shell.modlistPath(), manifestPath, backupDir);
    QFile mf(manifestPath);
    if (mf.open(QIODevice::ReadOnly)) {
        const auto man =
            bethesda_deploy::manifestFromJson(QString::fromUtf8(mf.readAll()));
        for (const auto &df : man.files) {
            QString rel = df.rel.toLower();
            rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
            out.deployed.insert(rel);
        }
    }
    return out;
}

// The paths for skse_check::gather, which does the reading. Only the "where"
// lives here: which exe this profile launches, where its Data/ is, and which
// mod put each file there according to the deploy manifest.
static skse_check::Facts gatherSkseFacts(const QString &id,
                                         const QString &dataDir,
                                         const QString &manifestPath)
{
    QString exe = Settings::gameExePath(id);
    if (!QFileInfo::exists(exe)) exe = GameProfileRegistry::findSteamGameExe(id);
    if (!QFileInfo::exists(exe)) exe = GameProfileRegistry::findGogGameExe(id);

    // Data/ is one level under the game root for every game that has one, and
    // a game without one has no script extender plugins to check.
    const QString gameRoot = QFileInfo(dataDir).absolutePath();

    QHash<QString, QString> owners;
    QFile mf(manifestPath);
    if (mf.open(QIODevice::ReadOnly)) {
        const auto man =
            bethesda_deploy::manifestFromJson(QString::fromUtf8(mf.readAll()));
        for (const auto &df : man.files)
            owners.insert(df.rel.toLower(), df.sourceMod);
    }

    return skse_check::gather(exe, gameRoot,
                              dataDir + QStringLiteral("/SKSE/Plugins"), owners);
}

// The script-extender verdict for one profile, or an empty one when the game
// does not deploy into a Data/ or has never been deployed - with no manifest
// nothing in the game folder came from here.
static skse_check::Findings skseFindingsFor(QWidget *parent, const QString &gameId,
                                            const QString &modlistFile)
{
    const GameAdapter *adapter = GameAdapterRegistry::find(gameId);
    if (!adapter || adapter->dataSubdir().isEmpty()) return {};
    QString manifestPath, backupDir;
    bethesdaStatePaths(modlistFile, manifestPath, backupDir);
    if (!QFileInfo::exists(manifestPath)) return {};
    const QString dataDir =
        bethesdaResolveDataDir(parent, gameId, adapter, /*allowPrompt=*/false);
    if (dataDir.isEmpty()) return {};
    return skse_check::evaluate(gatherSkseFacts(gameId, dataDir, manifestPath));
}

// Stamp the rows whose script-extender DLL cannot load against the installed
// game, so the list says it without being asked. Runs when Data/ changes, not
// from a paint path: it reads every plugin DLL in the game folder.
void DeployFlow::refreshScriptExtenderFlags()
{
    if (!m_shell.hasProfile()) return;
    const auto findings = skseFindingsFor(m_shell.dialogParent(), m_shell.currentGameId(),
                                          m_shell.modlistPath());

    QHash<QString, QString> stale;   // mod label -> tooltip detail
    for (const skse_check::Stale &st : findings.stale) {
        if (st.mod.isEmpty()) continue;
        const QString when = st.built.toString(QStringLiteral("yyyy-MM-dd"));
        stale.insert(st.mod,
                     st.declaredFor.valid
                         ? T("skse_row_detail_for").arg(st.file, when,
                                                        st.declaredFor.shortString(),
                                                        findings.game.shortString())
                         : T("skse_row_detail").arg(st.file, when,
                                                    findings.game.shortString()));
    }

    ModlistModel &model = m_shell.model();
    for (int i = 0; i < model.count(); ++i) {
        const ModRow item(&model, model.rowId(i));
        if (!mod_row::isMod(item)) continue;
        QString label = item->data(ModRole::CustomName).toString();
        if (label.isEmpty()) label = item->text();

        const auto hit    = stale.constFind(label);
        const bool isStale = hit != stale.constEnd();
        item->setData(ModRole::ScriptExtenderStale,  isStale);
        item->setData(ModRole::ScriptExtenderDetail, isStale ? *hit : QString());

        // Composed the same way the mod-properties dialog composes it, plus
        // the detail. A later property edit rewrites it without this line;
        // the next deploy puts it back, and a missing tooltip is better than
        // a stale one that names the wrong game version.
        QStringList tip;
        const QString modPath = item->data(ModRole::ModPath).toString();
        const QString annot   = item->data(ModRole::Annotation).toString();
        if (!modPath.isEmpty()) tip << modPath;
        if (!annot.isEmpty())   tip << annot;
        if (isStale)            tip << *hit;
        item->setToolTip(tip.join(QStringLiteral("\n\n")));
    }

    m_shell.repaintList();

    // Logged because the report this feature exists for is "my mod is not
    // marked" or "why is this one on the list", and the answer is which game
    // version was read and how many rows matched a finding.
    if (findings.game.valid)
        qCInfo(logging::lcLaunch).nospace()
            << "skse check: game=" << findings.game.shortString()
            << " loader=" << (findings.loaderFile.isEmpty()
                                  ? QStringLiteral("(none)") : findings.loaderFile)
            << " dbFormat=" << findings.databaseFormat
            << " stale=" << findings.stale.size()
            << " marked=" << stale.size();
}

// Pre-launch sanity check: the warnings the rows already carry (missing
// dependencies, empty installs, forbidden mods enabled) and the script
// extender's verdict on what Data/ now holds, in a summary dialog with a
// Launch-anyway / Cancel choice when any fired. True when the caller should go
// on. Missing masters are not among them: the OpenMW sync and the Bethesda
// deploy both leave such a plugin out of the game, so it cannot stop the
// launch, and the row's red mark says so.
bool DeployFlow::confirmLaunchIfWarnings()
{
    if (m_suppressLaunchSanityCheck) return true;

    const QString gameId = m_shell.currentGameId();
    auto warnings = launch_warnings::scan(m_shell.model().all(),
                                          m_shell.forbidden(), gameId);

    // Script-extender plugins against the installed game.
    launch_warnings::addScriptExtender(
        warnings, skseFindingsFor(m_shell.dialogParent(), gameId, m_shell.modlistPath()));

    if (warnings.total() == 0) return true;

    const auto choice = launch_warnings::showDialog(m_shell.dialogParent(), warnings);
    if (choice.suppress) m_suppressLaunchSanityCheck = true;
    // Not a launch, and not a plain cancel either: the user asked whether the
    // mods holding the game back have newer builds. Answering that is the
    // point of the button, so it runs and the launch does not.
    if (choice.checkUpdates) m_shell.checkUpdatesForMods(warnings.scriptExtenderMods);
    return choice.proceed;
}

bool DeployFlow::refuseLaunchIfRebootPending()
{
    return launch_warnings::refuseIfRebootPending(m_shell.dialogParent());
}

void DeployFlow::onLaunchOpenMW()
{
    if (refuseLaunchIfRebootPending()) return;
    if (!confirmLaunchIfWarnings()) return;
    const QString id = m_shell.currentGameId();
    QString path = m_shell.openmwPath();
    launchProgram(path,
                  [id](const QString &p) { Settings::setOpenmwPath(id, p); },
                  "openmw", T("launch_locate_openmw"),
                  /*monitored=*/true);
    m_shell.setOpenmwPath(path);
}

void DeployFlow::onLaunchOpenMWLauncher()
{
    if (refuseLaunchIfRebootPending()) return;
    if (!confirmLaunchIfWarnings()) return;
    const QString id = m_shell.currentGameId();
    QString path = m_shell.openmwLauncherPath();
    launchProgram(path,
                  [id](const QString &p) { Settings::setOpenmwLauncherPath(id, p); },
                  "openmw-launcher", T("launch_locate_launcher"));
    m_shell.setOpenmwLauncherPath(path);
}

// Helper used by both launch functions: tries Heroic URL → Heroic binary →
// Heroic Flatpak → direct exe, in that order.  Returns true if anything started.
static bool launchViaGog(const QString &gogExe)
{
    if (gogExe.isEmpty() || !QFile::exists(gogExe)) return false;

    // Ask Heroic to handle it so Wine/Proton and runtime env are set up correctly
    // Pass the full exe path - the lookup matches entries whose install_path
    // is a prefix of it, so subdir exes (e.g. bin/x64/...) work too.
    const QString appId = GameProfileRegistry::findHeroicGogAppId(gogExe);
    if (!appId.isEmpty()) {
        const QString url = "heroic://launch/gog/" + appId;
        if (subprocess::startDetached("xdg-open",  {url}))              return true;
        if (subprocess::startDetached("heroic",     {"launch", appId}))  return true;
        if (subprocess::startDetached("flatpak",    {"run",
                "com.heroicgameslauncher.hgl", "launch", appId}))      return true;
    }
    // Fallback: run the exe directly (works for native Linux builds and setups
    // where Wine/Proton is already in the environment - e.g. Lutris, Bottles)
    return subprocess::startDetached(gogExe, {});
}

// Find this game's script-extender loader (OBSE/SKSE/...) in `installDir`, if
// installed - preferred over the raw game exe for a direct launch so
// script-extender mods load.  Empty if none is present.
static QString findScriptExtenderLoader(const GameAdapter *adapter, const QString &installDir)
{
    if (!adapter || installDir.isEmpty()) return {};
    for (const QString &name : adapter->scriptExtenderLoaders()) {
        const QString p = QDir(installDir).filePath(name);
        if (QFileInfo::exists(p)) return p;
    }
    return {};
}

// Hand a steam:// URL to the desktop, and say so when that fails.
//
// Both callers used to fire xdg-open, fall back to `steam`, and then return
// regardless of whether either actually started. When both fail the handler
// does nothing at all - no window, no message, no log line - which is
// indistinguishable from a dead button, and leaves nothing to diagnose it
// with afterwards. Every branch of the launch flow now writes to lcLaunch so
// a "it doesn't work" report has something to read.
static bool dispatchSteamUrl(QWidget *parent, const QString &url)
{
    if (subprocess::startDetached("xdg-open", {url})) {
        qCInfo(logging::lcLaunch) << "launch: xdg-open" << url;
        return true;
    }
    qCWarning(logging::lcLaunch) << "launch: xdg-open failed for" << url
                                 << "- falling back to the steam binary";
    if (subprocess::startDetached("steam", {url})) {
        qCInfo(logging::lcLaunch) << "launch: steam" << url;
        return true;
    }
    qCWarning(logging::lcLaunch) << "launch: no handler could open" << url;
    ui::warn(parent, T("launch_error_title"), T("launch_error_body").arg(url));
    return false;
}

// Run a Windows .exe inside the game's own Proton prefix.
//
// Steam gives no way to say "launch this app, but run that exe" - the URL
// handlers only take an app id, and the app's configured launch option is what
// runs. For a script extender that is exactly the wrong thing: skse64_loader
// has to be the process that starts, because it launches the game itself with
// its hooks in place.
//
// Everything needed is recorded by Steam in compatdata/<appid>/config_info,
// written when the prefix was created:
//   line 1  Proton version
//   line 2  a path inside that Proton build (…/files/share/fonts/)
//   line 4  the Steam client install path
// From line 2 the Proton root is whatever precedes "/files/", and <root>/proton
// is the runner. Returns false if any of that fails to resolve, so the caller
// can fall back to the ordinary Steam launch rather than leaving the user with
// a button that does nothing.
static bool launchViaProton(QWidget *parent, const QString &appId,
                            const QString &exePath)
{
    if (appId.isEmpty() || exePath.isEmpty()) return false;

    // The prefix lives in the SAME library as the game, which for a
    // multi-drive setup is not the one under $HOME. Cut the exe path at its
    // "steamapps" component rather than walking up a fixed number of levels -
    // exes sit at different depths (Skyrim at the game root, Cyberpunk under
    // bin/x64) and a "/.." suffix has to be cleaned before it matches anything.
    QString compatdata;
    {
        const QString clean = QDir::cleanPath(exePath);
        const int at = clean.indexOf(QLatin1String("/steamapps/"));
        if (at > 0) {
            const QString c = clean.left(at) + "/steamapps/compatdata/" + appId;
            if (QDir(c).exists()) compatdata = c;
        }
    }
    if (compatdata.isEmpty()) {
        // Fall back to the default library under $HOME.
        for (const QString &lib : {QDir::homePath() + "/.local/share/Steam",
                                   QDir::homePath() + "/.steam/steam"}) {
            const QString c = lib + "/steamapps/compatdata/" + appId;
            if (QDir(c).exists()) { compatdata = c; break; }
        }
    }
    if (compatdata.isEmpty()) {
        qCWarning(logging::lcLaunch) << "proton: no compatdata for app" << appId;
        return false;
    }

    QFile info(compatdata + "/config_info");
    if (!info.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(logging::lcLaunch) << "proton: unreadable" << info.fileName();
        return false;
    }
    const QStringList lines =
        QString::fromUtf8(info.readAll()).split('\n');
    info.close();
    if (lines.size() < 4) {
        qCWarning(logging::lcLaunch) << "proton: config_info too short";
        return false;
    }
    const QString marker = lines.value(1).trimmed();
    const int filesAt = marker.indexOf(QLatin1String("/files/"));
    if (filesAt <= 0) {
        qCWarning(logging::lcLaunch) << "proton: cannot find the runner from" << marker;
        return false;
    }
    const QString protonRoot   = marker.left(filesAt);
    const QString runner       = protonRoot + "/proton";
    const QString steamClient  = lines.value(3).trimmed();
    if (!QFileInfo::exists(runner)) {
        qCWarning(logging::lcLaunch) << "proton: no runner at" << runner;
        return false;
    }

    qCInfo(logging::lcLaunch).nospace()
        << "proton: " << runner << " run " << exePath
        << " (prefix " << compatdata << ")";
    const bool ok = subprocess::startDetachedWithEnv(
        runner, {QStringLiteral("run"), exePath},
        {{QStringLiteral("STEAM_COMPAT_DATA_PATH"),          compatdata},
         {QStringLiteral("STEAM_COMPAT_CLIENT_INSTALL_PATH"), steamClient}},
        // Working dir beside the exe: the extender resolves the game binary
        // and its own DLLs relative to where it starts.
        QFileInfo(exePath).path());
    if (!ok) {
        qCWarning(logging::lcLaunch) << "proton: runner failed to start";
        ui::warn(parent, T("launch_error_title"), T("launch_error_body").arg(exePath));
    }
    return ok;
}

void DeployFlow::launchWhenSteamReady(const QString &appId, const QString &loader)
{
    QWidget *parent = m_shell.dialogParent();
    // Falls back to the plain Steam launch when no Proton command can be built
    // - never worse than before, as onLaunchGame always did.
    const auto run = [parent, appId, loader] {
        if (launchViaProton(parent, appId, loader)) return;
        qCWarning(logging::lcLaunch)
            << "launch game: found" << loader
            << "but could not build a Proton command - falling back to Steam";
        dispatchSteamUrl(parent, "steam://rungameid/" + appId);
    };

    const steam_client::State now = steam_client::state();
    if (steam_client::next(now, 0) == steam_client::Next::Go) { run(); return; }
    if (m_steamWait) {            // already waiting: one Steam, one game
        m_steamWait->raise();
        m_steamWait->activateWindow();
        return;
    }
    if (!now.running) {
        qCInfo(logging::lcLaunch) << "launch game: Steam is not running - starting it first";
        if (!subprocess::startDetached(QStringLiteral("steam"), {QStringLiteral("-silent")})
            && !subprocess::startDetached(QStringLiteral("xdg-open"),
                                          {QStringLiteral("steam://open/main")})) {
            qCWarning(logging::lcLaunch) << "launch game: could not start Steam";
            ui::warn(parent, T("launch_error_title"), T("launch_steam_cannot_start"));
            return;
        }
    } else {
        qCInfo(logging::lcLaunch) << "launch game: Steam is up but not signed in - waiting";
    }

    // Non-modal, with the window usable behind it; polled, never blocking.
    auto *dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(T("launch_steam_wait_title"));
    auto *lay = new QVBoxLayout(dlg);
    auto *label = new QLabel(T("launch_steam_wait_body").arg(m_shell.currentGameName()), dlg);
    label->setWordWrap(true);
    lay->addWidget(label);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dlg);
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    m_steamWait = dlg;

    auto *timer = new QTimer(dlg);
    timer->setInterval(500);
    auto waited = std::make_shared<QElapsedTimer>();
    waited->start();
    connect(dlg, &QDialog::rejected, timer, &QTimer::stop);
    connect(timer, &QTimer::timeout, dlg, [dlg, timer, run, waited, parent] {
        switch (steam_client::next(steam_client::state(), waited->elapsed())) {
        case steam_client::Next::Wait:
            return;
        case steam_client::Next::Go: {
            timer->stop();
            qCInfo(logging::lcLaunch) << "launch game: Steam ready after"
                                      << waited->elapsed() << "ms";
            const auto go = run;   // the lambda owning `run` goes with the dialog
            dlg->accept();
            go();
            return;
        }
        case steam_client::Next::GiveUp:
            timer->stop();
            qCWarning(logging::lcLaunch) << "launch game: Steam not ready after"
                                         << waited->elapsed() << "ms - not launching";
            dlg->reject();
            ui::warn(parent, T("launch_error_title"), T("launch_steam_timeout"));
            return;
        }
    });
    dlg->show();
    timer->start();
}

void DeployFlow::onLaunchSteamLauncher()
{
    const QString id     = m_shell.currentGameId();
    const QString appId  = GameProfileRegistry::steamAppId(id);
    qCInfo(logging::lcLaunch).nospace()
        << "launch launcher: game=" << id << " appId=" << appId;

    // -- 1. GOG / Heroic - always wins when the game is present there ---
    const QString gogExe = GameProfileRegistry::findGogGameExe(id, /*wantLauncher=*/true);
    if (!gogExe.isEmpty() && QFile::exists(gogExe)) {
        if (!launchViaGog(gogExe))
            ui::warn(m_shell.dialogParent(), T("launch_error_title"), T("launch_error_body").arg(gogExe));
        return;
    }

    // -- 2. Steam URL when we know the game is on Steam ---
    // Always prefer routing through `steam://launch/<appId>` over directly
    // exec'ing the launcher .exe.  Linux can't run a Windows binary
    // standalone (the user reported a Wine error doing exactly this with
    // FalloutNVLauncher.exe); Steam invokes Proton/Wine with the right
    // environment + prefix and then runs the game's configured default
    // launch option, which is the launcher for Bethesda titles.
    // findSteamLauncherExe / findSteamGameExe are still consulted as a
    // "is the game actually installed?" probe so we don't pop a Steam URL
    // for a game the user hasn't bought yet.
    const QString launcherPath = GameProfileRegistry::findSteamLauncherExe(id);
    const QString steamExe     = GameProfileRegistry::findSteamGameExe(id);
    const bool    steamPresent = (!launcherPath.isEmpty() && QFile::exists(launcherPath))
                              || (!steamExe.isEmpty()     && QFile::exists(steamExe));
    if (!appId.isEmpty() && steamPresent) {
        dispatchSteamUrl(m_shell.dialogParent(), "steam://launch/" + appId);
        return;
    }

    // -- 3. Steam URL last resort - we have an AppID but couldn't confirm
    //       a local install; the URL will surface a "buy/install" prompt
    //       which is friendlier than silently failing.
    if (!appId.isEmpty()) {
        qCInfo(logging::lcLaunch) << "launch launcher: no local install confirmed,"
                                  << "opening the Steam page for" << appId;
        dispatchSteamUrl(m_shell.dialogParent(), "steam://launch/" + appId);
        return;
    }

    // -- 4. Ask user ---
    qCInfo(logging::lcLaunch) << "launch launcher: nothing resolved for" << id
                              << "- asking the user for a path";
    QString path = QFileDialog::getOpenFileName(
        m_shell.dialogParent(), T("launch_locate_game").arg(m_shell.currentGameName()),
        QDir::homePath());
    if (path.isEmpty()) return;
    Settings::setLauncherExePath(id, path);
    if (!subprocess::startDetached(path, {}))
        ui::warn(m_shell.dialogParent(), T("launch_error_title"), T("launch_error_body").arg(path));
}

// Resolve the two paths an OpenGothic launch needs, asking only for what is
// missing, and remember both: the engine binary (it ships no assets and is not
// in the game folder) and the Gothic II root it has to be pointed at with -g.
//
// Kept together because neither is useful alone, and because both are stored
// under keys that already exist: the engine is this profile's "game exe" (it
// IS what gets run) and the root is its data dir (it IS what mods deploy into).
bool opengothicResolvePaths(QWidget *parent, const QString &id,
                            QString &engine, QString &root)
{
    root = Settings::dataDir(id);
    if (!opengothic::isGameRoot(root)) {
        // The storefront locators point at system/Gothic2.exe; the root is the
        // folder above it, and it is validated the way the engine validates it.
        QString exe = GameProfileRegistry::findGogGameExe(id, /*wantLauncher=*/false);
        if (exe.isEmpty()) exe = GameProfileRegistry::findSteamGameExe(id);
        if (exe.isEmpty()) exe = GameProfileRegistry::findLutrisGameExe(id);
        root = opengothic::gameRootFor(exe);
    }
    // Still nothing: sweep everywhere the stores say a game is installed and
    // ask the engine's own validator which of them is Gothic II.
    //
    // No names involved, so nothing here can be a false positive: a folder
    // either holds Data/, _work/Data/ and the compiled scripts, or it is not a
    // Gothic II install. This is the last thing tried before giving up and
    // asking, because a store install found this way is still better evidence
    // than a folder the user picks while half asleep.
    if (!opengothic::isGameRoot(root)) {
        QStringList found;
        for (const QString &p : store_scan::allInstallPaths()) {
            const QString r = opengothic::gameRootFor(p);
            if (!r.isEmpty() && !found.contains(r)) found << r;
        }
        if (found.size() == 1) {
            root = found.first();
        } else if (found.size() > 1) {
            bool ok = false;
            const QString picked = QInputDialog::getItem(
                parent, T("gothic_title"), T("gothic_pick_install"), found, 0,
                /*editable=*/false, &ok);
            if (ok && !picked.isEmpty()) root = picked;
        }
    }
    bool asked = false;
    while (!opengothic::isGameRoot(root)) {
        if (!asked) ui::info(parent, T("gothic_title"), T("gothic_locate_game"));
        asked = true;
        const QString picked = QFileDialog::getExistingDirectory(
            parent, T("gothic_locate_game_dialog"), QDir::homePath());
        if (picked.isEmpty()) return false;
        if (opengothic::isGameRoot(picked)) { root = picked; break; }

        // The engine, offered as the game. This is the mistake to expect: it is
        // the folder they just downloaded, and the profile is named after it.
        // Keep it - it is the other half of what has to be found anyway - and
        // ask again for the half that is still missing.
        const QString enginePicked = opengothic::findEngine({picked});
        if (!enginePicked.isEmpty()) {
            Settings::setGameExePath(id, enginePicked);
            if (!ui::confirm(parent, T("gothic_title"),
                             T("gothic_that_is_the_engine")
                                 .arg(QDir::toNativeSeparators(enginePicked))))
                return false;
            continue;
        }

        // Look around where they were looking: the game is usually a folder or
        // two away from whatever was picked.
        QStringList near = opengothic::findGameRoots(picked);
        if (near.isEmpty())
            near = opengothic::findGameRoots(QFileInfo(picked).absolutePath());
        if (!near.isEmpty()
            && ui::confirm(parent, T("gothic_title"),
                           T("gothic_found_nearby").arg(QDir::toNativeSeparators(near.first())))) {
            root = near.first();
            break;
        }

        // Wrong folder for no reason we can name (people pick system/ or Data/),
        // so say what was looked for.
        if (!ui::confirm(parent, T("gothic_title"),
                         T("gothic_not_a_game_root").arg(QDir::toNativeSeparators(picked))))
            return false;
    }
    Settings::setDataDir(id, root);

    // The engine second, because knowing where the game is makes finding it
    // likely enough to be worth trying before asking.
    engine = Settings::gameExePath(id);
    if (engine.isEmpty() || !QFileInfo(engine).isExecutable()) {
        engine = opengothic::findEngine(opengothic::engineSearchHints(root));
        if (engine.isEmpty()) {
            ui::info(parent, T("gothic_title"), T("gothic_locate_engine"));
            engine = QFileDialog::getOpenFileName(parent, T("gothic_locate_engine_dialog"),
                                                  QDir::homePath());
            if (engine.isEmpty()) return false;
        }
        Settings::setGameExePath(id, engine);
    }
    return true;
}

void DeployFlow::onLaunchGame()
{
    const QString id    = m_shell.currentGameId();
    const QString appId = GameProfileRegistry::steamAppId(id);
    const GameAdapter *adapter = GameAdapterRegistry::find(id);
    qCInfo(logging::lcLaunch).nospace()
        << "launch game: game=" << id << " appId=" << appId
        << " adapter=" << (adapter ? "yes" : "MISSING");

    // Deploy-on-launch: re-sync a previously-deployed Bethesda profile's Data/
    // to the current list before the game starts (best-effort, never blocks).
    maybeDeployBeforeLaunch(id);

    // Then look at what that put there. This button never asked before - the
    // launch warnings existed and only the two OpenMW launchers called them -
    // which is how a profile whose every script-extender plugin predates the
    // game got started with nothing said, and found out from the game.
    // Deliberately after the re-sync, so the check reads a Data/ in sync with
    // the list rather than the previous deployment.
    if (!confirmLaunchIfWarnings()) return;

    // -- 0. OpenGothic - not a storefront launch at all -----------------
    //
    // The thing being run is the engine, which lives wherever the user put it
    // and is handed the game folder with -g. Running Gothic2.exe instead would
    // start the 2002 Windows binary under Wine, which is not what this profile
    // manages and would ignore everything deployed.
    if (adapter && adapter->isOpenGothic()) {
        QString engine, root;
        if (!opengothicResolvePaths(m_shell.dialogParent(), id, engine, root)) return;
        // Only pass -game: when the ini is actually there. Passing a missing
        // one makes the engine log "no file in path" and fall back to vanilla,
        // which looks exactly like the mods not working.
        const QString iniName = opengothic::generatedIniName();
        const bool haveIni = QFileInfo::exists(
            QDir(root).filePath(QStringLiteral("system/") + iniName));
        const QStringList args = opengothic::launchArgs(root, haveIni ? iniName : QString());
        qCInfo(logging::lcLaunch) << "launch game: opengothic" << engine << args;
        if (!subprocess::startDetached(engine, args))
            ui::warn(m_shell.dialogParent(), T("launch_error_title"), T("launch_error_body").arg(engine));
        return;
    }

    // -- 1. GOG / Heroic - always wins when the game is present there ---
    const QString gogExe = GameProfileRegistry::findGogGameExe(id);
    if (!gogExe.isEmpty() && QFile::exists(gogExe)) {
        if (!launchViaGog(gogExe))
            ui::warn(m_shell.dialogParent(), T("launch_error_title"), T("launch_error_body").arg(gogExe));
        return;
    }

    // -- 2. Steam - confirmed installed (exe found in Steam library) ---
    const QString steamExe = GameProfileRegistry::findSteamGameExe(id);
    qCInfo(logging::lcLaunch) << "launch game: steam exe ="
                              << (steamExe.isEmpty() ? QStringLiteral("(not found)") : steamExe);
    if (!appId.isEmpty() && !steamExe.isEmpty() && QFile::exists(steamExe)) {
        // Script extender first, when one is actually sitting beside the exe.
        //
        // steam://rungameid runs the game's configured launch option, which for
        // the Bethesda titles is the LAUNCHER - so SKSE never loaded no matter
        // what was deployed. Running skse64_loader.exe instead is the whole
        // point of installing it, and it has to be this button: the Launcher
        // button stays on the launcher, which is what it is for.
        //
        // Falls straight through to the plain Steam launch if there is no
        // loader, or if the Proton command cannot be resolved - never worse
        // than before.
        const QString loader =
            findScriptExtenderLoader(adapter, QFileInfo(steamExe).path());
        if (!loader.isEmpty()) {
            launchWhenSteamReady(appId, loader);
            return;
        }
        dispatchSteamUrl(m_shell.dialogParent(), "steam://rungameid/" + appId);
        return;
    }

    // -- 3. Steam URL last resort (non-standard library path) ---
    if (!appId.isEmpty()) {
        qCInfo(logging::lcLaunch) << "launch game: exe not confirmed on disk,"
                                  << "asking Steam to run" << appId << "anyway";
        dispatchSteamUrl(m_shell.dialogParent(), "steam://rungameid/" + appId);
        return;
    }

    // -- 4. Ask user ---
    qCInfo(logging::lcLaunch) << "launch game: nothing resolved for" << id
                              << "- asking the user for an exe";
    QString exePath = QFileDialog::getOpenFileName(
        m_shell.dialogParent(), T("launch_locate_game").arg(m_shell.currentGameName()),
        QDir::homePath());
    if (exePath.isEmpty()) return;
    Settings::setGameExePath(id, exePath);
    // Prefer the script-extender loader (OBSE/SKSE) if it sits beside the exe:
    // launching it instead of the game exe is how those mods load for a direct
    // (non-Steam) launch.  On Steam the extender's own steam loader auto-loads
    // on the normal rungameid launch above, so paths 2/3 need no change.
    const QString seLoader = findScriptExtenderLoader(adapter, QFileInfo(exePath).path());
    const QString toRun = seLoader.isEmpty() ? exePath : seLoader;
    if (!seLoader.isEmpty())
        m_shell.showStatus(T("launch_with_script_extender"), 3000);
    if (!subprocess::startDetached(toRun, {}))
        ui::warn(m_shell.dialogParent(), T("launch_error_title"), T("launch_error_body").arg(toRun));
}
