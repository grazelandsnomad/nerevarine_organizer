#include "import_flow.h"

#include "import_formats.h"
#include "mainwindow_internal.h"
#include "modlist_model.h"
#include "modlist_serializer.h"
#include "nxmurl.h"
#include "openmwconfigwriter.h"
#include "prompts.h"
#include "subprocess.h"
#include "translator.h"
#include "undo_stack.h"
#include "window_shell.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>

using import_formats::ModlistFormat;

ImportFlow::ImportFlow(WindowShell &shell, QObject *parent)
    : QObject(parent), m_shell(shell)
{
}

void ImportFlow::exportModList()
{
    QString path = QFileDialog::getSaveFileName(
        m_shell.dialogParent(), T("export_dialog_title"),
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
    QList<ModEntry> entries = m_shell.snapshotEntriesForPersist();
    // In-flight install placeholders are transient, machine-local state - not
    // something a recipient (or a later restore) should inherit. A mod being
    // reinstalled is not one: it is still installed, and exports as such.
    for (int i = entries.size() - 1; i >= 0; --i)
        if (entries[i].isMod() && entries[i].installStatus == 2
            && entries[i].modPath.isEmpty())
            entries.removeAt(i);

    const QByteArray content =
        modlist_serializer::serializeModlist(entries).toUtf8();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)
        || f.write(content) != content.size()) {
        m_shell.showStatus(T("status_export_failed"), 3000);
        return;
    }

    m_shell.showStatus(T("status_exported").arg(QFileInfo(path).fileName()), 4000);
}

void ImportFlow::onImportMO2ModList()
{
    QString path = QFileDialog::getOpenFileName(
        m_shell.dialogParent(), T("import_mo2_dialog_title"),
        QDir::homePath(),
        "MO2 modlist (*.txt);;All Files (*)");
    if (!path.isEmpty()) doImportMO2ModList(path);
}

// Import a Mod Organizer 2 `modlist.txt`. MO2's file lists mods in REVERSE priority
// order (last line = highest priority) and uses prefixes: `+` enabled, `-` disabled,
// `*` unmanaged (skipped). Separators have names ending in `_separator`.
// Imported mods have no on-disk path - they're created as not-installed placeholders
// for the user to either point at a directory or re-install from Nexus.
void ImportFlow::doImportMO2ModList(const QString &path)
{
    if (!m_shell.confirmReplaceModList()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ui::warn(m_shell.dialogParent(), T("import_title"), T("import_mo2_read_error").arg(path));
        return;
    }
    const QStringList lines = import_formats::mo2ModlistLines(QString::fromUtf8(f.readAll()));
    f.close();

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
        QMessageBox ask(m_shell.dialogParent());
        ask.setWindowTitle(T("import_mo2_dialog_title"));
        ask.setIcon(QMessageBox::Question);
        ask.setText(T("import_mo2_mods_folder_prompt"));
        auto *yesBtn  = ask.addButton(T("import_mo2_mods_folder_yes"), QMessageBox::AcceptRole);
        ask.addButton(T("import_mo2_mods_folder_skip"), QMessageBox::RejectRole);
        ask.exec();
        if (ask.clickedButton() == yesBtn) {
            modsDir = QFileDialog::getExistingDirectory(
                m_shell.dialogParent(), T("import_mo2_mods_folder_pick"), QDir::homePath());
        }
    }

    const QString nexusGame = m_shell.hasProfile()
        ? nexusDomainFor(m_shell.currentGameId()) : QStringLiteral("morrowind");

    m_shell.undo()->pushUndo();
    ModlistModel &model = m_shell.model();
    model.clear();

    int added = 0, skipped = 0, linked = 0;
    for (const QString &raw : lines) {
        const auto row = import_formats::mo2Row(raw, modsDir, nexusGame, /*linkFolder=*/false);
        if (!row) { if (raw.startsWith('*')) ++skipped; continue; }
        if (row->nexusLinked) ++linked;
        model.append(row->entry);
        ++added;
    }

    m_shell.saveModList();
    m_shell.updateModCount();
    m_shell.updateSectionCounts();
    m_shell.showStatus(
        T("status_imported_mo2").arg(added).arg(skipped)
        + (linked > 0 ? QStringLiteral(" (%1 Nexus links)").arg(linked) : QString()),
        6000);
}

