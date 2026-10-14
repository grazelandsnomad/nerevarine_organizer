#include "undo_stack.h"

#include "modlist_model.h"
#include "translator.h"

UndoStack::UndoStack(ModlistModel *model, QObject *parent)
    : QObject(parent), m_model(model)
{
}

QList<ModEntry> UndoStack::captureState() const
{
    return m_model->all();
}

void UndoStack::applyState(const QList<ModEntry> &state)
{
    m_applyingState = true;
    // One reset, not a row at a time: nothing hears an itemChanged per row,
    // which is what keeps an undo from tripping the disable-warning seam and
    // the conflict scan once per row.
    m_model->replace(state);
    // Re-collapse sections once every row exists. MainWindow does the actual
    // hide/show in collapseSection(); we just signal which separators need it.
    for (int i = 0; i < m_model->count(); ++i) {
        const ModEntry &e = m_model->at(i);
        if (e.isSeparator() && e.collapsed)
            emit requestCollapse(ModRow(m_model, e.rowId));
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
    emit statusMessage(T("status_undo").arg(m_undoStack.size()), 1500);
}

void UndoStack::performRedo()
{
    if (m_redoStack.isEmpty()) return;
    m_undoStack.append(captureState());
    if (m_undoStack.size() > kUndoLimit)
        m_undoStack.removeFirst();
    applyState(m_redoStack.takeLast());
    emit statusMessage(T("status_redo").arg(m_redoStack.size()), 1500);
}

void UndoStack::clear()
{
    m_undoStack.clear();
    m_redoStack.clear();
}
