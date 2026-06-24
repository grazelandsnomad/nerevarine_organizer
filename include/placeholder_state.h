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

class QListWidgetItem;
class QString;

namespace placeholder_state {

// Full interactive flag set for a row that is NOT mid-install (enabled,
// selectable, draggable, user-checkable).
void restoreInteractiveFlags(QListWidgetItem *item);

// Restricted flags while the install spinner runs: enabled + selectable only
// (no drag/check until the row settles).
void setBusyFlags(QListWidgetItem *item);

// Clear the mid-install hint roles once an install settles: IntendedModPath,
// PrevModPath, MergeTargetPath, InstallToken.
void clearInstallTransients(QListWidgetItem *item);

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
