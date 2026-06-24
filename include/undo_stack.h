#ifndef UNDO_STACK_H
#define UNDO_STACK_H

#include <QColor>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <Qt>

class QListWidget;
class QListWidgetItem;

struct ItemSnapshot {
    QString        type;
    QString        text;
    Qt::CheckState checkState  = Qt::Unchecked;
    QString        modPath;
    QString        customName;
    QString        annotation;
    QString        nexusUrl;
    QDateTime      dateAdded;
    int            installStatus = 0;
    bool           updateAvailable = false;
    bool           isUtility = false;
    bool           isFavorite = false;
    QColor         bgColor;
    QColor         fgColor;
    bool           collapsed = false;
};

class UndoStack : public QObject {
    Q_OBJECT
public:
    static constexpr int kUndoLimit = 10;

    explicit UndoStack(QListWidget *list, QObject *parent = nullptr);

    bool isApplyingState() const { return m_applyingState; }

    void pushUndo();
    void performUndo();
    void performRedo();
    void clear();

signals:
    void requestCollapse(QListWidgetItem *sep);
    void stateApplied();
    void statusMessage(const QString &msg, int timeoutMs);

private:
    QList<ItemSnapshot> captureState() const;
    void                applyState(const QList<ItemSnapshot> &state);

    QListWidget                       *m_list = nullptr;
    QList<QList<ItemSnapshot>>         m_undoStack;
    QList<QList<ItemSnapshot>>         m_redoStack;
    bool                               m_applyingState = false;
};

#endif // UNDO_STACK_H
