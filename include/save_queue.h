#ifndef SAVE_QUEUE_H
#define SAVE_QUEUE_H

// SaveQueue - serializes the app's save-side file writes (modlist file,
// openmw.cfg, launcher.cfg + their snapshot backups) onto ONE background
// thread, in submission order, without ever blocking the UI thread.
//
// It replaces the old "QFuture m_lastSaveFuture + waitForFinished() before
// every write" scheme.  That scheme kept at most one write in flight by
// *blocking the UI thread* on the previous write whenever a second save landed
// while the first was still hitting disk:
//
//     if (m_lastSaveFuture.isRunning())
//         m_lastSaveFuture.waitForFinished();   // <- UI stall, every save site
//     m_lastSaveFuture = QtConcurrent::run(...);
//
// i.e. two saves close together (a debounced timer firing just as the user
// hits Add/Edit, or a multi-row drag emitting a burst of rowsMoved) produced
// exactly the grey-freeze the async write existed to avoid.
//
// A max-one-thread pool gives the same guarantee the wait was there for - two
// writers never race on the same file, and writes land in submission order so
// the loadorder/openmw.cfg mtime relationship is preserved - while the UI
// thread only ever enqueues and returns immediately.
//
// Qt Core only; header-only.

#include <QRunnable>
#include <QThreadPool>

#include <functional>
#include <utility>

class SaveQueue {
public:
    SaveQueue()
    {
        // One thread => strict FIFO, one write at a time. Keep the thread warm
        // for the whole session (default 30 s expiry would respawn it on every
        // save burst) - it's a single idle thread, and save bursts are common.
        m_pool.setMaxThreadCount(1);
        m_pool.setExpiryTimeout(-1);
    }

    // Enqueue `task` to run on the save thread after every previously-enqueued
    // task. Never blocks the caller.
    void post(std::function<void()> task)
    {
        m_pool.start(QRunnable::create(std::move(task)));
    }

    // Block until the queue is empty and the running task (if any) has
    // finished. Only closeEvent() should call this - it guarantees the user's
    // last edits are on disk before the process exits. Everywhere else, the
    // point is NOT to wait.
    void drain() { m_pool.waitForDone(); }

private:
    QThreadPool m_pool;
};

#endif // SAVE_QUEUE_H
