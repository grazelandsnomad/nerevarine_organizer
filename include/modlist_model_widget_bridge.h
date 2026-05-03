#pragma once

// Bridge between ModlistModel and QListWidget.  Lives in its own header so
// QtWidgets isn't a transitive dependency of every TU that just wants to
// reason about the modlist as data.  Use this from MainWindow / install
// pipeline code that already pulls in QtWidgets anyway.

#include "modentry.h"

#include <QList>

class QListWidget;
class ModlistModel;

namespace modlist {

// Walk every row in `list`, convert each QListWidgetItem to a ModEntry via
// ModEntry::fromItem, return the snapshot in row order.  Pure - doesn't
// mutate `list`.
QList<ModEntry> snapshotEntries(const QListWidget *list);

// Reset `model` to the current contents of `list`.  Used by MainWindow at
// strategic sync points (after loadModList, before saveModList) until the
// model becomes the authoritative source of truth.
void refreshModelFromList(ModlistModel *model, const QListWidget *list);

// Subscribe `model` to `list`'s row-change + item-change signals so the
// model mirrors the widget in real time (checkbox toggles, drag-reorders,
// programmatic insert/remove via takeItem/addItem all flow through).
// Connections own the model's update path; the model becomes
// authoritative-by-mirror until Stage 3 inverts the direction.
//
// Idempotent: safe to call multiple times - each call disconnects the
// previous wiring before reconnecting.  Returns the connection handles
// so the caller can dispose them on teardown if needed (lifetime is
// otherwise tied to model destruction via Qt's auto-disconnect).
void connectAutoSync(ModlistModel *model, QListWidget *list);

} // namespace modlist
