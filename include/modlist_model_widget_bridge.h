#pragma once

// Bridge between ModlistModel and QListWidget. Its own header so QtWidgets
// stays out of TUs that only treat the modlist as data. Use from MainWindow
// / install pipeline code that already pulls in QtWidgets.

#include "modentry.h"

#include <QList>

class QListWidget;
class ModlistModel;

namespace modlist {

// Snapshot every row of `list` to ModEntry (via ModEntry::fromItem) in row
// order. Doesn't mutate `list`.
QList<ModEntry> snapshotEntries(const QListWidget *list);

// Reset `model` to the current contents of `list`. Called at sync points
// (after loadModList, before saveModList) until the model is authoritative.
void refreshModelFromList(ModlistModel *model, const QListWidget *list);

// Subscribe `model` to `list`'s row/item-change signals so it tracks the
// widget live (checkbox toggles, drag-reorders, takeItem/addItem all flow
// through). The model follows the widget until Stage 3 inverts the
// direction.
//
// Idempotent: each call disconnects the previous wiring before reconnecting.
// Connection lifetime is otherwise tied to model destruction via Qt's
// auto-disconnect.
void connectAutoSync(ModlistModel *model, QListWidget *list);

} // namespace modlist
