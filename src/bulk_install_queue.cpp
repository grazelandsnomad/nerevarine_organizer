#include "bulk_install_queue.h"

#include <QListWidgetItem>
#include <QSet>
#include <QString>
#include <QTimer>

#include "mod_row.h"
#include "modroles.h"
#include "translator.h"

BulkInstallQueue::BulkInstallQueue(RowResolver resolve, InstallFn installFn,
                                   QObject *parent)
    : QObject(parent), m_resolve(std::move(resolve)), m_installFn(std::move(installFn))
{
    // 1.2 s between Nexus API kickoffs. Long enough that the Nexus rate-
    // limiter and modal file-picker dialogs don't pile up; short enough
    // that 100 mods drain in ~2 minutes.
    m_timer = new QTimer(this);
    m_timer->setInterval(1200);
    connect(m_timer, &QTimer::timeout, this, &BulkInstallQueue::processTick);
}

void BulkInstallQueue::enqueue(const QList<QUuid> &rows)
{
    if (rows.isEmpty()) return;
    // Fast lookup of what's already pending so a second context-menu bulk
    // action doesn't double-queue rows that are still waiting their turn.
    QSet<QUuid> pending(m_queue.begin(), m_queue.end());
    for (const QUuid &row : rows) {
        if (row.isNull() || pending.contains(row)) continue;
        m_queue.append(row);
        pending.insert(row);
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
        const QUuid row = m_queue.takeFirst();
        // Row may have been deleted, already installed, or its URL cleared
        // since the user clicked bulk-install - skip and try the next.
        QListWidgetItem *item = m_resolve ? m_resolve(row) : nullptr;
        if (!item) continue;
        if (!mod_row::isMod(item)) continue;
        if (mod_row::installStatusOf(item) != InstallStatus::NotInstalled) continue;
        if (item->data(ModRole::NexusUrl).toString().isEmpty()) continue;
        m_installFn(item);
        break; // one per tick
    }
    if (m_queue.isEmpty())
        m_timer->stop();
}
