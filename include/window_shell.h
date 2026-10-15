#pragma once

// WindowShell - what a flow may ask of the window.
//
// The flows (ImportFlow, DeployFlow, LibraryFlow, ...) are QObjects that own
// one concern each and talk to the window only through this interface: the
// rows (the model), the session (profile, paths, load order), persistence
// and the housekeeping the window does after a change, the install side it
// hands rows to, and the UI shell (a parent for dialogs, the status bar, the
// sticky banner, the deploy progress panel). MainWindow implements it; a
// test implements it over a ModlistModel and temp dirs with no widgets.
//
// Everything here is something several flows need. A thing only one flow
// needs stays in that flow.

#include "modentry.h"
#include "modrow.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUuid>

class ForbiddenModsRegistry;
class GameProfileRegistry;
class ModlistModel;
class NotifyBanner;
class QWidget;
class ScanOverlay;
class UndoStack;

// Which sticky banner is up, so a click on it routes to the right action.
enum class StickyKind { ViewSort, DeployHint, CaughtDownload };

class WindowShell {
public:
    virtual ~WindowShell() = default;

    // --- The rows and the session ---------------------------------------
    virtual ModlistModel &model() = 0;
    // A row of the list or of the rows parked across a profile switch.
    virtual ModRow rowById(const QUuid &row) const = 0;
    virtual GameProfileRegistry *profiles() = 0;
    virtual bool    hasProfile() const = 0;
    virtual QString currentGameId() const = 0;     // empty without a profile
    virtual QString currentGameName() const = 0;
    virtual QString modsDir() const = 0;
    // The library moved: the mirror, the download queue and the profile follow.
    virtual void    setModsDir(const QString &dir) = 0;
    virtual QStringList loadOrder() const = 0;
    virtual void        setLoadOrder(const QStringList &order) = 0;
    virtual QString     modlistPath() const = 0;
    virtual QList<int>      rowOrderForPersist() const = 0;
    virtual QList<ModEntry> snapshotEntriesForPersist() const = 0;
    virtual QList<QPair<QString, QList<ModEntry>>> otherProfileModlists() const = 0;
    virtual UndoStack             *undo() = 0;
    virtual ForbiddenModsRegistry *forbidden() = 0;
    virtual QString openmwPath() const = 0;
    virtual void    setOpenmwPath(const QString &path) = 0;
    virtual QString openmwLauncherPath() const = 0;
    virtual void    setOpenmwLauncherPath(const QString &path) = 0;

    // --- Persistence, and the housekeeping after a change ----------------
    virtual void saveModList() = 0;
    virtual void loadModList(const QString &path, const QString &remapFrom,
                             const QString &remapTo) = 0;
    virtual void saveLoadOrder() = 0;
    virtual void reconcileLoadOrder() = 0;
    virtual void syncGameConfig() = 0;
    virtual void scanMissingMasters() = 0;
    virtual void scheduleSizeScan() = 0;
    virtual void updateModCount() = 0;
    virtual void updateSectionCounts() = 0;
    virtual void scheduleConflictScan() = 0;
    virtual void repaintList() = 0;

    // --- The install side ------------------------------------------------
    // A folder on disk becomes a row (addModFromPath without a placeholder).
    virtual void installFolderAsMod(const QString &dirPath) = 0;
    virtual bool downloadsIdle() const = 0;
    virtual int  checkUpdatesForMods(const QStringList &labels) = 0;

    // --- Profiles ---------------------------------------------------------
    virtual bool confirmReplaceModList() = 0;
    virtual bool ensureModsDirForActiveProfile() = 0;
    virtual int  createNewModlistProfile(const QString &suggestedName) = 0;
    virtual void switchToModlistProfile(int idx) = 0;

    // --- The UI shell -----------------------------------------------------
    virtual QWidget *dialogParent() = 0;
    virtual void showStatus(const QString &msg, int ms) = 0;
    virtual NotifyBanner *notify() = 0;            // null without a window
    virtual StickyKind stickyKind() const = 0;
    virtual void setStickyKind(StickyKind kind) = 0;
    virtual ScanOverlay *deployOverlay() = 0;      // null without a window
    virtual void triageOpenMWLog() = 0;
    // The summary at the end of a long job: a message box in the window, a
    // record in a test. `warning` picks the icon.
    virtual void reportResult(const QString &title, const QString &body, bool warning) = 0;
};
