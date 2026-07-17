// mainwindow_config - openmw.cfg sync, load-order reconcile/sort, modlist
// save/load, and missing-master/-dependency scans. Split for parallel compile.

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