void ImportFlow::onImportWabbajack()
{
    QString path = QFileDialog::getOpenFileName(
        m_shell.dialogParent(), T("import_wabbajack_dialog_title"),
        QDir::homePath(),
        "Wabbajack modlist (*.wabbajack);;All Files (*)");
    if (!path.isEmpty()) doImportWabbajack(path);
}

void ImportFlow::doImportWabbajack(const QString &path)
{
    // Wabbajack-specific entry: when the current profile already has mods,
    // offer to install into a NEW profile instead of replacing the active
    // one.  This is the test-without-wiping flow - the user can run the WJ,
    // see if it works, and switch back to their daily-driver profile from
    // the toolbar without losing anything.
    if (m_shell.model().count() > 0 && m_shell.hasProfile()) {
        QMessageBox box(m_shell.dialogParent());
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
            const int idx = m_shell.createNewModlistProfile(suggest);
            if (idx < 0) return;
            m_shell.switchToModlistProfile(idx);
            // Fresh profile: the list is empty, no need to confirmReplace.
        } else if (box.clickedButton() == replaceBtn) {
            if (!m_shell.confirmReplaceModList()) return;
        } else {
            return;
        }
    } else if (!m_shell.confirmReplaceModList()) {
        return;
    }

    // Brand-new profile (or first-ever install) - make sure we have a
    // mods dir before the WJ import starts queueing downloads.
    if (!m_shell.ensureModsDirForActiveProfile()) return;

    // -- Background worker result ---
    struct WJRaw {
        QString errorKey;
        QString errorArg;
        QJsonObject root;
    };

    // -- Progress dialog (indeterminate while extracting + parsing) ---
    auto *progress = new QProgressDialog(
        T("import_wabbajack_extracting"), QString(), 0, 0, m_shell.dialogParent());
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
            ui::warn(m_shell.dialogParent(), T("import_wabbajack_title"),
                     T(result.errorKey).arg(result.errorArg));
            return;
        }
        finishWabbajackImport(result.root);
    });

    // `watcher` deletes itself via deleteLater() inside the finished
    // lambda above; analyzer can't see the Qt-signal/slot lifetime.
    progress->show(); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
}

