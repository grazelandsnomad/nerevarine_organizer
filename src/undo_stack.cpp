#include "undo_stack.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QObject>

#include "modroles.h"

UndoStack::UndoStack(QListWidget *list, QObject *parent)
    : QObject(parent), m_list(list)
{
}

QList<ModEntry> UndoStack::captureState() const
{
    QList<ModEntry> state;
    state.reserve(m_list->count());
    for (int i = 0; i < m_list->count(); ++i)
        state.append(ModEntry::fromItem(m_list->item(i)));
    return state;
}

void UndoStack::applyState(const QList<ModEntry> &state)
{
    m_applyingState = true;
    m_list->clear();
    for (const ModEntry &e : state) {
        auto *it = new QListWidgetItem;
        // applyToItem writes the text, the check state (mods only) and every
        // role. Note it runs BEFORE addItem: an item with no view emits no
        // itemChanged, which is what keeps an undo from tripping the
        // disable-warning seam and the conflict scan once per row.
        e.applyToItem(it);
        m_list->addItem(it);
    }
    // Re-collapse sections once every item exists. MainWindow does the actual
    // hide/show in collapseSection(); we just signal which separators need it.
    for (int i = 0; i < m_list->count(); ++i) {
        auto *sep = m_list->item(i);
        if (sep->data(ModRole::ItemType).toString() == ItemType::Separator
                && sep->data(ModRole::Collapsed).toBool())
            emit requestCollapse(sep);
    }
    m_applyingState = false;
    emit stateApplied();
}

void UndoStack::pushUndo()
{
    if (m_applyingState) return;
    m_undoStack.append(captureState());
    if (m_undoStack.size() > kUndoLimit)
        m_undoStack.removeFirst();
    m_redoStack.clear();
}

void UndoStack::performUndo()
{
    if (m_undoStack.isEmpty()) return;
    m_redoStack.append(captureState());
    if (m_redoStack.size() > kUndoLimit)
        m_redoStack.removeFirst();
    applyState(m_undoStack.takeLast());
    emit statusMessage(tr("Undo (%1 left)").arg(m_undoStack.size()), 1500);
}

void UndoStack::performRedo()
{
    if (m_redoStack.isEmpty()) return;
    m_undoStack.append(captureState());
    if (m_undoStack.size() > kUndoLimit)
        m_undoStack.removeFirst();
    applyState(m_redoStack.takeLast());
    emit statusMessage(tr("Redo (%1 left)").arg(m_redoStack.size()), 1500);
}

void UndoStack::clear()
{
    m_undoStack.clear();
    m_redoStack.clear();
}
