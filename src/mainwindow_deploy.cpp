// mainwindow_deploy - Bethesda deploy/undeploy + game-launch slots, split out
// of mainwindow.cpp so this ~800-line cluster compiles in parallel with the
// rest of MainWindow. Same class, different TU: every definition here is still
// MainWindow::. The Bethesda path-resolution file-statics travel with it; the
// one cross-cutting helper (resolveUserStatePath) comes via mainwindow_internal.h.

#include "mainwindow.h"
#include "settings.h"
#include "separatordialog.h"
#include "modroles.h"
#include "translator.h"
#include "placeholder_state.h"
#include "fomodwizard.h"
#include "bainwizard.h"
#include "bethesda_deploy.h"
#include "bethesda_loadorder.h"
#include "bethesda_archives.h"
#include "bethesda_custom_ini.h"
#include <functional>
#include "proton_paths.h"
#include "modlist_model_widget_bridge.h"
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

// Forward-declare the moved file-statics so intra-TU call order is irrelevant.
static int bethesdaActivate(const QString &id, const GameAdapter *adapter, const QString &dataDir, const bethesda_deploy::Manifest &manifest, const QStringList &loadOrder);
static QString bethesdaApplyDeploy(const QString &id, const GameAdapter *adapter, const QString &dataDir, const QList<bethesda_deploy::DeploySource> &sources, const QString &modlistFile, const QStringList &loadOrder);
static QString bethesdaApplyUndeploy(const QString &id, const GameAdapter *adapter, const QString &dataDir, const QString &modlistFile);
static QStringList bethesdaConfigureArchives(const QString &id, const GameAdapter *adapter, const QString &dataDir, const bethesda_deploy::Manifest &manifest);
static QStringList bethesdaPrefixUserDirs(const GameAdapter *adapter, const QString &dataDir);
static QString bethesdaResolveDataDir(QWidget *parent, const QString &id, const GameAdapter *adapter, bool allowPrompt);
static void bethesdaStatePaths(const QString &modlistFile, QString &manifestPath, QString &backupDir);
static QString findScriptExtenderLoader(const GameAdapter *adapter, const QString &installDir);
static QList<bethesda_deploy::DeploySource> gatherBethesdaSources(QListWidget *list);
static QString heroicGogAppId(const QString &exeOrDirPath);
static bool launchViaGog(const QString &gogExe);
static QString resolveBethesdaIniDir(const QString &id, const GameAdapter *adapter, const QString &dataDir);
static QString resolveBethesdaPluginsTxt(const QString &id, const GameAdapter *adapter, const QString &dataDir);
static void restoreNerevarineBak(const QString &path);


void MainWindow::onTuneSkyrimIni()
{
    // -- Locate the Skyrim SE "My Games" directory ---
    constexpr auto kSkyrimSeId = "skyrimspecialedition";
    QString iniDir = Settings::iniDir(kSkyrimSeId);

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
        ui::info(this, T("skyini_locate_title"), T("skyini_locate_body"));
        QString picked = QFileDialog::getExistingDirectory(
            this, T("skyini_locate_dialog"), QDir::homePath());
        if (picked.isEmpty()) return;
        if (!hasIni(picked)) {
            ui::warn(this, T("skyini_locate_title"), T("skyini_locate_missing"));
            return;
        }
        iniDir = picked;
    }
    Settings::setIniDir(kSkyrimSeId, iniDir);

    QString prefsPath = QDir(iniDir).filePath("SkyrimPrefs.ini");

    IniDoc prefs;
    if (!prefs.load(prefsPath)) {
        ui::warn(this, T("skyini_error_title"), T("skyini_read_error").arg(prefsPath));
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
        ui::warn(this, T("skyini_error_title"), T("skyini_write_error").arg(prefsPath));
        return;
    }

    statusBar()->showMessage(T("skyini_status_saved"), 4000);
}

