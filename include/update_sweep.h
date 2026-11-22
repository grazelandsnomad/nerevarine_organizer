#ifndef UPDATE_SWEEP_H
#define UPDATE_SWEEP_H

// update_sweep - which rows a check still has to ask about, one by one.
//
// A check costs one Nexus request per installed mod: 409 on a large list.
// Nexus will also answer, in a single request, which of a game's mods
// changed at all in the last day, week or month:
//
//   GET /v1/games/morrowind/mods/updated.json?period=1m
//   [{"mod_id":42138,"latest_file_update":1795180708,
//     "latest_mod_activity":1795180761}, ...]
//
// Measured against the real endpoint on 2026-11-22: 55 entries for a day,
// 353 for a week, 1524 for a month. So one request can rule out almost
// every row of a list, and only the handful that actually moved need asking.
//
// The rule is a filter, never a verdict: this decides who to ask, and the
// per-mod reply still decides what is true. Getting it wrong by asking too
// much costs requests; getting it wrong by asking too little hides an
// update, so every uncertainty here resolves to "ask".
//
// Qt Core only, no I/O, no network.

#include <QDateTime>

namespace update_sweep {

// The period asked for, and how much of it may be relied on. The window is
// deliberately shorter than the period: Nexus says "1m", and a month is
// 28 to 31 days depending on which one, so trusting a full 30 would let a
// row checked 29 days ago be skipped against a list that never covered it.
constexpr const char *kPeriod           = "1m";
constexpr int         kPeriodCoveredSecs = 25 * 24 * 60 * 60;

// Does this row still need a request of its own?
//
//   listUsable  - the bulk reply arrived and parsed. When it did not, this
//                 is false and every row is asked, exactly as before.
//   inList      - the bulk reply named this mod.
//   latestActivitySecs - its newest activity, epoch seconds; ignored unless
//                 inList.
//
// Never checked, checked before the window the list covers, or a stamp in
// the future (a list synced from another machine, a clock put back) all mean
// ask: the list cannot speak about what happened before it starts.
bool mustAsk(const QDateTime &lastCheckedAt, const QDateTime &now,
             bool listUsable, bool inList, qint64 latestActivitySecs,
             int coveredSecs = kPeriodCoveredSecs);

} // namespace update_sweep

#endif // UPDATE_SWEEP_H
