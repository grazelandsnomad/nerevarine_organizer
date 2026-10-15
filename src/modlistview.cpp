#include "modlistview.h"

#include "modlist_model.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <QUrl>

#include <algorithm>

ModListView::ModListView(QWidget *parent)
    : QListView(parent)
{
    connect(this, &QAbstractItemView::doubleClicked, this,
            [this](const QModelIndex &index) { emit itemDoubleClicked(itemFromIndex(index)); });
}

void ModListView::setModel(QAbstractItemModel *model)
{
    auto *ml = qobject_cast<ModlistModel *>(model);
    if (model && !ml) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    m_model = ml;
    QListView::setModel(ml);
    if (!m_model) return;

    connect(m_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &tl, const QModelIndex &br, const QList<int> &) {
        for (int r = tl.row(); r <= br.row(); ++r) emit itemChanged(item(r));
    });
    connect(m_model, &ModlistModel::rowHiddenChanged, this,
            [this](int row, bool hidden) { setRowHidden(row, hidden); });
    // The model's moves, reported as reorders: one pair per move outside a
    // drop, one pair for the whole drop however many rows Qt moves in it.
    connect(m_model, &QAbstractItemModel::rowsAboutToBeMoved, this, [this] {
        if (!m_inDrop) { emit aboutToReorderRows(); return; }
        if (!m_dropAnnounced) { m_dropAnnounced = true; emit aboutToReorderRows(); }
    });
    connect(m_model, &QAbstractItemModel::rowsMoved, this, [this] {
        if (!m_inDrop) emit rowsReordered();
        else m_dropMoved = true;
    });
    // A reset forgets the view's hidden rows; an insert may bring a hidden one.
    connect(m_model, &QAbstractItemModel::modelReset, this,
            [this] { syncHiddenRows(0, m_model->count() - 1); });
    connect(m_model, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &, int first, int last) { syncHiddenRows(first, last); });
    syncHiddenRows(0, m_model->count() - 1);
}

void ModListView::syncHiddenRows(int first, int last)
{
    for (int r = first; r <= last; ++r)
        if (isRowHidden(r) != m_model->at(r).hidden) setRowHidden(r, m_model->at(r).hidden);
}

int ModListView::count() const { return m_model ? m_model->count() : 0; }

ModRow ModListView::item(int row) const
{
    if (!m_model || row < 0 || row >= m_model->count()) return nullptr;
    return ModRow(m_model, m_model->rowId(row));
}

ModRow ModListView::rowById(const QUuid &id) const
{
    if (!m_model || m_model->findByRowId(id) < 0) return nullptr;
    return ModRow(m_model, id);
}

ModRow ModListView::currentItem() const { return itemFromIndex(currentIndex()); }

void ModListView::setCurrentItem(const ModRow &row) { setCurrentRow(this->row(row)); }

void ModListView::setCurrentItem(const ModRow &row, QItemSelectionModel::SelectionFlags command)
{
    const QModelIndex idx = indexFromItem(row);
    if (idx.isValid()) selectionModel()->setCurrentIndex(idx, command);
}

int ModListView::currentRow() const { return currentIndex().row(); }

void ModListView::setCurrentRow(int row)
{
    if (!m_model) return;
    const QModelIndex idx = m_model->index(row);
    switch (selectionMode()) {
    case SingleSelection:
        selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect); break;
    case NoSelection:
        selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate); break;
    default:
        selectionModel()->setCurrentIndex(idx, QItemSelectionModel::SelectCurrent); break;
    }
}

QList<ModRow> ModListView::selectedItems() const
{
    QList<ModRow> out;
    if (!selectionModel()) return out;
    const QModelIndexList sel = selectionModel()->selectedIndexes();
    out.reserve(sel.size());
    for (const QModelIndex &idx : sel) out.append(itemFromIndex(idx));
    return out;
}

void ModListView::setRowSelected(const ModRow &row, bool selected)
{
    const QModelIndex idx = indexFromItem(row);
    if (idx.isValid())
        selectionModel()->select(idx, selected ? QItemSelectionModel::Select
                                               : QItemSelectionModel::Deselect);
}

ModRow ModListView::itemAt(const QPoint &p) const { return itemFromIndex(indexAt(p)); }

QRect ModListView::visualItemRect(const ModRow &row) const { return visualRect(indexFromItem(row)); }

QModelIndex ModListView::indexFromItem(const ModRow &row) const
{
    const int r = this->row(row);
    return r < 0 ? QModelIndex() : m_model->index(r);
}

ModRow ModListView::itemFromIndex(const QModelIndex &index) const
{
    if (!m_model || !index.isValid() || index.model() != m_model) return nullptr;
    return item(index.row());
}

