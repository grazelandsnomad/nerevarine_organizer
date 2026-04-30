#include "undo_stack.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QObject>

#include "modroles.h"

UndoStack::UndoStack(QListWidget *list, QObject *parent)
    : QObject(parent), m_list(list)
{
}

QList<ItemSnapshot> UndoStack::captureState() const
{
    QList<ItemSnapshot> state;
    state.reserve(m_list->count());
    for (int i = 0; i < m_list->count(); ++i) {
        auto *it = m_list->item(i);
        ItemSnapshot s;
        s.type = it->data(ModRole::ItemType).toString();
        s.text = it->text();
        if (s.type == ItemType::Separator) {
            s.bgColor   = it->data(ModRole::BgColor).value<QColor>();
            s.fgColor   = it->data(ModRole::FgColor).value<QColor>();
            s.collapsed = it->data(ModRole::Collapsed).toBool();
        } else {
            s.checkState      = it->checkState();
            s.modPath         = it->data(ModRole::ModPath).toString();
            s.customName      = it->data(ModRole::CustomName).toString();
            s.annotation      = it->data(ModRole::Annotation).toString();
            s.nexusUrl        = it->data(ModRole::NexusUrl).toString();
            s.dateAdded       = it->data(ModRole::DateAdded).toDateTime();
            s.installStatus   = it->data(ModRole::InstallStatus).toInt();
            s.updateAvailable = it->data(ModRole::UpdateAvailable).toBool();
            s.isUtility       = it->data(ModRole::IsUtility).toBool();
            s.isFavorite      = it->data(ModRole::IsFavorite).toBool();
        }
        state.append(s);
    }
    return state;
}

void UndoStack::applyState(const QList<ItemSnapshot> &state)
{
    m_applyingState = true;
    m_list->clear();
    for (const auto &s : state) {
        auto *it = new QListWidgetItem(s.text);
        it->setData(ModRole::ItemType, s.type);
        if (s.type == ItemType::Separator) {
            it->setData(ModRole::BgColor,   s.bgColor);
            it->setData(ModRole::FgColor,   s.fgColor);
            it->setData(ModRole::Collapsed, s.collapsed);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        } else {
            it->setData(ModRole::ModPath,       s.modPath);
            it->setData(ModRole::CustomName,    s.customName);
            it->setData(ModRole::Annotation,    s.annotation);
            it->setData(ModRole::NexusUrl,      s.nexusUrl);
            it->setData(ModRole::DateAdded,     s.dateAdded);
            it->setData(ModRole::InstallStatus, s.installStatus);
            it->setData(ModRole::UpdateAvailable, s.updateAvailable);
            it->setData(ModRole::IsUtility,   s.isUtility);
            it->setData(ModRole::IsFavorite,  s.isFavorite);
            it->setCheckState(s.checkState);
            it->setToolTip(s.modPath);
        }
        m_list->addItem(it);
    }
    // Re-apply collapsed sections after all items exist. The owner (MainWindow)
    // handles the actual hide/show via collapseSection() - we just announce
    // which separators need it.
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
