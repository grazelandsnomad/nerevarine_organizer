#pragma once

// placeholder_state - the role transitions a modlist row goes through during
// an install (pending -> installing -> installed / cancelled / stranded).
//
// These were duplicated across MainWindow: the 4-flag "interactive" set ~7x
// verbatim, "roll back to not installed" 3x, "mark installed" 2x. One home for
// the role-poking so a future tweak can't desync a copy.
//
// Takes a QListWidgetItem* but needs no live QListWidget, so it's testable
// against a standalone heap item.

#include <QString>
#include <QtGlobal>

class QListWidgetItem;

namespace placeholder_state {

// Full interactive flag set for a row that is NOT mid-install (enabled,
// selectable, draggable, user-checkable).
void restoreInteractiveFlags(QListWidgetItem *item);

// Restricted flags while the install spinner runs: enabled + selectable only
// (no drag/check until the row settles).
void setBusyFlags(QListWidgetItem *item);

// Whether a row's folder is installed right now: an installed row (status 1),
// or one being reinstalled (status 2) - an update, a Replace, a Merge - whose
// folder is still on disk. That folder stays in the game until the new files
// land. Counted as gone, every save during a reinstall took its plugins out of
// the load order, with every plugin that needs them, and the install put them
// all back at the bottom: reinstalling Tamriel Data moved 337 of 413 plugins.
// A fresh install has no folder until it lands.
bool folderInstalled(int installStatus, const QString &modPath);
bool folderInstalled(const QListWidgetItem *item);

// Clear the mid-install hint roles once an install settles: IntendedModPath,
// PrevModPath, MergeTargetPath, InstallToken.
void clearInstallTransients(QListWidgetItem *item);

// What Nexus said about the file this row is installing, for verification:
// ExpectedMd5 / ExpectedSize, but only when ExpectedFileId names the file in
// flight (PendingFileId) - download_integrity::expectationsApply. Otherwise
// both come back empty, as if nothing had been stashed.
struct Expectations {
    QString md5;        // lower-case hex, or empty
    qint64  size = 0;   // bytes, or 0
};
Expectations applicableExpectations(const QListWidgetItem *item);

// Drop the stashed expectations and the file they were for: ExpectedMd5,
// ExpectedSize, ExpectedFileId and NexusFileName (the name the staged archive
// would be renamed to, which belongs to the same file).
void clearExpectations(QListWidgetItem *item);

// A row whose install did not land - cancelled, failed, abandoned - back to
// installed at the folder it still has, when it has one. An update, a Replace
// or a Merge leaves the old folder untouched until the new one lands, so
// cancelling one is no reason to call the mod "not installed": that cleared
// its path, orphaned the folder, and took its plugins out of the load order
// (cancel the package picker of a Tamriel Data update, and Tamriel Data was
// gone). The update mark, the install date and the installed file stay as
// they were - nothing new was installed. True when restored; false for a
// fresh install, with no folder to go back to, left for the caller to reset.
bool restoreInstalled(QListWidgetItem *item);

// Roll a row back to "not installed": status 0, drop the in-flight path +
// progress + install token, restore flags, recover a display name into
// CustomName (so reload keeps it, falling back to `fallbackName`). Does NOT
// persist; caller owns saveModList.
void resetToNotInstalled(QListWidgetItem *item, const QString &fallbackName);

// Roll a row forward to "installed at modPath": Mod type, status 1, set path,
// restore flags, clear UpdateAvailable + transients, (re)stamp DateAdded on
// update or when unset, check it, set tooltip. Display name from CustomName,
// falling back to the folder name. Does NOT persist. Tolerates a null item.
void markInstalled(QListWidgetItem *item, const QString &modPath);

} // namespace placeholder_state
