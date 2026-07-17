// mainwindow_import - modlist import/export slots (MO2, Wabbajack, openmw.cfg).
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
static int readMO2MetaId(const QString &metaIniPath);

void MainWindow::exportModList()
{
    QString path = QFileDialog::getSaveFileName(
        this, T("export_dialog_title"),
        QDir::homePath() + "/modlist_export.txt",
        "Mod List (*.txt);;All Files (*)");
    if (path.isEmpty()) return;

    // Export the same v2 JSONL the on-disk modlist uses, not a hand-rolled
    // subset, so the export is lossless: Nexus identity, FOMOD + BAIN choices,
    // dependencies, utility/favourite flags, notes and separator colours travel.
    // "Import Modlist" (loadModList + parseModlist) already remaps mod paths per
    // machine - so a shared list imports missing mods as placeholders to re-
    // download, while a same-machine import restores everything.
    // Saved order (not the temporary view-sort display) so an export taken
    // while sorted by size/date is still the real load order.
    QList<ModEntry> entries = snapshotEntriesForPersist();
    // In-flight install placeholders are transient, machine-local state - not
    // something a recipient (or a later restore) should inherit.
    for (int i = entries.size() - 1; i >= 0; --i)
        if (entries[i].isMod() && entries[i].installStatus == 2)
            entries.removeAt(i);

    const QByteArray content =
        modlist_serializer::serializeModlist(entries).toUtf8();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)
        || f.write(content) != content.size()) {
        statusBar()->showMessage(T("status_export_failed"), 3000);
        return;
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
        ui::warn(this, T("import_title"), T("import_mo2_read_error").arg(path));
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
                const QString url = nexusModUrl(nexusGame, modId);
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
    // Wabbajack-specific entry: when the current profile already has mods,
    // offer to install into a NEW profile instead of replacing the active
    // one.  This is the test-without-wiping flow - the user can run the WJ,
    // see if it works, and switch back to their daily-driver profile from
    // the toolbar without losing anything.
    if (m_modList->count() > 0 && !m_profiles->isEmpty()) {
        QMessageBox box(this);
        box.setWindowTitle(T("import_wabbajack_title"));
        box.setIcon(QMessageBox::Question);
        box.setText(T("import_wabbajack_route_body"));
        auto *newBtn = box.addButton(T("import_wabbajack_btn_new_profile"),
                                     QMessageBox::ActionRole);
        auto *replaceBtn = box.addButton(T("import_wabbajack_btn_replace"),
                                         QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(newBtn);
        box.exec();
        if (box.clickedButton() == newBtn) {
            // Suggest a name based on the .wabbajack filename.
            const QString suggest = QFileInfo(path).completeBaseName();
            const int idx = createNewModlistProfile(suggest);
            if (idx < 0) return;
            switchToModlistProfile(idx);
            // Fresh profile: m_modList is empty, no need to confirmReplace.
        } else if (box.clickedButton() == replaceBtn) {
            if (!confirmReplaceModList()) return;
        } else {
            return;
        }
    } else if (!confirmReplaceModList()) {
        return;
    }

    // Brand-new profile (or first-ever install) - make sure we have a
    // mods dir before the WJ import starts queueing downloads.
    if (!ensureModsDirForActiveProfile()) return;

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
            subprocess::applyEnv(proc);
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
            ui::warn(this, T("import_wabbajack_title"), T(result.errorKey).arg(result.errorArg));
            return;
        }
        finishWabbajackImport(result.root);
    });

    // `watcher` deletes itself via deleteLater() inside the finished
    // lambda above; analyzer can't see the Qt-signal/slot lifetime.
    progress->show(); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
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
                nexusUrl = nexusModUrl(modSlug, nexusId);
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
                // Keep the larger size.
                if (m.sizeBytes > kept.sizeBytes)
                    kept.sizeBytes = m.sizeBytes;
            }
        }
        mods = std::move(deduped);
    }

    if (mods.isEmpty()) {
        ui::warn(this, T("import_wabbajack_title"), T("import_wabbajack_no_mods"));
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
        ui::warn(this, T("import_title"), T("import_unknown_format").arg(fi.fileName()));
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
        ui::warn(this, T("import_mo2_profile_pick_title"), T("import_mo2_profile_not_instance"));
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
        ui::warn(this, T("import_mo2_profile_pick_title"), T("import_mo2_profile_no_profiles").arg(profilesDir));
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
        ui::warn(this, T("import_mo2_profile_pick_title"), T("import_mo2_profile_no_modlist").arg(modlistFile));
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
                const QString url = nexusModUrl(nexusGame, modId);
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

void MainWindow::onImportFromOpenMWConfig()
{
    // Default location -- on Linux at least.  If absent, fall through
    // to a file picker so Flatpak / snap / custom XDG_CONFIG_HOME users
    // can point us at the right file.  Windows ships openmw.cfg under
    // Documents/My Games/OpenMW/; the picker covers that path too.
#ifdef Q_OS_WIN
    const QString defaultCfg = QDir::homePath() +
        QStringLiteral("/Documents/My Games/OpenMW/openmw.cfg");
#else
    const QString defaultCfg = QDir::homePath() +
        QStringLiteral("/.config/openmw/openmw.cfg");
#endif

    QString cfgPath = defaultCfg;
    if (!QFileInfo::exists(cfgPath)) {
        ui::info(this, T("import_openmw_pick_title"), T("import_openmw_default_missing").arg(defaultCfg));
        cfgPath = QFileDialog::getOpenFileName(
            this, T("import_openmw_pick_title"), QDir::homePath(),
            T("import_openmw_pick_filter"));
        if (cfgPath.isEmpty()) return;
    }
    doImportOpenMWConfig(cfgPath);
}

void MainWindow::doImportOpenMWConfig(const QString &cfgPath)
{
    // Read + parse.  Pure parser lives in openmwconfigwriter.cpp so a
    // unit test can hit it without dragging the GUI in.
    QFile cfg(cfgPath);
    if (!cfg.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ui::warn(this, T("import_openmw_pick_title"), T("import_openmw_default_missing").arg(cfgPath));
        return;
    }
    const openmw::ImportEntries entries =
        openmw::parseConfigEntries(QString::fromUtf8(cfg.readAll()));
    cfg.close();

    if (entries.dataPaths.isEmpty()) {
        ui::info(this, T("import_openmw_empty_title"), T("import_openmw_empty_body").arg(cfgPath));
        return;
    }

    // Partition data= paths into "vanilla base game" (skip when creating
    // mod rows; they're not mods) and "everything else" (real mod
    // folders).  Vanilla detection is conservative -- folder must
    // contain Morrowind.esm AND at least one expansion -- so a user
    // mod that happens to ship a replacement Morrowind.esm doesn't get
    // misclassified.
    QStringList vanilla;
    QStringList managed;
    for (const QString &p : entries.dataPaths) {
        if (openmw::looksLikeVanillaDataFolder(p)) vanilla << p;
        else                                       managed << p;
    }

    // Confirm with the user before nuking the existing modlist.
    const QString vanillaLabel = vanilla.isEmpty()
        ? T("import_openmw_summary_no_vanilla")
        : QDir::toNativeSeparators(vanilla.first());
    const QString body = T("import_openmw_summary_body")
        .arg(QDir::toNativeSeparators(cfgPath))
        .arg(managed.size())
        .arg(entries.contentFiles.size())
        .arg(entries.groundcoverFiles.size())
        .arg(vanillaLabel);
    QMessageBox confirm(this);
    confirm.setWindowTitle(T("import_openmw_summary_title"));
    confirm.setTextFormat(Qt::RichText);
    confirm.setText(body);
    confirm.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    confirm.setDefaultButton(QMessageBox::Ok);
    if (confirm.exec() != QMessageBox::Ok) return;

    // -- Replace the modlist --------------------------------------------
    //
    // Wipe the in-memory list (the strand path used by switch-game isn't
    // needed here -- this isn't a profile change, no InstallController
    // signals are pending).  Then walk managed data= paths in encounter
    // order and synthesise a row per folder via addModFromPath, which
    // handles ModRole bookkeeping + addItem + scrollToItem for us.
    m_modList->clear();
    m_loadOrder.clear();

    int imported = 0;
    int skippedMissing = 0;
    for (const QString &dataPath : managed) {
        // Importer accepts non-existent paths only insofar as it warns
        // about them in the status bar; openmw.cfg quite often points
        // at user-renamed / moved folders.  Skip-but-count keeps the
        // import deterministic without dropping silently.
        if (!QFileInfo(dataPath).isDir()) {
            ++skippedMissing;
            continue;
        }
        addModFromPath(dataPath, /*placeholder=*/nullptr);
        ++imported;
    }

    // Seed load order.  content= goes first (these are the regular
    // plugins) then groundcover= (rendered as instanced grass).  The
    // serializer keeps order, so the user sees plugins in the same
    // slot OpenMW had them.  reconcileLoadOrder() (called from
    // saveModList()) drops anything that doesn't actually live in an
    // imported mod's plugin dir, so a stray content= line for a plugin
    // whose folder we couldn't find won't pollute the load order.
    m_loadOrder = entries.contentFiles + entries.groundcoverFiles;

    saveModList();
    updateModCount();
    updateSectionCounts();
    scheduleConflictScan();

    if (skippedMissing > 0) {
        statusBar()->showMessage(
            T("import_openmw_status_skipped").arg(imported).arg(skippedMissing),
            6000);
    } else {
        statusBar()->showMessage(
            T("import_openmw_status_done").arg(imported)
                .arg(QFileInfo(cfgPath).fileName()),
            6000);
    }
}
