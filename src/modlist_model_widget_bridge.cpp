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
    auto *qm = list->model();
    if (!qm) return;

    // Drop previous wiring so a second connectAutoSync doesn't fire the
    // lambdas N times per change. Model is the receiver context so we only
    // disconnect our own connections.
    QObject::disconnect(list, nullptr, model, nullptr);
    QObject::disconnect(qm, nullptr, model, nullptr);

    auto resync = [model, list]() {
        refreshModelFromList(model, list);
    };

    // Everything comes from the widget's own item model, not from
    // QListWidget::itemChanged and friends: a QSignalBlocker on the widget -
    // which the window uses to keep its own itemChanged handler out of a bulk
    // edit - silences the widget's signals, not its model's, so the typed
    // model stays faithful through those edits too.
    //
    // Inserts, removes and per-row changes are applied one row at a time;
    // the count checks catch a model that was wired up late or has drifted,
    // and rebuild it instead of applying a delta to the wrong shape.
    QObject::connect(qm, &QAbstractItemModel::rowsInserted, model,
                     [model, list, resync](const QModelIndex &, int first, int last) {
        const int added = last - first + 1;
        if (model->count() + added != list->count()) { resync(); return; }
        for (int r = first; r <= last; ++r)
            model->insertAt(r, ModEntry::fromItem(list->item(r)));
    });
    QObject::connect(qm, &QAbstractItemModel::rowsRemoved, model,
                     [model, list, resync](const QModelIndex &, int first, int last) {
        const int removed = last - first + 1;
        if (model->count() - removed != list->count()) { resync(); return; }
        for (int r = last; r >= first; --r) model->removeAt(r);
    });
    QObject::connect(qm, &QAbstractItemModel::dataChanged, model,
                     [model, list, resync](const QModelIndex &topLeft,
                                           const QModelIndex &bottomRight,
                                           const QList<int> &) {
        if (!topLeft.isValid() || model->count() != list->count()) { resync(); return; }
        for (int r = topLeft.row(); r <= bottomRight.row(); ++r)
            model->update(r, ModEntry::fromItem(list->item(r)));
    });

    // The widget re-emits its model's dataChanged as itemChanged, and its own
    // connection was made in its constructor - before this one - so by the
    // time the window's itemChanged slots run, the dataChanged handler above
    // has not yet. Subscribing to itemChanged as well, ahead of the window
    // (connectAutoSync runs right after the widget is built), lands the
    // update first; the handler above then repeats it, which is harmless.
    // Under a QSignalBlocker only the dataChanged path fires.
    QObject::connect(list, &QListWidget::itemChanged, model,
                     [model, list, resync](QListWidgetItem *item) {
        const int r = list->row(item);
        if (r < 0 || model->count() != list->count()) { resync(); return; }
        model->update(r, ModEntry::fromItem(item));
    });

    // Moves, resets and layout changes rebuild in one shot: a drag-reorder
    // fires several rowsAboutToBeMoved + rowsMoved pairs, and a block move's
    // bookkeeping is not worth getting subtly wrong for something this rare.
    QObject::connect(qm, &QAbstractItemModel::rowsMoved,
                     model, [resync](const QModelIndex &, int, int,
                                      const QModelIndex &, int) { resync(); });
    QObject::connect(qm, &QAbstractItemModel::modelReset,
                     model, resync);
    QObject::connect(qm, &QAbstractItemModel::layoutChanged,
                     model, [resync](const QList<QPersistentModelIndex> &,
                                      QAbstractItemModel::LayoutChangeHint) { resync(); });
}

} // namespace modlist
