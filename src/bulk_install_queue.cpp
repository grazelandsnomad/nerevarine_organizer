#include "bulk_install_queue.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QSet>
#include <QString>
#include <QTimer>

#include "modroles.h"
#include "translator.h"

BulkInstallQueue::BulkInstallQueue(QListWidget *list, InstallFn installFn,
                                    QObject *parent)
    : QObject(parent), m_list(list), m_installFn(std::move(installFn))
{
    // 1.2 s between Nexus API kickoffs. Long enough that the Nexus rate-
    // limiter and modal file-picker dialogs don't pile up; short enough
    // that 100 mods drain in ~2 minutes.
    m_timer = new QTimer(this);
    m_timer->setInterval(1200);
    connect(m_timer, &QTimer::timeout, this, &BulkInstallQueue::processTick);
}

void BulkInstallQueue::enqueue(const QList<QListWidgetItem *> &items)
{
    if (items.isEmpty()) return;
    // Fast lookup of what's already pending so a second context-menu bulk
    // action doesn't double-queue rows that are still waiting their turn.
    QSet<QListWidgetItem *> pending(m_queue.begin(), m_queue.end());
    for (auto *it : items) {
        if (!it) continue;
        if (pending.contains(it)) continue;
        m_queue.append(it);
        pending.insert(it);
    }
    if (!m_timer->isActive()) {
        processTick();
        if (!m_queue.isEmpty())
            m_timer->start();
    }
    if (!m_queue.isEmpty())
        emit statusMessage(
            T("status_bulk_install_queued").arg(m_queue.size()), 4000);
}

void BulkInstallQueue::processTick()
{
    while (!m_queue.isEmpty()) {
        auto *item = m_queue.takeFirst();
        if (!item) continue;
        // Row may have been deleted, already installed, or its URL cleared
        // since the user clicked bulk-install - skip and try the next.
        if (!m_list->indexFromItem(item).isValid()) continue;
        if (item->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (item->data(ModRole::InstallStatus).toInt() != 0) continue;
        if (item->data(ModRole::NexusUrl).toString().isEmpty()) continue;
        m_installFn(item);
        break; // one per tick
    }
    if (m_queue.isEmpty())
        m_timer->stop();
}
