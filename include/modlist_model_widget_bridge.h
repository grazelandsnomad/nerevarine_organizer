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

// Subscribe `model` to the signals of `list`'s own item model so it tracks
// the widget live: a row's change updates that row, an insert or remove
// applies to those rows, a move or reset rebuilds the list. Listening to the
// item model rather than to QListWidget::itemChanged means a QSignalBlocker
// on the widget (which the window uses around bulk edits) does not leave the
// model behind. The model follows the widget until Stage 3 inverts the
// direction; while it does, m_model->all() is the list as it stands.
//
// Idempotent: each call disconnects the previous wiring before reconnecting.
// Connection lifetime is otherwise tied to model destruction via Qt's
// auto-disconnect.
void connectAutoSync(ModlistModel *model, QListWidget *list);

} // namespace modlist
