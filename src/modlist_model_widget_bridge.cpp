#include "modlist_model_widget_bridge.h"

#include "modlist_model.h"

#include <QAbstractItemModel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QObject>

namespace modlist {

QList<ModEntry> snapshotEntries(const QListWidget *list)
{
    QList<ModEntry> out;
    if (!list) return out;
    out.reserve(list->count());
    for (int i = 0; i < list->count(); ++i) {
        if (const QListWidgetItem *it = list->item(i))
            out.append(ModEntry::fromItem(it));
    }
    return out;
}

void refreshModelFromList(ModlistModel *model, const QListWidget *list)
{
    if (!model) return;
    model->replace(snapshotEntries(list));
}

void connectAutoSync(ModlistModel *model, QListWidget *list)
{
    if (!model || !list) return;

    // Disconnect any previous wiring so calling connectAutoSync twice
    // doesn't end up firing the lambdas N times per change.  Use the
    // model as the receiver context so disconnect targets the right
    // connections only.
    QObject::disconnect(list, nullptr, model, nullptr);
    if (auto *qm = list->model())
        QObject::disconnect(qm, nullptr, model, nullptr);

    auto resync = [model, list]() {
        refreshModelFromList(model, list);
    };

    // Whole-list shape changes: rebuild the model in one shot.  Cheaper
    // than reasoning about partial deltas and immune to ordering issues
    // when multiple inserts/removes batch (drag-reorder fires several
    // rowsAboutToBeMoved + rowsMoved pairs in sequence).
    if (auto *qm = list->model()) {
        QObject::connect(qm, &QAbstractItemModel::rowsInserted,
                         model, [resync](const QModelIndex &, int, int) { resync(); });
        QObject::connect(qm, &QAbstractItemModel::rowsRemoved,
                         model, [resync](const QModelIndex &, int, int) { resync(); });
        QObject::connect(qm, &QAbstractItemModel::rowsMoved,
                         model, [resync](const QModelIndex &, int, int,
                                          const QModelIndex &, int) { resync(); });
        QObject::connect(qm, &QAbstractItemModel::modelReset,
                         model, resync);
        QObject::connect(qm, &QAbstractItemModel::layoutChanged,
                         model, [resync](const QList<QPersistentModelIndex> &,
                                          QAbstractItemModel::LayoutChangeHint) { resync(); });
    }

    // Per-item field changes (checkbox toggle, text rename, role updates
    // via setData) - resync just to keep things simple.  The cost is
    // O(rows) per item change, fine for the modlist sizes we deal with;
    // if profiling ever shows this hot we can swap to a per-row update.
    QObject::connect(list, &QListWidget::itemChanged,
                     model, [resync](QListWidgetItem *) { resync(); });
}

} // namespace modlist
