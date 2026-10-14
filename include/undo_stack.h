#ifndef UNDO_STACK_H
#define UNDO_STACK_H

#include <QList>
#include <QObject>
#include <QString>

#include "modentry.h"
#include "modrow.h"

class ModlistModel;

// Snapshots are ModEntry, the same value the modlist serializes, taken from
// and put back into the model as a whole. Row ids travel with them, so a
// reply in flight still finds its row after an undo.
//
// This used to be a hand-written ItemSnapshot carrying 18 of ModEntry's
// fields. Every field it did NOT carry was silently wiped by any undo - the
// declared dependency lists, the FOMOD and BAIN install choices, the Nexus id
// and title, the mod size, the video and source URLs. All of them are
// persisted, so an undo lost data a save had already written to disk.

class UndoStack : public QObject {
    Q_OBJECT
public:
    static constexpr int kUndoLimit = 10;

    explicit UndoStack(ModlistModel *model, QObject *parent = nullptr);

    bool isApplyingState() const { return m_applyingState; }

    void pushUndo();
    void performUndo();
    void performRedo();
    void clear();

signals:
    void requestCollapse(const ModRow &sep);
    void stateApplied();
    void statusMessage(const QString &msg, int timeoutMs);

private:
    QList<ModEntry> captureState() const;
    void            applyState(const QList<ModEntry> &state);

    ModlistModel                      *m_model = nullptr;
    QList<QList<ModEntry>>             m_undoStack;
    QList<QList<ModEntry>>             m_redoStack;
    bool                               m_applyingState = false;
};

#endif // UNDO_STACK_H
