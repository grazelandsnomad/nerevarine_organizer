// mainwindow_config - openmw.cfg sync, load-order reconcile/sort, modlist
// save/load, and missing-master/-dependency scans. Split for parallel compile.

#include "mainwindow.h"

#include "post_install.h"
#include "mainwindow_internal.h"
#include "settings.h"
#include "load_order_merge.h"
#include "separatordialog.h"
#include "modlistdelegate.h"
#include "mod_row.h"
#include "modroles.h"
#include "translator.h"
#include "placeholder_state.h"
#include "fomodwizard.h"
#include "bainwizard.h"
#include "modlist_model.h"
#include "modlist_serializer.h"
#include "modlist_summary_dialog.h"
#include "mod_sharing.h"
#include "loadordercontroller.h"
#include "nexuscontroller.h"
#include "downloadqueue.h"
#include "undo_stack.h"
#include "zoom_controller.h"
#include "filter_bar.h"
#include "library_flow.h"
#include "notify_banner.h"
#include "column_header.h"
#include "forbidden_mods.h"
#include "game_profiles.h"
#include "game_adapter.h"
#include "deploy_flow.h"
#include "async_guarded.h"
#include "install_layout.h"
#include "conflict_inspector.h"
#include "report_dialog.h"
#include "toolbar_customization.h"
#include "data_folder_choice.h"
#include "scan_cache.h"
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
#include <QRadioButton>
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
#include "modlist_io.h"
#include "openmwconfigwriter.h"
#include "master_satisfaction.h"
#include "log_triage_dialog.h"
#include "safe_fs.h"
#include "deps_resolver.h"
#include <QFutureWatcher>
using plugins::collectDataFolders;
using plugins::readTes3Masters;
#include <QDropEvent>
#include <QMimeData>
using fsutils::sanitizeFolderName;
#include "mainwindow_internal.h"

// Moved file-statics, forward-declared.
static QStringList readOpenMWContentOrder() ;
static QString detectLootBinary() ;


// Loot-invocation helpers, moved with autoSortLoadOrder.
static QPair<QString, QStringList> lootCommand(const QString &lootPath, const QStringList &lootArgs);
static QString lootFlatpakAppId(const QString &lootPath);

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

void MainWindow::maybeShowBackupMirrorNotice()
{
    if (m_profiles->isEmpty()) return;
    const QString live   = modlistPath();
    const QString mirror = safefs::backupMirrorDir(live, safefs::defaultBackupMirrorRoot());
    if (mirror.isEmpty() || Settings::backupMirrorNoticeShownFor() == mirror) return;
    Settings::setBackupMirrorNoticeShownFor(mirror);

    qInfo().noquote() << "[backups]" << QFileInfo(live).absolutePath()
                      << "is inside the git work tree" << safefs::gitWorkTreeOf(live)
                      << "- snapshots are mirrored to" << mirror;
    // Created now so the link opens a folder even before the first save.
    QDir().mkpath(mirror);
    m_notify->showWithLink(T("backup_mirror_notice"), "#1a6fa8",
                           QUrl::fromLocalFile(mirror).toString());
}

// The active game's plugin order. Fills the cache from that game's own file on
// a miss, so every read resolves the key first and no caller can be handed
// another game's list. A game with no file reads as an empty order, which is
// what loadLoadOrder() used to arrange by clearing before it read.
const QStringList &MainWindow::loadOrderRef() const
{
    const QString key = loadOrderPath();
    if (!m_loadOrders.has(key)) {
        QStringList order;
        QFile f(key);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString contents = QString::fromUtf8(f.readAll());
            f.close();
            // parseLoadOrder transparently handles v1 (line-per-plugin,
            // optional # comments) and v2 (schema header then one per line).
            order = modlist_serializer::parseLoadOrder(contents);
            // What that order was reconciled from, so the first reconcile of
            // the session can tell nothing has changed and skip the walk.
            const QString inputs =
                modlist_serializer::parseLoadOrderFingerprint(contents);
            if (!inputs.isEmpty()) m_reconciledInputs.insert(key, inputs);
        }
        m_loadOrders.put(key, order);
    }
    return m_loadOrders.get(key);
}

QStringList &MainWindow::loadOrderRef()
{
    // Through the const one first, purely to populate; then hand back the
    // entry itself, since most callers here mutate in place.
    static_cast<const MainWindow *>(this)->loadOrderRef();
    return m_loadOrders.ref(loadOrderPath());
}

// Profile churn renames, copies or removes load-order files, so a cached
// entry under the old path would shadow the file it no longer names. Dropped
// wholesale rather than re-keyed: every key reads its own file back on the
// next touch, and the active one has just been saved. The suppressed-plugin
// set goes with it - it is a cache of the last openmw.cfg sync, and keeping a
// stale one beside a fixed cache is how this bug happened in the first place.
// A mod's folder changed, so both things that remember what was in it have to
// forget: ScanCoordinator's data-folder list for that path, and the
// reconcile fingerprint - which is keyed on row paths, and a reinstall can add
// a plugin without any row path moving. Kept in one place because the two are
// only correct together.
void MainWindow::invalidateModScan(const QString &modPath)
{
    if (m_scans && !modPath.isEmpty()) m_scans->invalidateDataFoldersCache(modPath);
    m_reconciledInputs.clear();
}

void MainWindow::forgetCachedLoadOrders()
{
    m_loadOrders.clear();
    m_reconciledInputs.clear();
    m_suppressedPlugins.clear();
}

void MainWindow::loadLoadOrder()
{
    // A forced re-read of the active game's file, for the callers that want
    // the file rather than whatever is cached: startup, and anything that
    // knows the file was written behind our back.
    m_loadOrders.forget(loadOrderPath());
    (void)loadOrderRef();
}

void MainWindow::saveLoadOrder()
{
    const QString path = loadOrderPath();
    if (const auto err = modlist_io::writeLoadOrderFile(
            path, modlist_serializer::serializeLoadOrder(
                      loadOrderRef(), m_reconciledInputs.value(path))))
        onAsyncWriteFailed(path, *err);
}

