#pragma once

// ModListView - the mod list on screen: a QListView over ModlistModel, plus
// the slice of QListWidget's surface the window uses, over ModRow handles,
// so the window's code keeps its shape (item(i), row(item), selectedItems(),
// takeItem(i), ...) while the rows live in the model. Rows marked hidden in
// the model are hidden here. The three row signals the window listens to
// (itemChanged, currentItemChanged, itemDoubleClicked) carry handles.
//
// Drops: files dragged in from outside are handed to the window one event
// loop tick later as localFilesDropped (installing from inside the drop
// event re-entered the view and overflowed the stack on Wayland); a
// selection dropped onto a separator joins that section
// (dropSelectionOnSeparator), and the drop is then handed to QListView so
// Qt knows the rows were moved - see dropEvent.

#include "modentry.h"
#include "modrow.h"

#include <QItemSelectionModel>
#include <QList>
#include <QListView>
#include <QStringList>

class ModlistModel;

class ModListView : public QListView {
    Q_OBJECT
public:
    explicit ModListView(QWidget *parent = nullptr);

    // The model must be a ModlistModel; anything else is refused.
    void setModel(QAbstractItemModel *model) override;
    [[nodiscard]] ModlistModel *modlist() const { return m_model; }

    [[nodiscard]] int    count() const;
    [[nodiscard]] ModRow item(int row) const;
    [[nodiscard]] int    row(const ModRow &row) const { return row.model() == m_model ? row.row() : -1; }
    [[nodiscard]] ModRow rowById(const QUuid &id) const;

    [[nodiscard]] ModRow currentItem() const;
    // Like QListWidget: with an extended selection the row joins the
    // selection (SelectCurrent); the two-argument form takes the command.
    void setCurrentItem(const ModRow &row);
    void setCurrentItem(const ModRow &row, QItemSelectionModel::SelectionFlags command);
    [[nodiscard]] int currentRow() const;
    void setCurrentRow(int row);
    [[nodiscard]] QList<ModRow> selectedItems() const;
    void setRowSelected(const ModRow &row, bool selected);

    [[nodiscard]] ModRow      itemAt(const QPoint &p) const;
    [[nodiscard]] QRect       visualItemRect(const ModRow &row) const;
    [[nodiscard]] QModelIndex indexFromItem(const ModRow &row) const;
    [[nodiscard]] ModRow      itemFromIndex(const QModelIndex &index) const;
    void scrollToItem(const ModRow &row, ScrollHint hint = EnsureVisible);

    // Move `rows` (any order, duplicates ignored) to sit together at
    // `insertAt`, counted in the list without them, keeping their own
    // top-to-bottom order. One reorder, so the selection and the hidden rows
    // follow; returns the handles of the moved rows, top to bottom.
    QList<ModRow> moveRowsTo(QList<int> rows, int insertAt);

    // A selection dropped onto the separator at `sepRow`: the visible
    // selected mods move to sit right under that header, in their own
    // top-to-bottom order. Hidden rows (inside a collapsed section) and
    // separators stay where they are, selected or not. One reorder, reported
    // as aboutToReorderRows / rowsReordered(moved); returns the moved rows.
    QList<ModRow> dropSelectionOnSeparator(int sepRow);

    // The rows. addItem/insertItem return the handle of the new row.
    ModRow   addItem(ModEntry e);
    ModRow   insertItem(int row, ModEntry e);
    ModEntry takeItem(int row);
    void     removeRow(int row);
    void     clear();

signals:
    void itemChanged(const ModRow &row);
    void currentItemChanged(const ModRow &current, const ModRow &previous);
    void itemDoubleClicked(const ModRow &row);
    // Local files dropped from outside, one tick after the drop.
    void localFilesDropped(const QStringList &paths);
    // Web links (http/https) dropped from outside, a tick later as well.
    void webLinksDropped(const QStringList &urls);
    // Around every reorder of the rows: a drag between rows (one pair however
    // many rows it moves - Qt moves them one at a time), a selection dropped
    // onto a separator, a programmatic model move. The window takes one undo
    // step before and one save after. `moved` holds the rows a drop moved
    // (empty for a programmatic move), so the window can keep them in view.
    void aboutToReorderRows();
    void rowsReordered(const QList<ModRow> &moved);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;

private:
    void syncHiddenRows(int first, int last);
    // The selected rows a drop acts on: visible ones only, top to bottom.
    [[nodiscard]] QList<int> visibleSelectedRows() const;

    ModlistModel *m_model = nullptr;
    // A drop in progress: the model's per-row moves are reported as one.
    bool m_inDrop = false;
    bool m_dropAnnounced = false;
    bool m_dropMoved = false;
};
