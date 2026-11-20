#ifndef UPDATE_BUDGET_H
#define UPDATE_BUDGET_H

// update_budget - what an update check is allowed to spend.
//
// A check costs one Nexus request per installed mod. On a 409-mod list that
// is 409 requests, and a user who clicked three times in ninety seconds spent
// about 1227 and was answered HTTP 429. Two questions decide the cost, and
// both are pure, so both are tested rather than argued about:
//
//   stillFresh   - may this row reuse the answer the last check got?
//   stopForQuota - should the run stop before its next request?
//
// Qt Core only, no I/O, no network.

#include <QDateTime>

namespace update_budget {

// How long an answer stands. A judgement, not a measurement: Nexus's own bulk
// update periods are a day, a week and a month, so a day is the granularity
// Nexus itself thinks in; an author cannot realistically publish and matter
// inside six hours; and a row that was skipped KEEPS its previous verdict, so
// a mod already carrying an arrow still carries it. Only "nothing found" goes
// stale, and six hours of that is a fair trade against 409 requests a click.
//
// The user can always ask for a full sweep - see the force flag on the check.
constexpr int kRecheckWindowSecs = 6 * 60 * 60;

// Enough of the allowance left to actually download something with. A scan
// that spends the last of it leaves the user unable to install what it found.
constexpr int kQuotaFloor = 50;

// Is the last answer for this row still worth reusing?
//
// Never checked is not fresh. A stamp in the future is not fresh either: a
// mod list synced from another machine, or a clock put back, must not freeze
// a row out of every check from here on.
bool stillFresh(const QDateTime &lastCheckedAt, const QDateTime &now,
                int windowSecs = kRecheckWindowSecs);

// Should the run stop rather than send its next request?
//
// `remaining` below zero means nothing is known about the allowance, which is
// never a reason to stop - that is the state every build before this one was
// in, and it has to keep working.
bool stopForQuota(int remaining, int floor = kQuotaFloor);

} // namespace update_budget

#endif // UPDATE_BUDGET_H
