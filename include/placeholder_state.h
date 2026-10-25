#pragma once

// placeholder_state - the transitions a modlist row goes through during an
// install (pending -> installing -> installed / cancelled / stranded).
//
// These were duplicated across MainWindow: the 4-flag "interactive" set ~7x
// verbatim, "roll back to not installed" 3x, "mark installed" 2x. One home for
// the field-poking so a future tweak can't desync a copy.
//
// Each transition works on a ModEntry - a row being built, or a test's value
// - and on a ModRow, a live row of the model, which is read, changed and
// written back as one update.

#include "modentry.h"
#include "modrow.h"

#include <QString>
#include <QtGlobal>

namespace placeholder_state {

// Full interactive flag set for a row that is NOT mid-install (enabled,
// selectable, draggable, user-checkable).
void restoreInteractiveFlags(ModEntry &e);
void restoreInteractiveFlags(ModRow row);

// Restricted flags while the install spinner runs: enabled + selectable only
// (no drag/check until the row settles).
void setBusyFlags(ModEntry &e);
void setBusyFlags(ModRow row);

// Whether a row's folder is installed right now: an installed row (status 1),
// or one being reinstalled (status 2) - an update, a Replace, a Merge - whose
// folder is still on disk. That folder stays in the game until the new files
// land. Counted as gone, every save during a reinstall took its plugins out of
// the load order, with every plugin that needs them, and the install put them
// all back at the bottom: reinstalling Tamriel Data moved 337 of 413 plugins.
// A fresh install has no folder until it lands.
bool folderInstalled(int installStatus, const QString &modPath);
bool folderInstalled(const ModEntry &e);
bool folderInstalled(const ModRow &row);

// Clear the mid-install hint fields once an install settles: intendedModPath,
// prevModPath, mergeTargetPath, installToken, pendingFileId,
// awaitingUpdateDownload.
void clearInstallTransients(ModEntry &e);
void clearInstallTransients(ModRow row);

// What Nexus said about the file this row is installing, for verification:
// expectedMd5 / expectedSize, but only when expectedFileId names the file in
// flight (pendingFileId) - download_integrity::expectationsApply. Otherwise
// both come back empty, as if nothing had been stashed.
struct Expectations {
    QString md5;        // lower-case hex, or empty
    qint64  size = 0;   // bytes, or 0
};
Expectations applicableExpectations(const ModEntry &e);
Expectations applicableExpectations(const ModRow &row);

// Drop the stashed expectations and the file they were for: expectedMd5,
// expectedSize, expectedFileId and nexusFileName (the name the staged archive
// would be renamed to, which belongs to the same file).
void clearExpectations(ModEntry &e);
void clearExpectations(ModRow row);

// A row whose install did not land - cancelled, failed, abandoned - back to
// installed at the folder it still has, when it has one. An update, a Replace
// or a Merge leaves the old folder untouched until the new one lands, so
// cancelling one is no reason to call the mod "not installed": that cleared
// its path, orphaned the folder, and took its plugins out of the load order
// (cancel the package picker of a Tamriel Data update, and Tamriel Data was
// gone). The update mark, the install date and the installed file stay as
// they were - nothing new was installed. True when restored; false for a
// fresh install, with no folder to go back to, left for the caller to reset.
bool restoreInstalled(ModEntry &e);
bool restoreInstalled(ModRow row);

// Roll a row back to "not installed": status 0, drop the in-flight path +
// progress + install token, restore flags, recover a display name into
// customName (so reload keeps it, falling back to `fallbackName`). Does NOT
// persist; caller owns saveModList.
void resetToNotInstalled(ModEntry &e, const QString &fallbackName);
void resetToNotInstalled(ModRow row, const QString &fallbackName);

// Roll a row forward to "installed at modPath": Mod type, status 1, set path,
// restore flags, clear updateAvailable + transients, (re)stamp dateAdded on
// update or when unset, check it unless it was an update (an update keeps
// the mod enabled or disabled as it was), set the tool tip. Display name from
// customName, falling back to the folder name. Does NOT persist. A null row
// is left alone.
void markInstalled(ModEntry &e, const QString &modPath);
void markInstalled(ModRow row, const QString &modPath);

} // namespace placeholder_state