void ImportFlow::finishWabbajackImport(const QJsonObject &root)
{
    const QString listName = root["Name"].toString().trimmed();
    const QString author   = root["Author"].toString().trimmed();

    const QList<import_formats::WJMod> mods = import_formats::wabbajackMods(root);
    if (mods.isEmpty()) {
        ui::warn(m_shell.dialogParent(), T("import_wabbajack_title"), T("import_wabbajack_no_mods"));
        return;
    }

    // -- Confirm with a summary dialog ---
    {
        const int nexusCount = static_cast<int>(
            std::count_if(mods.begin(), mods.end(),
                          [](const import_formats::WJMod &m){ return !m.nexusUrl.isEmpty(); }));

        QDialog dlg(m_shell.dialogParent());
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

    // -- Populate the mod list ---
    m_shell.undo()->pushUndo();
    ModlistModel &model = m_shell.model();
    model.clear();
    for (const import_formats::WJMod &mod : mods)
        model.append(import_formats::wabbajackRow(mod));

    m_shell.saveModList();
    m_shell.updateModCount();
    m_shell.updateSectionCounts();
    m_shell.showStatus(
        T("status_imported_wabbajack").arg(mods.size()).arg(listName),
        5000);
}

void ImportFlow::handleDroppedImportFile(const QString &path)
{
    const QFileInfo fi(path);

    if (fi.suffix().toLower() == QLatin1String("wabbajack")) {
        doImportWabbajack(path);
        return;
    }
    if (fi.suffix().toLower() != QLatin1String("txt")) return;

    switch (import_formats::sniffModlistFormat(path)) {
    case ModlistFormat::MO2:        doImportMO2ModList(path); return;
    case ModlistFormat::Nerevarine: doImportNerevarineModList(path); return;
    case ModlistFormat::Unknown:
        ui::warn(m_shell.dialogParent(), T("import_title"),
                 T("import_unknown_format").arg(fi.fileName()));
        return;
    }
}

void ImportFlow::onImportMO2Profile()
{
    if (!m_shell.confirmReplaceModList()) return;

    // -- 1. Pick the MO2 instance folder ---
    QString instanceDir = QFileDialog::getExistingDirectory(
        m_shell.dialogParent(), T("import_mo2_profile_pick_instance"), QDir::homePath());
    if (instanceDir.isEmpty()) return;

    // -- 2. Resolve mods + profiles dirs (ModOrganizer.ini overrides) -----
    const import_formats::MO2Instance inst = import_formats::resolveMO2Instance(instanceDir);
    if (!inst.valid) {
        ui::warn(m_shell.dialogParent(), T("import_mo2_profile_pick_title"),
                 T("import_mo2_profile_not_instance"));
        return;
    }
    const QString modsDir     = inst.modsDir;
    const QString profilesDir = inst.profilesDir;

    // -- 3. Enumerate profiles ---
    QDir profDir(profilesDir);
    QStringList profiles = profDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    if (profiles.isEmpty()) {
        ui::warn(m_shell.dialogParent(), T("import_mo2_profile_pick_title"),
                 T("import_mo2_profile_no_profiles").arg(profilesDir));
        return;
    }

    // -- 4. Profile-picker dialog (skip if only one profile) ---
    QString selectedProfile = profiles.first();
    bool importPlugins = false;

    {
        QDialog dlg(m_shell.dialogParent());
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
        ui::warn(m_shell.dialogParent(), T("import_mo2_profile_pick_title"),
                 T("import_mo2_profile_no_modlist").arg(modlistFile));
        return;
    }
    const QStringList lines = import_formats::mo2ModlistLines(QString::fromUtf8(f.readAll()));
    f.close();

    const QString nexusGame = m_shell.hasProfile()
        ? nexusDomainFor(m_shell.currentGameId()) : QStringLiteral("morrowind");

    m_shell.undo()->pushUndo();
    ModlistModel &model = m_shell.model();
    model.clear();

    int added = 0, linked = 0, skipped = 0;
    for (const QString &raw : lines) {
        const auto row = import_formats::mo2Row(raw, modsDir, nexusGame, /*linkFolder=*/true);
        if (!row) { if (raw.startsWith('*')) ++skipped; continue; }
        if (row->folderLinked) ++linked;
        model.append(row->entry);
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
                m_shell.setLoadOrder(order);
                m_shell.saveLoadOrder();
            }
        }
    }

    m_shell.saveModList();
    m_shell.updateModCount();
    m_shell.updateSectionCounts();
    m_shell.showStatus(
        T("status_imported_mo2_profile").arg(added).arg(linked).arg(skipped).arg(selectedProfile),
        6000);
}

void ImportFlow::doImportNerevarineModList(const QString &path)
{
    if (!m_shell.confirmReplaceModList()) return;

    const QString foreignBase = import_formats::detectModsBaseFromFile(path);

    QString remapFrom, remapTo;
    if (!foreignBase.isEmpty() && foreignBase != m_shell.modsDir()) {
        QMessageBox info(m_shell.dialogParent());
        info.setWindowTitle(T("import_remap_title"));
        info.setText(T("import_remap_prompt").arg(foreignBase));
        info.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        info.setDefaultButton(QMessageBox::Ok);
        if (info.exec() != QMessageBox::Ok) return;

        QString chosen = QFileDialog::getExistingDirectory(
            m_shell.dialogParent(), T("import_remap_pick"), m_shell.modsDir());
        if (chosen.isEmpty()) return;

        remapFrom = foreignBase;
        remapTo   = chosen;
    }

    m_shell.loadModList(path, remapFrom, remapTo);
    // Persist the imported state immediately so openmw.cfg / launcher.cfg
    // reflect the imported mods' enabled/disabled flags right away - without
    // this, the user would see "every ESP inactive" in the OpenMW Launcher
    // until something else (closing the app, toggling a mod) triggered a
    // saveModList -> syncOpenMWConfig pass.
    m_shell.saveModList();
    m_shell.showStatus(T("status_imported"), 3000);
}

