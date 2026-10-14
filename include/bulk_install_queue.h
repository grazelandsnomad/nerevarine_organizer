#ifndef BULK_INSTALL_QUEUE_H
#define BULK_INSTALL_QUEUE_H

#include <QList>
#include <QObject>
#include <QUuid>

#include <functional>

class QListWidgetItem;
class QTimer;

// Drip-feed throttle for "Install N selected mods". Kicking off N
// onInstallFromNexus calls in a tight loop hammers the Nexus rate-limiter
// and stacks up modal pickers. We append rows to a queue and pop one
// every interval until the queue drains.
//
// Rows are held by id (mod_row::rowId), never as items: a row removed
// between the click and its tick resolves to nothing and is skipped, and so
// is one that an undo rebuilt without carrying the id.
class BulkInstallQueue : public QObject {
    Q_OBJECT
public:
    // resolve turns a row id into the live row, or nullptr when it is gone
    // (MainWindow::rowById in practice). installFn is the actual per-row
    // kick-off (MainWindow::onInstallFromNexus), given the resolved row.
    using RowResolver = std::function<QListWidgetItem *(const QUuid &)>;
    using InstallFn   = std::function<void(QListWidgetItem *)>;

    BulkInstallQueue(RowResolver resolve, InstallFn installFn,
                     QObject *parent = nullptr);

    // Append rows, kick off the first immediately, then start the timer for
    // the rest. Re-queueing the same row twice is a no-op.
    void enqueue(const QList<QUuid> &rows);

signals:
    void statusMessage(const QString &msg, int timeoutMs);

private:
    void processTick();

    RowResolver   m_resolve;
    InstallFn     m_installFn;
    QList<QUuid>  m_queue;
    QTimer       *m_timer = nullptr;
};

#endif // BULK_INSTALL_QUEUE_H
