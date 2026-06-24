#ifndef BULK_INSTALL_QUEUE_H
#define BULK_INSTALL_QUEUE_H

#include <QList>
#include <QObject>

#include <functional>

class QListWidget;
class QListWidgetItem;
class QTimer;

// Drip-feed throttle for "Install N selected mods". Kicking off N
// onInstallFromNexus calls in a tight loop hammers the Nexus rate-limiter
// and stacks up modal pickers. We append items to a queue and pop one
// every interval until the queue drains.
class BulkInstallQueue : public QObject {
    Q_OBJECT
public:
    // installFn is the actual per-item kick-off (MainWindow::onInstallFromNexus
    // in practice). The list pointer is used for indexFromItem validity checks
    // so rows removed between enqueue and tick are skipped silently.
    using InstallFn = std::function<void(QListWidgetItem *)>;

    BulkInstallQueue(QListWidget *list, InstallFn installFn,
                     QObject *parent = nullptr);

    // Append items, kick off the first immediately, then start the timer for
    // the rest. Re-queueing the same row twice is a no-op.
    void enqueue(const QList<QListWidgetItem *> &items);

signals:
    void statusMessage(const QString &msg, int timeoutMs);

private:
    void processTick();

    QListWidget               *m_list = nullptr;
    InstallFn                  m_installFn;
    QList<QListWidgetItem *>   m_queue;
    QTimer                    *m_timer = nullptr;
};

#endif // BULK_INSTALL_QUEUE_H
