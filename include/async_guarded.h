#ifndef ASYNC_GUARDED_H
#define ASYNC_GUARDED_H

// async::guarded - one name for the off-thread-work-then-apply pattern that a
// dozen call sites used to hand-roll.
//
// The pattern everywhere was: capture a QPointer to `this`, QtConcurrent::run a
// worker, then QMetaObject::invokeMethod the result back onto the object's
// thread - each hop re-checking that the object is still alive. The re-check on
// the *second* hop is the easy one to forget, and forgetting it dangles a
// pointer during shutdown. This helper owns both hops so it can't be forgotten.
//
//   async::guarded(this,
//       [captures](Self *self) -> Result {   // runs on a QtConcurrent worker
//           return heavyComputation(...);      // touch only thread-safe members
//       },
//       [captures](Self *self, Result r) {    // runs on `this`'s own thread
//           self->applyResult(r);              // self guaranteed alive here
//       });
//
// Contract:
//   * work(self) runs on the thread pool. `self` is non-null when called, but
//     the object may be destroyed WHILE work runs, so work must touch only
//     thread-safe state (e.g. a mutex-guarded cache) - never widgets or plain
//     members. Mirrors what the old open-coded workers already assumed.
//   * then(self, result) runs on `obj`'s thread via a queued invocation, and
//     only if `obj` is still alive; `self` is non-null there. Moving the result
//     across the hop, never copying it.
//   * If `obj` dies before either hop, the result is dropped silently - which is
//     exactly what every hand-rolled site wanted (nobody left to deliver to).
//
// work may also return void, for a pure off-thread job with a UI-thread
// epilogue; then `then` takes just (self).
//
// Qt Core only (QtConcurrent + QPointer + QMetaObject); no Widgets, so callers
// in the moc-free controllers can use it too.

#include <QMetaObject>
#include <QPointer>
#include <QtConcurrent/QtConcurrent>

#include <type_traits>
#include <utility>

namespace async {

template <typename Obj, typename Work, typename Then>
void guarded(Obj *obj, Work work, Then then)
{
    QPointer<Obj> safe(obj);
    (void)QtConcurrent::run(
        [safe, work = std::move(work), then = std::move(then)]() mutable {
            using Result = std::invoke_result_t<Work &, Obj *>;

            // Object already gone before the worker even started: nothing to
            // deliver to, so skip the (possibly expensive) work entirely.
            if (!safe) return;

            if constexpr (std::is_void_v<Result>) {
                work(safe.data());
                if (!safe) return;                 // died during work
                QMetaObject::invokeMethod(safe.data(),
                    [safe, then = std::move(then)]() mutable {
                        if (!safe) return;         // died before the queued call ran
                        then(safe.data());
                    }, Qt::QueuedConnection);
            } else {
                Result result = work(safe.data());
                if (!safe) return;                 // died during work
                QMetaObject::invokeMethod(safe.data(),
                    [safe, result = std::move(result),
                     then = std::move(then)]() mutable {
                        if (!safe) return;         // died before the queued call ran
                        then(safe.data(), std::move(result));
                    }, Qt::QueuedConnection);
            }
        });
}

} // namespace async

#endif // ASYNC_GUARDED_H
