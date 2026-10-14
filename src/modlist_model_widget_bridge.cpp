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

    // Drop previous wiring so a second connectAutoSync doesn't fire the
    // lambdas N times per change. Model is the receiver context so we only
    // disconnect our own connections.
    QObject::disconnect(list, nullptr, model, nullptr);
    if (auto *qm = list->model())
        QObject::disconnect(qm, nullptr, model, nullptr);

    auto resync = [model, list]() {
        refreshModelFromList(model, list);
    };

    // Whole-list shape changes: rebuild in one shot. Cheaper than tracking
    // partial deltas, and dodges ordering bugs when changes batch
    // (drag-reorder fires several rowsAboutToBeMoved + rowsMoved pairs).
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

    // Per-item changes (checkbox toggle, rename, setData role updates): that
    // one row, in place. A full resync here cost O(rows) per change, which a
    // size scan writing every row turned into O(rows^2) on the UI thread.
    QObject::connect(list, &QListWidget::itemChanged,
                     model, [model, list, resync](QListWidgetItem *item) {
        const int row = item ? list->row(item) : -1;
        if (row < 0 || row >= model->count()) { resync(); return; }
        model->update(row, ModEntry::fromItem(item));
    });
}

} // namespace modlist
