// mainwindow_list - list mutation + view: context menu, remove, separators,
// view-sorts, section counts. Split for parallel compilation; same class.

#include "mainwindow.h"
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
#include "mod_sharing.h"
#include "nexuscontroller.h"
#include "downloadqueue.h"
#include "undo_stack.h"
#include "zoom_controller.h"
#include "filter_bar.h"
#include "notify_banner.h"
#include "column_header.h"
#include "forbidden_mods.h"
#include "game_profiles.h"
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
#include "log_triage_dialog.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>
using plugins::collectDataFolders;
using plugins::readTes3Masters;
#include <QDropEvent>
#include <QMimeData>
using fsutils::sanitizeFolderName;

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

void MainWindow::clearViewSortState()
{
    // Cosmetic reset for list rebuilds (load / profile switch / new): drop the
    // banner + flag. Correctness doesn't depend on this - the rebuilt rows carry
    // no SortAnchor stamps, so rowOrderForPersist already returns identity.
    m_viewSortActive = false;
    if (m_notify) m_notify->hideSticky();
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