void ImportFlow::onImportModList()
{
    QString path = QFileDialog::getOpenFileName(
        m_shell.dialogParent(), T("import_dialog_title"),
        QDir::homePath(),
        "Mod List (*.txt);;All Files (*)");
    if (path.isEmpty()) return;

    doImportNerevarineModList(path);
}

void ImportFlow::onImportFromOpenMWConfig()
{
    // Default location -- on Linux at least.  If absent, fall through
    // to a file picker so Flatpak / snap / custom XDG_CONFIG_HOME users
    // can point us at the right file.
    const QString defaultCfg = QDir::homePath() +
        QStringLiteral("/.config/openmw/openmw.cfg");

    QString cfgPath = defaultCfg;
    if (!QFileInfo::exists(cfgPath)) {
        ui::info(m_shell.dialogParent(), T("import_openmw_pick_title"),
                 T("import_openmw_default_missing").arg(defaultCfg));
        cfgPath = QFileDialog::getOpenFileName(
            m_shell.dialogParent(), T("import_openmw_pick_title"), QDir::homePath(),
            T("import_openmw_pick_filter"));
        if (cfgPath.isEmpty()) return;
    }
    doImportOpenMWConfig(cfgPath);
}

void ImportFlow::doImportOpenMWConfig(const QString &cfgPath)
{
    // Read + parse.  Pure parser lives in openmwconfigwriter.cpp so a
    // unit test can hit it without dragging the GUI in.
    QFile cfg(cfgPath);
    if (!cfg.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ui::warn(m_shell.dialogParent(), T("import_openmw_pick_title"),
                 T("import_openmw_default_missing").arg(cfgPath));
        return;
    }
    const openmw::ImportEntries entries =
        openmw::parseConfigEntries(QString::fromUtf8(cfg.readAll()));
    cfg.close();

    if (entries.dataPaths.isEmpty()) {
        ui::info(m_shell.dialogParent(), T("import_openmw_empty_title"),
                 T("import_openmw_empty_body").arg(cfgPath));
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
    QMessageBox confirm(m_shell.dialogParent());
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
    // order and synthesise a row per folder via the install side, which
    // handles the row bookkeeping and the scroll for us.
    m_shell.model().clear();
    m_shell.setLoadOrder({});

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
        m_shell.installFolderAsMod(dataPath);
        ++imported;
    }

    // Seed load order.  content= goes first (these are the regular
    // plugins) then groundcover= (rendered as instanced grass).  The
    // serializer keeps order, so the user sees plugins in the same
    // slot OpenMW had them.  reconcileLoadOrder() (called from
    // saveModList()) drops anything that doesn't actually live in an
    // imported mod's plugin dir, so a stray content= line for a plugin
    // whose folder we couldn't find won't pollute the load order.
    m_shell.setLoadOrder(entries.contentFiles + entries.groundcoverFiles);

    m_shell.saveModList();
    m_shell.updateModCount();
    m_shell.updateSectionCounts();
    m_shell.scheduleConflictScan();

    if (skippedMissing > 0) {
        m_shell.showStatus(
            T("import_openmw_status_skipped").arg(imported).arg(skippedMissing),
            6000);
    } else {
        m_shell.showStatus(
            T("import_openmw_status_done").arg(imported)
                .arg(QFileInfo(cfgPath).fileName()),
            6000);
    }
}
