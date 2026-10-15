#pragma once

// FakeShell - a WindowShell with no window: a model, a few strings and
// counters, and dialogs that answer themselves. What a flow asks of the
// window shows up here as state a test can read.

#include "window_shell.h"

#include "modlist_model.h"
#include "undo_stack.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

class FakeShell : public WindowShell {
public:
    ModlistModel modelStore;
    UndoStack    undoStack{&modelStore};
    QString      modsDirValue;
    QStringList  loadOrderValue;
    QString      gameId = QStringLiteral("morrowind");
    QString      gameName = QStringLiteral("OpenMW (Morrowind)");
    bool         profile = true;
    bool         replaceAnswer = true;
    bool         idle = true;
    int          saves = 0, loadOrderSaves = 0, countUpdates = 0, sectionUpdates = 0,
                 conflictScans = 0, sizeScans = 0, replaceAsked = 0, configSyncs = 0,
                 masterScans = 0, repaints = 0;
    QStringList  statuses;
    QStringList  installedFolders;
    QList<QPair<QString, QString>> reports;     // title, body
    QString      loadedPath, loadedRemapFrom, loadedRemapTo;
    StickyKind   sticky = StickyKind::ViewSort;
    QString      openmw, launcher;

    ModlistModel &model() override { return modelStore; }
    ModRow rowById(const QUuid &row) const override
    {
        return modelStore.findByRowId(row) >= 0
                   ? ModRow(const_cast<ModlistModel *>(&modelStore), row) : ModRow();
    }
    GameProfileRegistry *profiles() override { return nullptr; }
    bool    hasProfile() const override { return profile; }
    QString currentGameId() const override { return profile ? gameId : QString(); }
    QString currentGameName() const override { return profile ? gameName : QString(); }
    QString modsDir() const override { return modsDirValue; }
    void    setModsDir(const QString &dir) override { modsDirValue = dir; }
    QStringList loadOrder() const override { return loadOrderValue; }
    void        setLoadOrder(const QStringList &order) override { loadOrderValue = order; }
    QString     modlistPath() const override { return modsDirValue + QStringLiteral("/modlist.txt"); }
    QList<int> rowOrderForPersist() const override
    {
        QList<int> order;
        for (int i = 0; i < modelStore.count(); ++i) order.append(i);
        return order;
    }
    QList<ModEntry> snapshotEntriesForPersist() const override { return modelStore.all(); }
    QList<QPair<QString, QList<ModEntry>>> otherProfileModlists() const override { return {}; }
    UndoStack             *undo() override { return &undoStack; }
    ForbiddenModsRegistry *forbidden() override { return nullptr; }
    QString openmwPath() const override { return openmw; }
    void    setOpenmwPath(const QString &path) override { openmw = path; }
    QString openmwLauncherPath() const override { return launcher; }
    void    setOpenmwLauncherPath(const QString &path) override { launcher = path; }

    void saveModList() override { ++saves; }
    void loadModList(const QString &path, const QString &remapFrom, const QString &remapTo) override
    {
        loadedPath = path; loadedRemapFrom = remapFrom; loadedRemapTo = remapTo;
    }
    void saveLoadOrder() override { ++loadOrderSaves; }
    void reconcileLoadOrder() override {}
    void syncGameConfig() override { ++configSyncs; }
    void scanMissingMasters() override { ++masterScans; }
    void scheduleSizeScan() override { ++sizeScans; }
    void updateModCount() override { ++countUpdates; }
    void updateSectionCounts() override { ++sectionUpdates; }
    void scheduleConflictScan() override { ++conflictScans; }
    void repaintList() override { ++repaints; }

    void installFolderAsMod(const QString &dirPath) override
    {
        installedFolders << dirPath;
        ModEntry e = ModEntry::named(dirPath.section('/', -1));
        e.modPath = dirPath;
        e.installStatus = 1;
        e.checked = true;
        modelStore.append(e);
    }
    bool downloadsIdle() const override { return idle; }
    int  checkUpdatesForMods(const QStringList &labels) override { return labels.size(); }

    bool confirmReplaceModList() override { ++replaceAsked; return replaceAnswer; }
    bool ensureModsDirForActiveProfile() override { return !modsDirValue.isEmpty(); }
    int  createNewModlistProfile(const QString &) override { return -1; }
    void switchToModlistProfile(int) override {}

    QWidget *dialogParent() override { return nullptr; }
    void showStatus(const QString &msg, int) override { statuses << msg; }
    NotifyBanner *notify() override { return nullptr; }
    StickyKind stickyKind() const override { return sticky; }
    void setStickyKind(StickyKind kind) override { sticky = kind; }
    ScanOverlay *deployOverlay() override { return nullptr; }
    void triageOpenMWLog() override {}
    void reportResult(const QString &title, const QString &body, bool) override
    {
        reports.append({title, body});
    }
};