// Reads the `content=` list from openmw.cfg in their current order.
// Linux path only for now - matches the rest of our Morrowind-profile code.
// Used after LOOT writes a newly-sorted cfg to pull the order back into
// loadOrderRef(), and at startup to respect reorders done in OpenMW Launcher.
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
// between sessions), rewrite loadOrderRef() so the managed content= order from
// openmw.cfg wins. Without this step, Nerevarine would overwrite the
// launcher's changes on the next syncOpenMWConfig() call.
//
// Only active for the Morrowind profile (that's where we sync openmw.cfg).
// Entries in loadOrderRef() that don't appear in openmw.cfg (disabled mods,
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
    // haven't absorbed yet and our loadOrderRef() is a stale baseline.
    const bool cfgNewer      = cfgInfo.exists()      && (!loInfo.exists() || cfgMtime > loMtime);
    const bool launcherNewer = launcherInfo.exists() && (!loInfo.exists() || lcMtime  > loMtime);
    if (!cfgNewer && !launcherNewer) return;

    QStringList merged = loadOrderRef();

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
    // launcher.cfg from a stale loadOrderRef() and clobber the reorder.
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

    if (merged != loadOrderRef()) {
        loadOrderRef() = merged;
        saveLoadOrder();
    }
}

// Ensure loadOrderRef() contains exactly the plugin filenames that are currently
// installed (placeholder_state::folderInstalled: an installed mod, or one mid-
// reinstall whose folder is still there), preserving existing positions for
// plugins already present. New plugins are appended to the end; a topological
// pass at the end enforces "masters above dependents" using the MAST entries
// declared inside each plugin so OpenMW never sees a parent loaded after its
// child (the "Stargazer - Telescopes Cyrodiil asks for parent file
// Stargazer - Telescopes" crash). Uninstalled plugins are removed.
// What reconcileLoadOrder reads, as one string, without touching a single file:
// the path of every row it would consider installed, plus the order it starts
// from. Identical input, identical output - which is what lets the whole
// function be skipped.
QString MainWindow::loadOrderInputsFingerprint() const
{
    QStringList parts;
    const QList<ModEntry> rows = const_cast<MainWindow *>(this)->m_model.all();
    for (const ModEntry &it : rows) {
        if (!it.isMod()) continue;
        if (!placeholder_state::folderInstalled(it.installStatus, it.modPath)) continue;
        parts << it.modPath;
    }
    // Unit separator between the two halves so no row path can be mistaken for
    // a plugin name, as sourceFingerprint does for the same reason.
    const QString joined = parts.join(QChar(0x1e)) + QChar(0x1f)
                         + loadOrderRef().join(QChar(0x1e));
    return QString::fromLatin1(QCryptographicHash::hash(
        joined.toUtf8(), QCryptographicHash::Sha1).toHex());
}

