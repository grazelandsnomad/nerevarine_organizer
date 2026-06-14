#pragma once

// placeholder_state - the named role transitions a modlist row goes through
// during an install (pending → installing → installed / cancelled / stranded).
//
// These transitions were open-coded and duplicated across MainWindow: the
// 4-flag "interactive" set appears ~7 times verbatim, the "roll back to not
// installed" sequence 3 times (resetPlaceholderAfterInstallCancel + the two
// stranded FOMOD/BAIN cancel branches), and the "mark installed" sequence in
// applyInstalledStateToStrandedPlaceholder + the merge-restore branch.  Pulling
// them here gives one tested home for the role-poking so a future tweak can't
// silently desync one copy.
//
// Operates on a QListWidgetItem* (QtWidgets) but needs no live QListWidget, so
// it's unit-testable against a standalone heap QListWidgetItem.

class QListWidgetItem;
class QString;

namespace placeholder_state {

// Full interactive flag set carried by a row that is NOT mid-install
// (enabled, selectable, draggable, user-checkable).
void restoreInteractiveFlags(QListWidgetItem *item);

// Restricted flag set while the install spinner runs: enabled + selectable
// only (no drag/check until the row settles).
void setBusyFlags(QListWidgetItem *item);

// Clear the transient mid-install hint roles once an install settles:
// IntendedModPath, PrevModPath, MergeTargetPath, InstallToken.
void clearInstallTransients(QListWidgetItem *item);

// Roll a row back to "not installed": status 0, drop the in-flight
// download/extract path + progress + install token, restore interactive flags,
// and recover a display name into CustomName (so a save/reload keeps it,
// falling back to `fallbackName` when no CustomName is set).  Does NOT persist;
// the caller owns saveModList / saveModListFor.
void resetToNotInstalled(QListWidgetItem *item, const QString &fallbackName);

// Roll a row forward to "installed at modPath": Mod type, status 1, set the
// path, restore flags, clear UpdateAvailable + the mid-install transients,
// (re)stamp DateAdded when this was an update or the date is unset, check it,
// and set the tooltip.  Recovers the display name from CustomName, falling back
// to the folder name.  Does NOT persist.  Tolerates a null item.
void markInstalled(QListWidgetItem *item, const QString &modPath);

} // namespace placeholder_state
