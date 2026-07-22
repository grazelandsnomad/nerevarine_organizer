// async::guarded - the off-thread-work-then-apply helper that replaced the
// hand-rolled QPointer + invokeMethod + re-check dance at a dozen call sites.
//
// Verifies the three properties the call sites rely on:
//   (a) work runs on a pool thread, then runs back on the object's own thread
//       with the moved result;
//   (b) the void-work overload delivers a bare epilogue the same way;
//   (c) if the object is destroyed WHILE work is still running, `then` is
//       dropped and nothing dereferences the dead object.
//
// (c) is the safety property the whole refactor hinges on, so it's driven
// deterministically: work blocks on a gate, the object is deleted, then the
// gate is released - the release/acquire on the gate orders the worker's
// liveness re-check strictly after the delete.

#include "async_guarded.h"
#include "save_queue.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QThread>

#include <atomic>
#include <iostream>

#include "test_harness.h"

// Plain QObject subclass - no signals/slots, so no moc needed. Only used for
// thread affinity, QPointer tracking, and as the queued-invocation context.
class Probe : public QObject {
public:
    std::atomic<QThread *> workThread{nullptr};
    std::atomic<QThread *> thenThread{nullptr};
    std::atomic<int>       gotResult{-1};
};

// Pump the event loop until `pred` holds or we give up (so a broken helper
// fails the test instead of hanging CI).
template <typename Pred>
static bool spinUntil(Pred pred, int maxMs = 5000)
{
    for (int waited = 0; waited < maxMs; waited += 5) {
        if (pred()) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    return pred();
}

static void testResultHopsBackToOwnerThread()
{
    std::cout << "testResultHopsBackToOwnerThread\n";
    auto *p = new Probe;

    async::guarded(p,
        [](Probe *self) -> int {
            self->workThread.store(QThread::currentThread());
            return 42;
        },
        [](Probe *self, int r) {
            self->gotResult.store(r);
            self->thenThread.store(QThread::currentThread());
        });

    const bool delivered = spinUntil([&] { return p->gotResult.load() >= 0; });
    check("then eventually ran", delivered);
    check("result value moved across the hop", p->gotResult.load() == 42,
          QString("got=%1").arg(p->gotResult.load()));
    check("work ran off the owner thread",
          p->workThread.load() != nullptr
              && p->workThread.load() != QThread::currentThread());
    check("then ran on the owner (main) thread",
          p->thenThread.load() == QThread::currentThread());
    delete p;
}

static void testVoidWorkEpilogue()
{
    std::cout << "testVoidWorkEpilogue\n";
    auto *p = new Probe;

    async::guarded(p,
        [](Probe *self) {                       // returns void
            self->workThread.store(QThread::currentThread());
        },
        [](Probe *self) {                       // epilogue takes just (self)
            self->gotResult.store(99);
            self->thenThread.store(QThread::currentThread());
        });

    const bool delivered = spinUntil([&] { return p->gotResult.load() >= 0; });
    check("void-work epilogue ran", delivered);
    check("epilogue ran on the owner thread",
          p->thenThread.load() == QThread::currentThread());
    delete p;
}

static void testDeadObjectDropsThen()
{
    std::cout << "testDeadObjectDropsThen\n";
    // Function-local statics, not heap: they must outlive the (possibly still
    // running) detached worker, and a `new` that is deliberately never freed
    // trips LeakSanitizer in the sanitizer CI build.
    static std::atomic<bool> gateStore{false};
    static std::atomic<bool> thenRanStore{false};
    auto *gate    = &gateStore;
    auto *thenRan = &thenRanStore;
    auto *q       = new Probe;

    async::guarded(q,
        [gate](Probe *) -> int {
            while (!gate->load(std::memory_order_acquire))
                QThread::yieldCurrentThread();   // stay in-flight until released
            return 7;
        },
        [thenRan](Probe *, int) { thenRan->store(true); });

    delete q;                                    // object dies mid-work
    gate->store(true, std::memory_order_release);// now let the worker finish

    // Give any (erroneously) posted epilogue ample chance to run.
    spinUntil([] { return false; }, 200);

    check("then dropped when the object died before completion", !thenRan->load());
    // gate / thenRan intentionally leaked: the detached worker may still touch
    // `gate` as it unwinds, and this is a one-shot test process.
}

// SaveQueue - the single-thread serialized write queue that replaced the
// "waitForFinished() before every save" UI stall. Verifies the two properties
// the save path leans on: (a) drain() runs every posted task to completion
// before returning (closeEvent's flush guarantee), and (b) tasks run strictly
// in submission order on the one thread (so two writers never race and mtime
// ordering holds). The staggered sleeps would surface reordering if the pool
// ever ran more than one task at a time.
static void testSaveQueueSerializesAndDrains()
{
    std::cout << "testSaveQueueSerializesAndDrains\n";
    SaveQueue q;
    std::atomic<int> ran{0};
    QMutex mu;
    QList<int> order;

    const int N = 24;
    for (int i = 0; i < N; ++i) {
        q.post([i, &ran, &mu, &order]() {
            QThread::msleep((i % 3) + 1);        // stagger so >1 thread would reorder
            QMutexLocker lk(&mu);
            order.append(i);
            ran.fetch_add(1, std::memory_order_relaxed);
        });
    }

    q.drain();   // must block until all N have executed

    check("drain ran every posted task", ran.load() == N,
          QString("ran=%1/%2").arg(ran.load()).arg(N));
    bool inOrder = (order.size() == N);
    for (int i = 0; inOrder && i < N; ++i)
        inOrder = (order[i] == i);
    check("tasks ran in submission order (single-thread FIFO)", inOrder);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "== test_async ==\n";
    testResultHopsBackToOwnerThread();
    testVoidWorkEpilogue();
    testDeadObjectDropsThen();
    testSaveQueueSerializesAndDrains();
    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