// Candidate Proton-prefix "users/steamuser" dirs for a Bethesda profile, best
// guess first: derived from the actual install location (handles custom Steam
// libraries), then the standard Steam locations.  Empty entries are dropped.
static QStringList bethesdaPrefixUserDirs(const GameAdapter *adapter,
                                          const QString &dataDir)
{
    const QString appId = adapter ? adapter->steamAppId() : QString();
    if (appId.isEmpty()) return {};
    QStringList out;
    const QString marker = QStringLiteral("/steamapps/common/");
    const int idx = dataDir.indexOf(marker);
    if (idx > 0)
        out << proton::prefixUserDir(dataDir.left(idx) + "/steamapps/compatdata", appId);
    const QString home = QDir::homePath();
    for (const QString &root : { home + "/.local/share/Steam/steamapps/compatdata",
                                 home + "/.steam/steam/steamapps/compatdata" })
        out << proton::prefixUserDir(root, appId);
    out.removeAll(QString());
    return out;
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

// Resolve a deployable profile's Data/ dir: Settings override, else the located
// install + the adapter's Data subdir, else (allowPrompt) a folder picker.
// Persists whatever it settles on; empty if unresolved.
static QString bethesdaResolveDataDir(QWidget *parent, const QString &id,
                                      const GameAdapter *adapter, bool allowPrompt)
{
    auto ok = [](const QString &d) { return !d.isEmpty() && QDir(d).exists(); };
    QString dataDir = Settings::dataDir(id);
    if (!ok(dataDir)) {
        QString exe = GameProfileRegistry::findGogGameExe(id, /*wantLauncher=*/false);
        if (exe.isEmpty()) exe = GameProfileRegistry::findSteamGameExe(id);
        if (!exe.isEmpty() && adapter)
            dataDir = QFileInfo(exe).absolutePath() + "/" + adapter->dataSubdir();
    }
    if (!ok(dataDir) && allowPrompt) {
        ui::info(parent, T("deploy_title"), T("deploy_locate_data"));
        dataDir = QFileDialog::getExistingDirectory(
            parent, T("deploy_locate_data_dialog"), QDir::homePath());
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

// Revert a deployment: undeploy the manifest (restoring displaced vanilla
// files), restore the Plugins.txt / Oblivion.ini we backed up on deploy, and
// clear the manifest so nothing is "deployed" any more.  Returns the summary.
static QString bethesdaApplyUndeploy(const QString &id, const GameAdapter *adapter,
                                     const QString &dataDir, const QString &modlistFile)
{
    QString manifestPath, backupDir;
    bethesdaStatePaths(modlistFile, manifestPath, backupDir);
    QFile mf(manifestPath);
    if (!mf.open(QIODevice::ReadOnly | QIODevice::Text))
        return T("undeploy_none");
    const auto man = bethesda_deploy::manifestFromJson(QString::fromUtf8(mf.readAll()));
    mf.close();

    const auto u = bethesda_deploy::undeploy(dataDir, backupDir, man);

    const QString pluginsTxt = resolveBethesdaPluginsTxt(id, adapter, dataDir);
    if (!pluginsTxt.isEmpty()) restoreNerevarineBak(pluginsTxt);
    if (id == QLatin1String("oblivion")) {
        const QString iniDir = resolveBethesdaIniDir(id, adapter, dataDir);
        if (!iniDir.isEmpty())
            restoreNerevarineBak(QDir(iniDir).filePath("Oblivion.ini"));
    }
    QFile::remove(manifestPath);
    return T("undeploy_done").arg(u.removed).arg(u.restored).arg(u.errors.size());
}

// Enabled, installed mods' data roots in load order (top to bottom; later
// overrides earlier).  collectDataFolders finds plugin-bearing roots;
// collectResourceFolders catches pure asset mods (retextures); de-nested so a
// root nested inside another isn't deployed at the wrong relative path.
static QList<bethesda_deploy::DeploySource> gatherBethesdaSources(QListWidget *list)
{
    static const QStringList kExts{".esp", ".esm", ".esl"};
    QList<bethesda_deploy::DeploySource> sources;
    for (int i = 0; i < list->count(); ++i) {
        auto *item = list->item(i);
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->checkState() != Qt::Checked) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 1) continue;
        const QString modPath = item->data(ModRole::ModPath).toString();
        if (modPath.isEmpty() || !QDir(modPath).exists()) continue;

        QStringList roots;
        for (const auto &p : plugins::collectDataFolders(modPath, kExts))
            roots << p.first;
        roots += plugins::collectResourceFolders(modPath);
        if (roots.isEmpty()) roots << modPath;   // fallback: the mod folder itself
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

// Timestamp engines (Oblivion/FO3/FNV): stamp the deployed plugins' mtimes for
// load order + write Plugins.txt (active set, masters first).  Returns the count
// of plugins stamped; a no-op for other load-order styles.
static int bethesdaActivate(const QString &id, const GameAdapter *adapter,
                            const QString &dataDir,
                            const bethesda_deploy::Manifest &manifest,
                            const QStringList &loadOrder)
{
    const LoadOrderStyle style = adapter->loadOrderStyle();
    if (style != LoadOrderStyle::TimestampPluginsTxt
        && style != LoadOrderStyle::AsteriskPluginsTxt) return 0;
    QSet<QString> active;
    for (const auto &f : manifest.files) {
        if (f.rel.contains('/')) continue;        // plugins load from Data/ root only
        const QString low = f.rel.toLower();
        if (low.endsWith(".esp") || low.endsWith(".esm") || low.endsWith(".esl"))
            active.insert(f.rel);
    }
    QStringList ordered;
    QSet<QString> placed;
    for (const QString &p : loadOrder)
        if (active.contains(p)) { ordered << p; placed.insert(p); }
    for (const auto &f : manifest.files)
        if (active.contains(f.rel) && !placed.contains(f.rel)) {
            ordered << f.rel; placed.insert(f.rel);
        }
    ordered = bethesda_loadorder::mastersFirst(ordered);
    if (ordered.isEmpty()) return 0;

    int activated = 0;
    QString body;
    if (style == LoadOrderStyle::TimestampPluginsTxt) {
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

    const QString pluginsTxt = resolveBethesdaPluginsTxt(id, adapter, dataDir);
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
    return activated;
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

// Undeploy the previous manifest, deploy `sources`, persist the new manifest,
// then activate (load order + Plugins.txt) and configure archives.  Returns the
// human summary.  Shared by the Deploy action and deploy-on-launch.
static QString bethesdaApplyDeploy(const QString &id, const GameAdapter *adapter,
                                   const QString &dataDir,
                                   const QList<bethesda_deploy::DeploySource> &sources,
                                   const QString &modlistFile, const QStringList &loadOrder)
{
    QString manifestPath, backupDir;
    bethesdaStatePaths(modlistFile, manifestPath, backupDir);

    QFile prevFile(manifestPath);
    if (prevFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const auto prev = bethesda_deploy::manifestFromJson(
            QString::fromUtf8(prevFile.readAll()));
        prevFile.close();
        bethesda_deploy::undeploy(dataDir, backupDir, prev);
    }
    const auto res = bethesda_deploy::deploy(dataDir, backupDir, sources);
    QFile outFile(manifestPath);
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        outFile.write(bethesda_deploy::manifestToJson(res.manifest).toUtf8());
        outFile.close();
    }
    const int activated = bethesdaActivate(id, adapter, dataDir, res.manifest, loadOrder);
    const QStringList stray =
        bethesdaConfigureArchives(id, adapter, dataDir, res.manifest);

    QString summary = T("deploy_done").arg(res.filesDeployed)
                                      .arg(res.vanillaBackedUp)
                                      .arg(activated)
                                      .arg(res.errors.size());
    // An archive named after no plugin never loads, and nothing else would say
    // so: registering it would mean rewriting the engine's whole archive list.
    if (!stray.isEmpty())
        summary += QStringLiteral("\n\n")
                 + T("deploy_stray_archives").arg(stray.join(QStringLiteral(", ")));
    return summary;
}

// Experimental: deploy the enabled mods of a Bethesda profile into the game's
// Data/ folder.  Bethesda engines only load content physically present there
// (unlike OpenMW's data= paths), so a Skyrim/Oblivion mod list does nothing
// until its files are linked in.  The heavy lifting - hardlink/symlink/copy,
// full vanilla backup, reversible manifest - lives in bethesda_deploy; this
// resolves the paths, gathers the load-ordered mod roots, and reports the
// outcome.  Activation (Plugins.txt + load order) is a later phase; this only
// places the files.
void MainWindow::onDeployBethesda()
{
    if (m_profiles->isEmpty()) return;
    const QString id = currentProfile().id;
    const GameAdapter *adapter = GameAdapterRegistry::find(id);
    if (!adapter || adapter->dataSubdir().isEmpty()) {
        ui::info(this, T("deploy_title"), T("deploy_not_supported"));
        return;
    }

    const QString dataDir = bethesdaResolveDataDir(this, id, adapter, /*allowPrompt=*/true);
    if (dataDir.isEmpty()) return;

    const auto sources = gatherBethesdaSources(m_modList);
    if (sources.isEmpty()) {
        ui::info(this, T("deploy_title"), T("deploy_none"));
        return;
    }

    if (!ui::confirm(this, T("deploy_title"),
                     T("deploy_confirm").arg(sources.size()).arg(dataDir)))
        return;

    const QString summary = bethesdaApplyDeploy(id, adapter, dataDir, sources,
                                                modlistPath(), m_loadOrder);
    ui::info(this, T("deploy_title"), summary);
}

// Experimental: undo a Bethesda deployment - take our files back out of Data/,
// restore the original game files (including Plugins.txt / Oblivion.ini), and
// forget the deployment.  bethesda_deploy's reversible manifest is what makes
// this safe: only files we actually placed are touched.
void MainWindow::onUndeployBethesda()
{
    if (m_profiles->isEmpty()) return;
    const QString id = currentProfile().id;
    const GameAdapter *adapter = GameAdapterRegistry::find(id);
    if (!adapter || adapter->dataSubdir().isEmpty()) {
        ui::info(this, T("undeploy_title"), T("deploy_not_supported"));
        return;
    }
    QString manifestPath, backupDir;
    bethesdaStatePaths(modlistPath(), manifestPath, backupDir);
    if (!QFileInfo::exists(manifestPath)) {
        ui::info(this, T("undeploy_title"), T("undeploy_none"));
        return;
    }
    const QString dataDir = bethesdaResolveDataDir(this, id, adapter, /*allowPrompt=*/true);
    if (dataDir.isEmpty()) return;
    if (!ui::confirm(this, T("undeploy_title"), T("undeploy_confirm").arg(dataDir)))
        return;
    const QString summary = bethesdaApplyUndeploy(id, adapter, dataDir, modlistPath());
    ui::info(this, T("undeploy_title"), summary);
}

// Deploy-on-launch: if this Bethesda profile has been deployed before (a
// manifest exists), re-sync Data/ to the current enabled-mods list right before
// the game starts.  Best-effort and silent - it must never block a launch, and
// only fires when a manifest exists so it never surprises a user who hasn't
// opted into deployment.
void MainWindow::maybeDeployBeforeLaunch(const QString &id)
{
    const GameAdapter *adapter = GameAdapterRegistry::find(id);
    if (!adapter || adapter->dataSubdir().isEmpty()) return;
    QString manifestPath, backupDir;
    bethesdaStatePaths(modlistPath(), manifestPath, backupDir);
    if (!QFileInfo::exists(manifestPath)) return;
    const QString dataDir = bethesdaResolveDataDir(this, id, adapter, /*allowPrompt=*/false);
    if (dataDir.isEmpty()) return;
    const auto sources = gatherBethesdaSources(m_modList);
    if (sources.isEmpty()) return;
    bethesdaApplyDeploy(id, adapter, dataDir, sources, modlistPath(), m_loadOrder);
    if (statusBar()) statusBar()->showMessage(T("deploy_relaunch_synced"), 3000);
}

// Read-only diagnostic for the experimental Bethesda deployment: shows every
// resolved path (Data/, Plugins.txt, Oblivion.ini) and whether it exists, the
// manifest state, what would be deployed, and the Proton-prefix candidates
// probed - so a failed path resolution is obvious when testing.  Copyable.
void MainWindow::onInspectDeployment()
{
    if (m_profiles->isEmpty()) return;
    const QString id = currentProfile().id;
    const GameAdapter *adapter = GameAdapterRegistry::find(id);
    if (!adapter || adapter->dataSubdir().isEmpty()) {
        ui::info(this, T("deploy_inspect_title"), T("deploy_not_supported"));
        return;
    }

    // Resolve every path via the same bethesda* helpers the real deploy path
    // uses, gather into a plain Facts, then let deployment_report format it and
    // report_dialog show it - the two testable/reusable halves.
    deployment_report::Facts f;
    f.gameName   = currentProfile().displayName;
    f.gameId     = id;
    f.steamAppId = adapter->steamAppId();
    switch (adapter->loadOrderStyle()) {
    case LoadOrderStyle::TimestampPluginsTxt: f.loadOrderStyle = "timestamp + Plugins.txt (Oblivion/FO3/FNV)"; break;
    case LoadOrderStyle::AsteriskPluginsTxt:  f.loadOrderStyle = "*-prefixed Plugins.txt (Skyrim SE/FO4)"; break;
    case LoadOrderStyle::OpenMW:              f.loadOrderStyle = "OpenMW"; break;
    default:                                  f.loadOrderStyle = "unknown"; break;
    }

    const QString dataDir = bethesdaResolveDataDir(this, id, adapter, /*allowPrompt=*/false);
    f.dataFolder = { dataDir, !dataDir.isEmpty() && QDir(dataDir).exists() };

    const QString installDir = dataDir.isEmpty() ? QString() : QFileInfo(dataDir).path();
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
    bethesdaStatePaths(modlistPath(), manifestPath, backupDir);
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

    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() == ItemType::Mod
            && it->checkState() == Qt::Checked
            && it->data(ModRole::InstallStatus).toInt() == 1)
            ++f.enabledInstalledMods;
    }
    f.dataRootCount = int(gatherBethesdaSources(m_modList).size());

    f.prefixCandidates = bethesdaPrefixUserDirs(adapter, dataDir);
    for (const QString &p : f.prefixCandidates)
        f.prefixExists << (QDir(p).exists() ? QStringLiteral("  [found]")
                                            : QStringLiteral("  [MISSING]"));

    ui::showMonospaceReport(this, T("deploy_inspect_title"),
                            deployment_report::format(f), 760, 520);
}

void MainWindow::launchProgram(QString &storedPath,
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
            this, locateTitle, "/usr/bin");
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
                QTimer::singleShot(0, this, &MainWindow::onTriageOpenMWLog);
        });
        proc->start(storedPath, {});
        if (!proc->waitForStarted(3000)) {
            proc->deleteLater();
            ui::warn(this, T("launch_error_title"), T("launch_error_body").arg(storedPath));
        }
    } else {
        if (!subprocess::startDetached(storedPath, {}))
            ui::warn(this, T("launch_error_title"), T("launch_error_body").arg(storedPath));
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

void MainWindow::onLaunchOpenMW()
{
    if (refuseLaunchIfRebootPending()) return;
    if (!confirmLaunchIfWarnings()) return;
    const QString id = currentProfile().id;
    launchProgram(m_openmwPath,
                  [id](const QString &p) { Settings::setOpenmwPath(id, p); },
                  "openmw", T("launch_locate_openmw"),
                  /*monitored=*/true);
    currentProfile().openmwPath = m_openmwPath;
}

void MainWindow::onLaunchOpenMWLauncher()
{
    if (refuseLaunchIfRebootPending()) return;
    if (!confirmLaunchIfWarnings()) return;
    const QString id = currentProfile().id;
    launchProgram(m_openmwLauncherPath,
                  [id](const QString &p) { Settings::setOpenmwLauncherPath(id, p); },
                  "openmw-launcher", T("launch_locate_launcher"));
    currentProfile().openmwLauncherPath = m_openmwLauncherPath;
}

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

void MainWindow::onLaunchSteamLauncher()
{
    const QString &id    = currentProfile().id;
    const QString appId  = GameProfileRegistry::steamAppId(id);

    // -- 1. GOG / Heroic - always wins when the game is present there ---
    const QString gogExe = GameProfileRegistry::findGogGameExe(id, /*wantLauncher=*/true);
    if (!gogExe.isEmpty() && QFile::exists(gogExe)) {
        if (!launchViaGog(gogExe))
            ui::warn(this, T("launch_error_title"), T("launch_error_body").arg(gogExe));
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
        if (!subprocess::startDetached("xdg-open", {"steam://launch/" + appId}))
            subprocess::startDetached("steam",     {"steam://launch/" + appId});
        return;
    }

    // -- 3. Steam URL last resort - we have an AppID but couldn't confirm
    //       a local install; the URL will surface a "buy/install" prompt
    //       which is friendlier than silently failing.
    if (!appId.isEmpty()) {
        if (!subprocess::startDetached("xdg-open", {"steam://launch/" + appId}))
            subprocess::startDetached("steam",     {"steam://launch/" + appId});
        return;
    }

    // -- 4. Ask user ---
    QString path = QFileDialog::getOpenFileName(
        this, T("launch_locate_game").arg(currentProfile().displayName),
        QDir::homePath());
    if (path.isEmpty()) return;
    Settings::setLauncherExePath(id, path);
    if (!subprocess::startDetached(path, {}))
        ui::warn(this, T("launch_error_title"), T("launch_error_body").arg(path));
}

void MainWindow::onLaunchGame()
{
    const QString &id   = currentProfile().id;
    const QString appId = GameProfileRegistry::steamAppId(id);
    const GameAdapter *adapter = GameAdapterRegistry::find(id);

    // Deploy-on-launch: re-sync a previously-deployed Bethesda profile's Data/
    // to the current list before the game starts (best-effort, never blocks).
    maybeDeployBeforeLaunch(id);

    // -- 1. GOG / Heroic - always wins when the game is present there ---
    const QString gogExe = GameProfileRegistry::findGogGameExe(id);
    if (!gogExe.isEmpty() && QFile::exists(gogExe)) {
        if (!launchViaGog(gogExe))
            ui::warn(this, T("launch_error_title"), T("launch_error_body").arg(gogExe));
        return;
    }

    // -- 2. Steam - confirmed installed (exe found in Steam library) ---
    const QString steamExe = GameProfileRegistry::findSteamGameExe(id);
    if (!appId.isEmpty() && !steamExe.isEmpty() && QFile::exists(steamExe)) {
        if (!subprocess::startDetached("xdg-open", {"steam://rungameid/" + appId}))
            subprocess::startDetached("steam",     {"steam://rungameid/" + appId});
        return;
    }

    // -- 3. Steam URL last resort (non-standard library path) ---
    if (!appId.isEmpty()) {
        if (!subprocess::startDetached("xdg-open", {"steam://rungameid/" + appId}))
            subprocess::startDetached("steam",     {"steam://rungameid/" + appId});
        return;
    }

    // -- 4. Ask user ---
    QString exePath = QFileDialog::getOpenFileName(
        this, T("launch_locate_game").arg(currentProfile().displayName),
        QDir::homePath());
    if (exePath.isEmpty()) return;
    Settings::setGameExePath(id, exePath);
    // Prefer the script-extender loader (OBSE/SKSE) if it sits beside the exe:
    // launching it instead of the game exe is how those mods load for a direct
    // (non-Steam) launch.  On Steam the extender's own steam loader auto-loads
    // on the normal rungameid launch above, so paths 2/3 need no change.
    const QString seLoader = findScriptExtenderLoader(adapter, QFileInfo(exePath).path());
    const QString toRun = seLoader.isEmpty() ? exePath : seLoader;
    if (!seLoader.isEmpty() && statusBar())
        statusBar()->showMessage(T("launch_with_script_extender"), 3000);
    if (!subprocess::startDetached(toRun, {}))
        ui::warn(this, T("launch_error_title"), T("launch_error_body").arg(toRun));
}