void MainWindow::reconcileLoadOrder()
{
    static const QStringList contentExts{".esp", ".esm", ".omwaddon", ".omwscripts"};

    QElapsedTimer rTotal; rTotal.start();
    QElapsedTimer rPhase; rPhase.start();

    // The loop below asks ScanCoordinator for every installed mod's data
    // folders, and a cache miss walks the folder there and then, on this
    // thread. Cold over 409 mods that measured 38 seconds of a profile
    // switch. Nothing it reads has changed since the last reconcile unless a
    // row changed, so the fingerprint above is the whole guard - and it is
    // seeded from the order file, so the first switch of a session is covered
    // too, which an in-memory memo alone would miss.
    const QString key = loadOrderPath();
    const QString inputsBefore = loadOrderInputsFingerprint();
    if (m_reconciledInputs.value(key) == inputsBefore) {
        qCDebug(logging::lcModList).nospace()
            << "reconcileLoadOrder: inputs unchanged, skipped";
        return;
    }

    // filename → absolute path for every installed plugin we see.  Used
    // both to decide presence and to feed readTes3Masters() below.
    // Routed through the ScanCoordinator cache so a saveModList that fires
    // on every checkbox toggle / rename / annotation edit doesn't pay for
    // a full per-mod recursive directory walk every time.  Cache is
    // invalidated explicitly in addModFromPath / onRemoveSelected for
    // paths whose contents we just changed.
    QHash<QString, QString> pathByName;
    const QList<ModEntry> modRows = m_model.all();
    for (int i = 0; i < modRows.size(); ++i) {
        const ModEntry &item = modRows.at(i);
        if (!item.isMod()) continue;
        if (!placeholder_state::folderInstalled(item.installStatus, item.modPath)) continue;
        QString modPath = item.modPath;
        for (const auto &p : m_scans->cachedDataFolders(modPath, contentExts))
            for (const QString &cf : p.second)
                pathByName.insert(cf, p.first + "/" + cf);
    }

    const qint64 ms_paths = rPhase.restart();

    // Drop plugins that no longer have an installed owner - but only once the
    // rows are measurements rather than assumptions. loadorder::pruneUnprovided
    // says why. The append and sort passes below are additive, so they run
    // either way; only the lossy step waits.
    const bool settled = installStatusSettled();
    {
        QSet<QString> provided;
        provided.reserve(pathByName.size());
        for (auto it = pathByName.constBegin(); it != pathByName.constEnd(); ++it)
            provided.insert(it.key());
        loadorder::pruneUnprovided(loadOrderRef(), provided, settled);
    }

    // Append new plugins. Preserve the order they were discovered in - walk
    // the modlist top-to-bottom so newly-installed mods land beneath the
    // existing list in a predictable position; the topo pass below will
    // lift their masters above any dependents.
    const qint64 ms_drop = rPhase.restart();

    QSet<QString> inList(loadOrderRef().begin(), loadOrderRef().end());
    // Saved order (identity unless a temporary view sort is active) so newly
    // discovered plugins append in load-order position, never the sorted display.
    const QList<int> loOrder = rowOrderForPersist();
    for (int oi = 0; oi < loOrder.size(); ++oi) {
        ModRow item = m_modList->item(loOrder[oi]);
        if (!mod_row::isMod(item)) continue;
        if (!placeholder_state::folderInstalled(item)) continue;
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
                    loadOrderRef().append(cf);
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
    const qint64 ms_append = rPhase.restart();

    loadOrderRef() = loadorder::topologicallySortByMasters(
        loadOrderRef(),
        [this, &pathByName, &reconcileMemo](const QString &name) -> QStringList {
            auto it = reconcileMemo.constFind(name);
            if (it != reconcileMemo.constEnd()) return it.value();
            QStringList ms = m_scans
                ? m_scans->cachedTes3Masters(pathByName.value(name))
                : readTes3Masters(pathByName.value(name));
            reconcileMemo.insert(name, ms);
            return ms;
        });
    const qint64 ms_sort = rPhase.elapsed();

    // The order moved, so the fingerprint that describes it has to be the one
    // computed from the result. Not recorded when the prune was skipped: this
    // pass did only half its work, and the next one has to run rather than
    // decide its inputs are unchanged.
    if (settled) m_reconciledInputs.insert(key, loadOrderInputsFingerprint());

    qCInfo(logging::lcModList).nospace()
        << "reconcileLoadOrder ms total=" << rTotal.elapsed()
        << " paths=" << ms_paths << " drop=" << ms_drop
        << " append=" << ms_append << " sort=" << ms_sort
        << " plugins=" << loadOrderRef().size();
}

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

void MainWindow::autoSortLoadOrder()
{
    if (loadOrderRef().size() <= 1) return;

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

    // Make sure openmw.cfg on disk reflects our current loadOrderRef() - LOOT
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

    // Pull the new order back from openmw.cfg and splice it into loadOrderRef(),
    // preserving the relative positions of plugins whose mods are disabled
    // (those aren't in the cfg but still live in loadOrderRef()).
    const QStringList sortedActive = readOpenMWContentOrder();
    if (sortedActive.isEmpty()) {
        statusBar()->showMessage(T("loot_empty_cfg"), 6000);
        return;
    }

    QSet<QString> activeSet(sortedActive.begin(), sortedActive.end());
    QStringList merged;
    int idx = 0;
    for (const QString &cf : loadOrderRef()) {
        if (activeSet.contains(cf)) {
            if (idx < sortedActive.size())
                merged.append(sortedActive[idx++]);
        } else {
            merged.append(cf);
        }
    }
    while (idx < sortedActive.size()) merged.append(sortedActive[idx++]);
    loadOrderRef() = merged;

    statusBar()->showMessage(T("loot_done"), 4000);
}

void MainWindow::saveModList()           { doSaveModList(false); }
void MainWindow::saveModListForSwitch()  { doSaveModList(true); }

void MainWindow::doSaveModList(bool leavingProfile)
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
    // openmw.cfg from the stale loadOrderRef() and wipe the launcher's reorder.
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
        // A row being reinstalled keeps its folder and is saved as the mod it
        // still is (modlist_serializer) - never dropped, whatever its URL.
        if (!e.isMod() || e.installStatus != 2 || !e.modPath.isEmpty())
            continue;
        if (e.nexusUrl.isEmpty()) {
            entries.removeAt(i);
            continue;
        }
        // Strip the spinner prefix from a placeholder's display name
        // ("⠋ Installing (...)") - the JSONL form stores the raw name.
        if (e.customName.isEmpty()
            && e.displayName.startsWith(QStringLiteral("⠋ "))) {
            entries[i].displayName = e.displayName.mid(2);
        }
    }
    const QString modlistContent = modlist_serializer::serializeModlist(entries);
    const QString modlistFile = modlistPath();

    // Snapshot backup + modlist file write moved off the UI thread.  These
    // are pure-IO and previously sat squarely inside the user-perceived
    // grey-freeze on Add/Edit.  m_saveQueue serializes writes on one
    // background thread in submission order, so two writers never race on the
    // same file and the loadorder/openmw.cfg mtime relationship is preserved
    // (absorbExternalLoadOrder above checks loadorder vs openmw.cfg mtime) -
    // without the UI thread ever blocking on a prior save.  closeEvent drains
    // the queue so unsaved state still flushes before the process dies.
    QPointer<MainWindow> safeSelf(this);
    m_saveQueue.post(
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

    // Leaving this profile: the rows below are about to be destroyed by the
    // switch and this game's config describes a game nobody is looking at, so
    // the four phases here would decorate the dead and rewrite the absent.
    // Measured together at 254 ms of a 620 ms save, and 33 s of a cold one.
    // Each is re-run for the game being switched TO, by its own load.
    const bool leaving = leavingProfile;

    if (!leaving) syncGameConfig();
    const qint64 ms_syncCfg = phase.restart();

    if (!leaving) scanMissingMasters();
    const qint64 ms_masters = phase.restart();

    if (!leaving) scanMissingDependencies();
    const qint64 ms_deps = phase.restart();

    if (!leaving) updateSectionCounts();
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

void MainWindow::onEditLoadOrder()
{
    reconcileLoadOrder(); // make sure the list reflects what's actually installed

    if (loadOrderRef().isEmpty()) {
        ui::info(this, T("loadorder_title"), T("loadorder_empty"));
        return;
    }

    // Build a set of plugins that belong to currently-ENABLED mods, so we can
    // visually distinguish them from ones whose mod is disabled (still in the
    // load order but not emitted to content=).
    static const QStringList contentExts{".esp", ".esm", ".omwaddon", ".omwscripts"};
    QSet<QString> enabledSet;
    const QList<ModEntry> modRows = m_model.all();
    for (int i = 0; i < modRows.size(); ++i) {
        const ModEntry &item = modRows.at(i);
        if (!item.isMod()) continue;
        if (!item.isInstalled()) continue;
        if (!item.checked) continue;
        for (const auto &p : collectDataFolders(
                 item.modPath, contentExts))
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
    for (const QString &cf : loadOrderRef()) {
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
        loadOrderRef().clear();
        for (int i = 0; i < list->count(); ++i)
            loadOrderRef().append(list->item(i)->text());
        autoSortLoadOrder();
        list->clear();
        QFont rowFont("monospace");
        for (const QString &cf : loadOrderRef()) {
            auto *row = new QListWidgetItem(cf, list);
            row->setFont(rowFont);
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
    loadOrderRef().clear();
    for (int i = 0; i < list->count(); ++i)
        loadOrderRef().append(list->item(i)->text());
    saveLoadOrder();
    syncGameConfig();
    statusBar()->showMessage(T("loadorder_applied"), 3000);
}

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
    const QList<ModEntry> modRows = m_model.all();
    for (int i = 0; i < modRows.size(); ++i) {
        const ModEntry &it = modRows.at(i);
        if (!it.isMod()) continue;

        deps::ModEntry e;
        e.idx         = i;
        e.nexusUrl    = it.nexusUrl;
        e.enabled     = it.checked;
        e.installed   = it.isInstalled();
        e.dependsOn   = it.dependsOn;
        e.displayName = it.customName;
        if (e.displayName.isEmpty()) e.displayName = it.displayName;
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
    QHash<int, ModRow > byIdx;
    for (const auto &e : snap) byIdx.insert(e.idx, m_modList->item(e.idx));

    // Write pruned DependsOn lists back to the items.
    if (pruned) {
        for (const auto &e : snap) {
            const ModRow item = byIdx.value(e.idx);
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
        const ModRow item = byIdx.value(target.idx);
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
    const auto clearAll = [this] {
        for (int i = 0; i < m_modList->count(); ++i) {
            ModRow item = m_modList->item(i);
            item->setData(ModRole::HasMissingMaster, false);
            item->setData(ModRole::MissingMasters, QStringList());
        }
    };
    if (m_profiles->isEmpty()) { clearAll(); return; }

    // The Bethesda games: the deploy now leaves a plugin whose master is not
    // installed out of Plugins.txt (Fallout 4 stopped loading on one), and this
    // is where the list says so - the same red mark Morrowind rows get.
    const GameAdapter *adapter = GameAdapterRegistry::find(currentProfile().id);
    if (adapter && listsPluginsTxt(adapter->loadOrderStyle())) {
        runDataFolderMastersScan();
        return;
    }
    // Otherwise only Morrowind's TES3 plugins are checked. For other profiles
    // just clear any previously-flagged icons synchronously and bail.
    if (currentProfile().id != "morrowind") { clearAll(); return; }

    // Snapshot enabled + installed mods into the POD form the controller
    // expects.  No QListWidgetItem pointers cross the thread boundary --
    // the controller keys its results by ModPath.
    QList<LoadOrderController::MastersInput> enabledMods;
    QSet<QString> availableLower;

    static const QStringList contentExts{".esp", ".esm"};
    const QList<ModEntry> modRows = m_model.all();
    for (int i = 0; i < modRows.size(); ++i) {
        const ModEntry &item = modRows.at(i);
        if (!item.isMod()) continue;
        if (!item.isInstalled()) continue;
        if (!item.checked) continue;

        LoadOrderController::MastersInput e;
        e.modPath = item.modPath;
        // Skip groundcover-approved mods - OpenMW gracefully handles missing
        // masters for groundcover= plugins, so flagging them just produces
        // noise (e.g. TOTSP plugins the user doesn't need).
        if (m_groundcoverApproved.contains(e.modPath)) continue;
        QList<openmw::PluginRef> shipped;
        for (const auto &p : m_scans->cachedDataFolders(e.modPath, contentExts)) {
            for (const QString &file : p.second) {
                // Every file on disk can SATISFY a master, whether or not it is
                // loaded - that is a question about the disk.
                availableLower.insert(file.toLower());
                shipped.append({file, QDir(p.first).filePath(file)});
            }
        }
        // But only a plugin the load order carries can FAIL to find one. The
        // rest are not in the game: an optional compatibility patch for a mod
        // the user does not have is held back by findUnsatisfiedMasters long
        // before this (m_suppressedPlugins), and flagging it afterwards is
        // warning about a file OpenMW is never told to open. See
        // master_satisfaction.h.
        for (const auto &pr : openmw::carriedBy(shipped, loadOrderRef(),
                                                m_suppressedPlugins))
            e.plugins.append({pr.fullPath, pr.filename});
        if (e.plugins.isEmpty()) continue;

        enabledMods.append(e);
    }

    m_loadCtl->scanMissingMasters(enabledMods, availableLower);
}

// A Bethesda game loads the plugins at the root of Data/, so the candidates are
// each enabled mod's plugins at the folder a deploy merges into Data/
// (install_layout::deployRoot), and what satisfies a master is those plus the
// game's own files in Data/ - masters, DLC, Creation Club - which no mod
// deploys. Without Data/ those are unknown and every plugin would look like it
// lacks Fallout4.esm, so nothing is marked.
void MainWindow::runDataFolderMastersScan()
{
    const DeployFlow::GameData game = m_deployFlow->gameData();
    if (game.dataDir.isEmpty()) {
        for (int i = 0; i < m_modList->count(); ++i) {
            ModRow item = m_modList->item(i);
            item->setData(ModRole::HasMissingMaster, false);
            item->setData(ModRole::MissingMasters, QStringList());
        }
        return;
    }

    static const QStringList kPlugins = {QStringLiteral("*.esp"), QStringLiteral("*.esm"),
                                         QStringLiteral("*.esl")};
    QSet<QString> availableLower;
    for (const QString &f : QDir(game.dataDir).entryList(kPlugins, QDir::Files))
        if (!game.deployed.contains(f.toLower())) availableLower.insert(f.toLower());

    QList<LoadOrderController::MastersInput> enabledMods;
    const QList<ModEntry> modRows = m_model.all();
    for (const ModEntry &item : modRows) {
        if (!item.isMod() || !item.isInstalled() || !item.checked) continue;
        if (item.modPath.isEmpty()) continue;
        const QDir root(install_layout::deployRoot(item.modPath, m_modsDir));
        LoadOrderController::MastersInput e;
        e.modPath = item.modPath;
        for (const QString &f : root.entryList(kPlugins, QDir::Files)) {
            availableLower.insert(f.toLower());
            e.plugins.append({root.filePath(f), f});
        }
        if (!e.plugins.isEmpty()) enabledMods.append(e);
    }
    m_loadCtl->scanMissingMasters(enabledMods, availableLower);
}

// The machine-translation provider: Google's free endpoint, or a
// LibreTranslate-compatible server of the user's own. Kept to one small modal
// because the real work lives in Settings and the translate pump; this only
// has to ask the question legibly and hand over the docker one-liner.
void MainWindow::onSetTranslateProvider()
{
    QDialog dlg(this);
    dlg.setWindowTitle(T("translate_provider_title"));
    auto *lay = new QVBoxLayout(&dlg);

    auto *why = new QLabel(T("translate_provider_why"), &dlg);
    why->setWordWrap(true);
    lay->addWidget(why);

    auto *useGoogle = new QRadioButton(T("translate_provider_google"), &dlg);
    auto *useLocal  = new QRadioButton(T("translate_provider_local"), &dlg);
    const bool local = Settings::translateProvider() == QLatin1String("local");
    (local ? useLocal : useGoogle)->setChecked(true);
    lay->addWidget(useGoogle);
    lay->addWidget(useLocal);

    auto *form = new QFormLayout;
    auto *url = new QLineEdit(Settings::translateLocalEndpoint(), &dlg);
    auto *key = new QLineEdit(Settings::translateLocalApiKey(), &dlg);
    key->setPlaceholderText(T("translate_provider_key_hint"));
    form->addRow(T("translate_provider_url"), url);
    form->addRow(T("translate_provider_key"), key);
    lay->addLayout(form);

    // The endpoint fields mean nothing while Google is picked; greying them
    // says so without hiding where they will be.
    const auto syncEnabled = [useLocal, url, key] {
        url->setEnabled(useLocal->isChecked());
        key->setEnabled(useLocal->isChecked());
    };
    syncEnabled();
    connect(useLocal,  &QRadioButton::toggled, &dlg, syncEnabled);

    auto *hint = new QLabel(T("translate_provider_hint"), &dlg);
    hint->setWordWrap(true);
    hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    lay->addWidget(hint);

    auto *btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted) return;
    Settings::setTranslateProvider(useLocal->isChecked()
                                       ? QStringLiteral("local")
                                       : QStringLiteral("google"));
    Settings::setTranslateLocalEndpoint(url->text());
    Settings::setTranslateLocalApiKey(key->text());
}

void MainWindow::syncOpenMWConfig()
{
    // OpenMW config sync is only applicable to the Morrowind game profile
    if (m_profiles->isEmpty() || currentProfile().id != "morrowind")
        return;

    // This writes the file the game loads from, straight out of which rows
    // read as installed - and between a list going up and its probe landing,
    // some of those rows are assumptions. An assumption in the safe direction
    // costs an inert data= line. One in the other direction makes every line
    // the mod owns look like an orphan and takes it out of the game. So the
    // write waits; applyInstallProbe calls back. Same rule as updateDeployHint.
    //
    // On the way out it simply does not happen, and that is right: this file
    // is derived state, rebuilt at startup and at every profile switch, and
    // the mod list itself is written before any of this. A config one launch
    // out of date costs nothing; an emptied one costs 58 missing plugins.
    if (!installStatusSettled()) {
        m_syncDeferred = true;
        qCInfo(logging::lcOpenMW) << "syncOpenMWConfig deferred: list not settled";
        return;
    }

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
    QSet<QString> disabledModPaths;   // installed, but unticked right now
    // Display names of all currently-installed mods.  Used by the patch-skip
    // filter a few lines below so that FOMOD-style "01 X for Y" subfolders
    // (e.g. Ashfront's "01 Grass for Remiros' Groundcover") get dropped when
    // their Y mod isn't in the list - OpenMW would otherwise warn / crash
    // loading plugins that reference an absent companion mod.
    const QStringList installedModNames =
        m_model.installedModDisplayNames();
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
        ModRow item = m_modList->item(cfgOrder[oi]);
        if (!mod_row::isMod(item)) continue;

        openmw::ConfigMod cm;
        cm.enabled   = (item->checkState() == Qt::Checked);
        // A row mid-reinstall still has its folder in the game until the new
        // one lands (placeholder_state::folderInstalled).
        cm.installed = placeholder_state::folderInstalled(item);

        if (cm.installed) {
            const QString modPath = item->data(ModRole::ModPath).toString();
            if (!modPath.isEmpty()) {
                managedModPaths.insert(QDir::cleanPath(modPath));
                if (!cm.enabled) disabledModPaths.insert(QDir::cleanPath(modPath));
            }
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
                const QString dir = cm.pluginDirs[pi].first;
                if (m_declinedPatches.contains(
                        data_folder_choice::key(modPath, dir))) {
                    cm.pluginDirs.removeAt(pi);
                    continue;
                }
                const QString target =
                    detectPatchTarget(QFileInfo(dir).fileName());
                if (!target.isEmpty() && !patchTargetInstalled(target))
                    cm.pluginDirs.removeAt(pi);
            }
            if (cm.enabled && cm.pluginDirs.isEmpty()) {
                cm.resourceRoots = plugins::collectResourceFolders(modPath);
                if (cm.resourceRoots.isEmpty()) cm.resourceRoots << modPath;
                // A mod with no plugins at all answers to the same switch:
                // a texture pack shipping 1024/ and 2048/ is exactly the case
                // the chooser exists for.
                for (int ri = cm.resourceRoots.size() - 1; ri >= 0; --ri)
                    if (m_declinedPatches.contains(
                            data_folder_choice::key(modPath, cm.resourceRoots[ri])))
                        cm.resourceRoots.removeAt(ri);
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
            // The user's own word beats every rule below, including an
            // approved mod: one remover inside an approved grass mod has to
            // be rescuable, and a guess that cannot be corrected is the bug
            // this exists for.
            auto forcedContent = [this](const QString &cf) {
                return m_contentPlugins.contains(cf);
            };
            if (modApproved) {
                for (const auto &p : cm.pluginDirs)
                    for (const QString &cf : p.second)
                        if (!forcedContent(cf)) cm.groundcoverFiles.insert(cf);
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
                    // The folder's OWN name, not the whole path: a library
                    // living under any directory called "grass" used to mark
                    // every plugin in every mod inside it.
                    const QString dirName = QFileInfo(p.first).fileName();
                    const bool    dirIsGC =
                        modNameIsGC ||
                        dirName.contains("groundcover", Qt::CaseInsensitive) ||
                        dirName.contains("grass", Qt::CaseInsensitive) ||
                        hintMatches(dirName);
                    for (const QString &cf : p.second) {
                        if (forcedContent(cf)) continue;
                        // A plugin named after the grass it DELETES is not
                        // grass. Tools like Greenmote emit exactly that, and
                        // classifying it as groundcover drops it from the
                        // load order, so the deletions never happen.
                        if (post_install::nameSaysDeletesGroundcover(cf)) continue;
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
    prep.disabledModPaths = std::move(disabledModPaths);
    prep.modsRoot         = m_modsDir.isEmpty() ? QString()
                                                : QDir::cleanPath(m_modsDir);
    prep.loadOrder        = loadOrderRef();
    // The masters the window already has, per file with an mtime gate, rather
    // than re-reading every enabled plugin's header on every sync.
    if (m_scans)
        prep.mastersOf = [this](const QString &pluginPath) {
            return m_scans->cachedTes3Masters(pluginPath);
        };

    openmw::SyncPrepareResult prepared = openmw::prepareForSync(prep);
    // The writer refused: this pass claimed no mods while the config still
    // described populated ones. Touch nothing at all - not the file, not the
    // load order - and say so loudly, because it means something upstream is
    // handing out install answers it should not have.
    if (prepared.refusedEmptyClaim) {
        qCWarning(logging::lcOpenMW).nospace()
            << "syncOpenMWConfig refused: no mod claimed a path while the "
               "managed section still describes installed mods. openmw.cfg "
               "left untouched. rows=" << m_modList->count();
        return;
    }
    mods = std::move(prepared.mods);
    const qint64 ms_masters = syncPhase.restart();

    // Plugins held back for a missing master keep their places in the order
    // kept (persistLoadOrder), so they return to them when the master does.
    m_suppressedPlugins = prepared.suppressedPlugins;
    if (prepared.persistLoadOrder != loadOrderRef()) {
        loadOrderRef() = prepared.persistLoadOrder;
        saveLoadOrder();
    }

    if (prepared.droppedOrphans > 0) {
        statusBar()->showMessage(
            T("status_orphan_content_dropped").arg(prepared.droppedOrphans),
            6000);
    }

    const QString rendered = openmw::renderOpenMWConfig(
        mods, prepared.effectiveLoadOrder, prepared.scrubbedExisting);

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

    // openmw.cfg + launcher.cfg writes moved off the UI thread.  Posted onto
    // the same m_saveQueue as the modlist file write so closeEvent drains one
    // queue, and sequenced behind any prior write (single-thread FIFO) so two
    // saves can't race on the same file.  The launcher mtime gate runs in the
    // worker so it sees the post-write mtime of openmw.cfg.
    const QByteArray cfgBytes = rendered.toUtf8();
    QPointer<MainWindow> safeSelf(this);
    m_saveQueue.post(
        [safeSelf, cfgPath, cfgBytes, launcherPath, lDataPaths, lContent]() {
            auto reportFail = [safeSelf](const QString &path, const QString &why) {
                QMetaObject::invokeMethod(safeSelf.data(),
                    [safeSelf, path, why]() {
                        if (!safeSelf) return;
                        safeSelf->onAsyncWriteFailed(path, why);
                    }, Qt::QueuedConnection);
            };

            (void)safefs::snapshotBackup(cfgPath);
            // ~/.config/openmw does not exist until OpenMW has run once;
            // without it the write failed and a red "Could not save
            // openmw.cfg" banner greeted every new user at startup.
            QDir().mkpath(QFileInfo(cfgPath).path());
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
    // A profile that has never been saved has no file yet, and that is an
    // empty list - one that still has to finish loading as one. Returning
    // early left the count, the model and the loaded flag describing the list
    // this one replaced: a new game opened on "150 / 150 mods active" over an
    // empty view. Only for the profile's own list: an explicit path that is
    // missing (an import, a remap) is a mistake to leave alone, not a list.
    // And a file that exists but will not open stays untouched either way -
    // an empty list shown over it could be saved over it.
    const bool neverSaved = path.isEmpty() && !f.exists();
    if (!neverSaved && !f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    // Per-stage ms, like saveModList's: the probe below is the one that
    // crosses the filesystem once per row, and it had no number at all until
    // this line existed.
    QElapsedTimer loadTotal;  loadTotal.start();
    QElapsedTimer loadPhase;  loadPhase.start();
    int probeCount = 0;
    int probeCold  = 0;   // the ones with nothing written down
    QStringList unprobed; // ...whose folders a worker opens after the list is up
    // The ones a written-down answer was used for, with the time their folder
    // carried when it was scanned, collected only when the one stat below says
    // the library moved while the app was not looking. A worker re-checks
    // them behind the list; scan_cache.h explains the three guards.
    QList<QPair<QString, qint64>> verify;
    const bool recheck = m_scans && m_scans->modsRootMoved(m_modsDir);

    // A freshly loaded list is in saved order; end any temporary view sort.
    clearViewSortState();
    m_modList->clear();
    // What a batch update did is about the list being replaced, and a check
    // still out for it can no longer mark a row of this one: a reply that
    // never came must not keep the button from ever offering Update All.
    m_updateNote = {};
    if (m_updateNoteTimer) m_updateNoteTimer->stop();
    m_checksRunning = 0;

    // Schema-versioned read.  parseModlist sniffs the first non-empty
    // line: if it's the v2 JSONL header, dispatch to the JSON parser;
    // otherwise fall back to the v1 tab parser so legacy files still
    // load on first launch under the new code.  Both paths produce a
    // QList<ModEntry>; the loop below converts each entry into the
    // QListWidgetItem the UI expects, plus the per-row install-status
    // probe and the on-disk "is this dir actually populated?" check
    // that the old loader did inline.
    const QString contents = neverSaved ? QString() : QString::fromUtf8(f.readAll());
    f.close();
    const QList<ModEntry> entries = modlist_serializer::parseModlist(contents);

    QList<ModEntry> rows;
    rows.reserve(entries.size());
    for (const ModEntry &e : entries) {
        if (e.isSeparator()) {
            ModEntry sep;
            sep.itemType    = ItemType::Separator;
            sep.displayName = e.displayName;
            // Defaults that match the legacy loader for missing colours.
            sep.bgColor   = e.bgColor.isValid() ? e.bgColor : QColor(55, 55, 75);
            sep.fgColor   = e.fgColor.isValid() ? e.fgColor : QColor(Qt::white);
            sep.collapsed = e.collapsed;
            sep.itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
            rows.append(sep);
            continue;
        }

        // Mid-install placeholder (`installing: true`): rebuild as a row that
        // is not installed. The InstallController has no live extraction for
        // it; the user has to retry or click Install on the Nexus URL.
        if (e.installStatus == 2) {
            ModEntry ph = ModEntry::named(e.displayName);
            ph.customName    = e.customName;
            ph.nexusUrl      = e.nexusUrl;
            ph.dateAdded     = e.dateAdded;
            ph.installStatus = int(InstallStatus::NotInstalled);   // not actually mid-install anymore
            ph.checked       = false;
            rows.append(ph);
            continue;
        }

        // Cross-machine path remap.
        QString modPath = e.modPath;
        if (!remapFrom.isEmpty() && !remapTo.isEmpty()
            && modPath.startsWith(remapFrom))
            modPath = remapTo + modPath.mid(remapFrom.size());

        ModEntry m = ModEntry::named(e.customName.isEmpty() ? QFileInfo(modPath).fileName()
                                                            : e.customName);
        m.modPath         = modPath;
        m.customName      = e.customName;
        m.annotation      = e.annotation;
        m.nexusUrl        = e.nexusUrl;
        m.dateAdded       = e.dateAdded;
        m.nexusFileId     = e.nexusFileId;
        m.dependsOn       = e.dependsOn;
        m.updateAvailable = e.updateAvailable;
        m.isUtility       = e.isUtility;
        m.isFavorite      = e.isFavorite;
        m.fomodChoices    = e.fomodChoices;
        m.translationOf   = e.translationOf;
        m.bainChoices     = e.bainChoices;
        m.videoUrl        = e.videoUrl;
        m.sourceUrl       = e.sourceUrl;
        m.creationVersion = e.creationVersion;

        // Same install-status probe as before, in one directory walk instead
        // of a stat and a walk: an empty directory is treated as
        // not-installed so the missing-master scan and the reinstall-prompt UI
        // behave consistently, and a missing one reads the same way.
        // install_layout::folderHasContent says why that is the same answer.
        // This runs once per row over the mods mount, which measured 4.1 s
        // cold over 405 folders with the stat in front of it.
        {
            probeCount++;
            // The written-down scan first. Opening the folder costs 103 ms on
            // the spinning disk this library lives on, and there are 409 of
            // them, which was 42 s of a profile switch. Nothing is checked
            // here - see ScanCoordinator::cachedDataFolders for why - so a
            // folder emptied behind the app's back still reads as installed
            // until the worker below gets to it, a second or two later.
            std::optional<bool> cached;
            if (m_scans) cached = m_scans->cachedFolderHasContent(modPath);
            if (cached && recheck)
                verify.append({modPath, m_scans->recordedFolderTime(modPath)});
            // A written-down "there is nothing here" is the one answer that is
            // never taken on trust, because it is the only one that can empty
            // the game's config: not installed means the mod claims no path,
            // every data= line it owns reads as an orphan, and the plugin
            // leaves the load order. A cache that said exactly that about all
            // 409 mods is what put 58 missing files in front of the user.
            //
            // So it is treated the same as no answer at all: the row goes up
            // installed and a worker confirms it. Being wrong this way costs
            // one inert data= line until the probe lands. Being wrong the
            // other way costs the mod. The asymmetry is deliberate.
            if (!cached || !*cached) {
                // Opening the folder here would put the whole list behind a
                // disk seek per mod, so the answer is settled on a worker -
                // everything that depends on it waits below.
                if (!modPath.isEmpty()) unprobed << modPath;
                ++probeCold;
            }
            m.installStatus = int(InstallStatus::Installed);
        }
        m.checked = e.checked;
        QString tip = e.annotation.isEmpty() ? modPath
                                             : modPath + "\n\n" + e.annotation;
        // Mark a shared install: its folder lives outside this profile's mods
        // dir (it was shared in from another profile, or added from elsewhere).
        if (!modPath.isEmpty() && !m_modsDir.isEmpty()
            && !mod_sharing::cleanModPath(modPath).startsWith(
                   mod_sharing::cleanModPath(m_modsDir) + QLatin1Char('/')))
            tip += QStringLiteral("\n\n") + T("share_external_tooltip");
        m.toolTip = tip;
        rows.append(m);
    }
    const qint64 ms_rows = loadPhase.restart();
    // One reset: nothing hears an itemChanged per row.
    m_model.replace(rows);

    // Apply any collapsed separators (hidden items must be set after all items exist)
    for (int i = 0; i < m_modList->count(); ++i) {
        ModRow sep = m_modList->item(i);
        if (mod_row::isSeparator(sep)
                && sep->data(ModRole::Collapsed).toBool())
            collapseSection(sep, true);
    }

    const qint64 ms_collapse = loadPhase.restart();
    updateModCount();
    m_modListLoaded = true;

    qCInfo(logging::lcModList).nospace()
        << "loadModList ms total=" << loadTotal.elapsed()
        << " rows=" << ms_rows
        << " collapse=" << ms_collapse
        << " probes=" << probeCount
        << " cold=" << probeCold
        << " items=" << m_modList->count();

    // Warm the data-folder cache for installed mods. Runs on a worker thread
    // so loadModList returns immediately; by the time the user tries to delete
    // a mod the cache is ready and cachedDataFolders returns instantly instead
    // of doing a cold filesystem scan.
    {
        QStringList paths;
        for (int i = 0; i < m_modList->count(); ++i) {
            ModRow item = m_modList->item(i);
            if (!mod_row::isMod(item)) continue;
            // Every row with a path, not only the installed ones. This pass is
            // the only thing that re-measures a folder in normal operation, and
            // gating it on "installed" meant a cache that wrongly said a mod
            // was uninstalled could never be corrected by it - the repair was
            // gated on the answer it repairs.
            const QString mp = item->data(ModRole::ModPath).toString();
            if (!mp.isEmpty()) paths << mp;
        }
        m_scans->warmDataFoldersCache(paths);
    }

    // The same probe will run for every other profile the first time it is
    // opened, and cold it costs seconds. Nothing is waiting now, so walk their
    // mod folders on a worker purely to pull the directory entries into the
    // page cache - the answer is thrown away. Single-threaded on purpose:
    // eight threads over this mount measured 1.1x, so the mount serialises
    // metadata reads and a pool would only add risk.
    // The folders nothing was written down for. Opening each costs a seek on
    // a spinning library - 103 ms measured, times 409 mods - so the list goes
    // up first and this corrects it. Everything that reads "installed" is held
    // until it lands: see installStatusSettled.
    //
    // The same worker re-checks the rows a written-down answer was used for,
    // but only when the mods root's own time moved - a mod folder added or
    // removed while the app was closed. That is one stat per mod, measured at
    // 11 s for 410 of them on this library, which is why it is gated behind a
    // single stat and never runs on the UI thread. The usual load does none of
    // it: nothing was unprobed, nothing needs re-checking, and the list is up.
    if (!unprobed.isEmpty() || !verify.isEmpty()) {
        const quint64 gen = ++m_installProbeGen;
        async::guarded(this,
            [unprobed, verify, recheck, gen, root = m_modsDir](MainWindow *) {
                InstallProbe r;
                r.verified = recheck || verify.isEmpty();
                r.root     = root;
                r.gen      = gen;
                r.answers.reserve(unprobed.size());
                for (const QString &p : unprobed)
                    r.answers.append({p, install_layout::folderHasContent(p)});
                for (const auto &v : verify) {
                    if (scan_cache::folderAsRecorded(v.first, v.second))
                        continue;                      // as it was written down
                    // Moved or gone. The written-down scan for it is no longer
                    // an answer, and the install status has to be read afresh.
                    r.stale << v.first;
                    r.answers.append({v.first, install_layout::folderHasContent(v.first)});
                }
                return r;
            },
            [](MainWindow *self, InstallProbe r) {
                self->applyInstallProbe(r);
            });
    }

    // Not saved here: serialising a 1.2 MB cache on the way through a switch
    // is exactly the kind of cost this was meant to remove. Close and the
    // install probe's epilogue are enough.

    warmOtherProfileFolders();

    // Folders the user put in the mods directory themselves become rows here:
    // MO2 picks them up on a refresh, and a profile load is this app's
    // equivalent moment (the window coming back to the front is the other).
    // The scan itself runs on a worker.
    if (m_libraryFlow)
        QTimer::singleShot(0, m_libraryFlow, &LibraryFlow::adoptNewFolders);
}

// Reads nothing, keeps nothing, and touches no state: it exists only so the
// kernel has the directory entries by the time the user switches. Mods dirs
// are read from the registry rather than the rows, since the rows for other
// profiles are not loaded.
// The worker's answers, back on the UI thread. Rows that were assumed
// installed and are not get corrected, and only now do the things that read
// "installed" run - the deploy hint above all, because a row briefly in the
// wrong state would make the fingerprint disagree with the manifest and put
// the "your mod list has changed" banner up over a list nobody touched.
void MainWindow::applyInstallProbe(const InstallProbe &r)
{
    // A pass for a list that has already been replaced says nothing about the
    // one on screen, and its answers are matched by path, so applying them
    // could mark a row of a different profile. Drop it.
    if (r.gen != m_installProbeGen) {
        qCInfo(logging::lcModList).nospace()
            << "installProbe dropped stale gen=" << r.gen
            << " current=" << m_installProbeGen;
        return;
    }

    // Whatever moved behind the app's back loses its written-down scan, so the
    // next thing that needs that mod's data folders reads them off the disk.
    for (const QString &p : r.stale)
        if (m_scans) m_scans->invalidateDataFoldersCache(p);
    // Every mod under this root has just been checked, so the one stat that
    // gates the next pass has something current to compare against.
    if (r.verified && m_scans) m_scans->noteModsRootChecked(r.root);

    int corrected = 0;
    for (const auto &a : r.answers) {
        for (int i = 0; i < m_modList->count(); ++i) {
            ModRow item = m_modList->item(i);
            if (!mod_row::isMod(item)) continue;
            if (item->data(ModRole::ModPath).toString() != a.first) continue;
            const int want = int(a.second ? InstallStatus::Installed
                                          : InstallStatus::NotInstalled);
            if (item->data(ModRole::InstallStatus).toInt() == want) continue;
            item->setData(ModRole::InstallStatus, want);
            ++corrected;
        }
    }
    m_installProbeSettled = r.gen;

    qCInfo(logging::lcModList).nospace()
        << "installProbe ms settled=" << r.answers.size()
        << " stale=" << r.stale.size()
        << " corrected=" << corrected;

    updateModCount();
    updateSectionCounts();
    // Whatever was withheld while the rows were assumptions, now that they are
    // measurements. Through the dispatcher, so it stays right when games other
    // than OpenMW grow a config writer.
    if (m_syncDeferred) { m_syncDeferred = false; syncGameConfig(); }
    if (m_deployFlow) m_deployFlow->updateDeployHint();
    scanMissingMasters();
    // The probe is what the next run wants written down.
    if (m_scans) m_scans->savePersistedCache();
}

void MainWindow::warmOtherProfileFolders()
{
    if (!m_profiles || m_profiles->isEmpty()) return;
    QStringList roots;
    const QString active = m_modsDir.isEmpty() ? QString()
                                               : QDir::cleanPath(m_modsDir);
    for (const GameProfile &gp : m_profiles->games())
        for (const ModlistProfile &mp : gp.modlistProfiles) {
            const QString dir = mp.modsDir.isEmpty() ? gp.modsDir : mp.modsDir;
            if (dir.isEmpty()) continue;
            const QString clean = QDir::cleanPath(dir);
            if (clean == active) continue;      // already warm, we just read it
            if (!roots.contains(clean)) roots << clean;
        }
    if (roots.isEmpty()) return;

    // Deliberately light: list each root and open each mod folder once, which
    // is all the install probe needs, and throw the answer away. Handing these
    // to the full recursive scan instead made this a background job that
    // saturated a spinning library for minutes - which is what made even
    // opening the game menu stall. If pre-building another game's scan is ever
    // worth it, it should be something the user starts and can cancel.
    async::guarded(this,
        [roots](MainWindow *) {
            for (const QString &root : roots)
                for (const QFileInfo &kid : QDir(root).entryInfoList(
                         QDir::Dirs | QDir::NoDotAndDotDot))
                    (void)install_layout::folderHasContent(kid.absoluteFilePath());
        },
        [](MainWindow *) { /* nothing to apply: the page cache is the result */ });
}

void MainWindow::saveModListFor(const QString &profileKey,
                                ModRow placeholder)
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

    const ModEntry updated = placeholder->entry();

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
