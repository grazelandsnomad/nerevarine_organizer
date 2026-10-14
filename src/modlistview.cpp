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
        // Rows above the header move down past it and shift it up by one
        // each; rows below move up to right after it. Walk the ones above
        // bottom-up and the ones below top-down so the numbers stay true.
        QList<int> above, below;
        for (int r : rows) (r < sepRow ? above : below).append(r);
        int placed = 0;   // rows already put under the header, from below
        for (int i = below.size() - 1; i >= 0; --i) {
            // Each move lands just after the header, so an earlier (higher)
            // row ends up above the ones moved before it.
            m_model->moveRows(QModelIndex(), below.at(i), 1, QModelIndex(), sepRow + 1);
            ++placed;
        }
        for (int i = above.size() - 1; i >= 0; --i) {
            // Move to just after the header; the header itself shifts up.
            m_model->moveRows(QModelIndex(), above.at(i), 1, QModelIndex(), sepRow + 1);
            --sepRow;
        }
        Q_UNUSED(placed);
        event->acceptProposedAction();
        return;
    }
    QListView::dropEvent(event);
}