void ModListView::scrollToItem(const ModRow &row, ScrollHint hint)
{
    const QModelIndex idx = indexFromItem(row);
    if (idx.isValid()) scrollTo(idx, hint);
}

ModRow ModListView::addItem(ModEntry e)
{
    if (!m_model) return nullptr;
    return item(m_model->append(std::move(e)));
}

ModRow ModListView::insertItem(int row, ModEntry e)
{
    if (!m_model) return nullptr;
    row = std::clamp(row, 0, m_model->count());
    m_model->insertAt(row, std::move(e));
    return item(row);
}

QList<ModRow> ModListView::moveRowsTo(QList<int> rows, int insertAt)
{
    QList<ModRow> moved;
    if (!m_model) return moved;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    rows.removeIf([this](int r) { return r < 0 || r >= m_model->count(); });
    if (rows.isEmpty()) return moved;
    QList<int> remaining;
    remaining.reserve(m_model->count() - rows.size());
    for (int i = 0; i < m_model->count(); ++i)
        if (!std::binary_search(rows.cbegin(), rows.cend(), i)) remaining.append(i);
    insertAt = std::clamp(insertAt, 0, int(remaining.size()));
    QList<int> order;
    order.reserve(m_model->count());
    for (int k = 0; k < insertAt; ++k) order.append(remaining.at(k));
    order += rows;
    for (int k = insertAt; k < remaining.size(); ++k) order.append(remaining.at(k));
    QList<QUuid> ids;
    ids.reserve(rows.size());
    for (int r : rows) ids.append(m_model->rowId(r));
    m_model->reorder(order);
    moved.reserve(ids.size());
    for (const QUuid &id : ids) moved.append(ModRow(m_model, id));
    return moved;
}

ModEntry ModListView::takeItem(int row) { return m_model ? m_model->takeAt(row) : ModEntry(); }

void ModListView::removeRow(int row) { if (m_model) m_model->removeAt(row); }

void ModListView::clear() { if (m_model) m_model->clear(); }

void ModListView::currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    QListView::currentChanged(current, previous);
    emit currentItemChanged(itemFromIndex(current), itemFromIndex(previous));
}

// --- Drops ------------------------------------------------------------------------

void ModListView::dragEnterEvent(QDragEnterEvent *event)
{
    // dragEnter/dragMove must acceptProposedAction or the drop never fires.
    if (event->source() != this && event->mimeData()->hasUrls()) {
        for (const QUrl &u : event->mimeData()->urls()) {
            if (u.isLocalFile()) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    QListView::dragEnterEvent(event);
}

void ModListView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->source() != this && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    QListView::dragMoveEvent(event);
}

void ModListView::dropEvent(QDropEvent *event)
{
    if (event->source() != this && event->mimeData()->hasUrls()) {
        // Accept now, hand over one tick later: Qt re-delivers a drop by
        // cursor position to the deepest widget - this one - and an install
        // started from in here recursed until the stack overflowed (Wayland,
        // a TTF for OpenMW dropped on the list).
        QStringList paths;
        for (const QUrl &u : event->mimeData()->urls())
            if (u.isLocalFile()) paths << u.toLocalFile();
        event->acceptProposedAction();
        if (!paths.isEmpty())
            QTimer::singleShot(0, this, [this, paths] { emit localFilesDropped(paths); });
        return;
    }

    const QModelIndex target = indexAt(event->position().toPoint());
    if (m_model && event->source() == this && target.isValid()
        && m_model->at(target.row()).isSeparator()) {
        // Dropped onto a section header: the selection joins that section,
        // right under the header, in its own top-to-bottom order.
        int sepRow = target.row();
        QList<int> rows;
        for (const QModelIndex &idx : selectionModel()->selectedIndexes())
            if (idx.row() != sepRow) rows.append(idx.row());
        if (rows.isEmpty()) { event->ignore(); return; }
        std::sort(rows.begin(), rows.end());
        // The block lands right under the header, counted in the list
        // without the rows that are moving.
        int insertAt = sepRow + 1;
        for (int r : rows) if (r < sepRow) --insertAt;
        emit aboutToReorderRows();
        moveRowsTo(rows, insertAt);
        emit rowsReordered();
        event->acceptProposedAction();
        return;
    }
    // A drag between rows: Qt calls moveRow once per selected row. Report
    // the whole drop as one reorder, so an eleven-row drag is one undo step
    // and not eleven snapshots that push the pre-drag state off the stack.
    m_inDrop = true;
    m_dropAnnounced = false;
    m_dropMoved = false;
    QListView::dropEvent(event);
    m_inDrop = false;
    if (m_dropMoved) emit rowsReordered();
}
