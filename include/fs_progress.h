#pragma once

// FsProgress - how far a long file job has got. The job runs on a worker and
// writes it; the UI thread reads it on a timer to draw the row's bar. Hence
// atomics, and a percent that tolerates reading `done` and `total` a moment
// apart (it is clamped, and the next tick corrects it).
//
// Qt Core only.

#include <QtGlobal>

#include <atomic>

struct FsProgress {
    // What the job is doing, for the status line. Each phase counts afresh.
    enum class Phase {
        Staging,     // putting the picked files together
        Merging,     // overlaying them on an existing mod (a Merge)
        Finishing,   // moving the result into place, removing the unpacked archive
    };

    std::atomic<int>    phase{int(Phase::Staging)};
    std::atomic<qint64> done{0};
    std::atomic<qint64> total{0};   // 0: no count for this phase

    void begin(Phase p, qint64 newTotal)
    {
        done.store(0, std::memory_order_relaxed);
        total.store(newTotal, std::memory_order_relaxed);
        phase.store(int(p), std::memory_order_relaxed);
    }
    void add(qint64 n) { done.fetch_add(n, std::memory_order_relaxed); }

    Phase currentPhase() const { return Phase(phase.load(std::memory_order_relaxed)); }

    // 0-100, or -1 when the phase has no count (drawn as a moving stripe).
    int percent() const
    {
        const qint64 t = total.load(std::memory_order_relaxed);
        if (t <= 0) return -1;
        const qint64 d = done.load(std::memory_order_relaxed);
        return int(qBound(qint64(0), d * 100 / t, qint64(100)));
    }
};
