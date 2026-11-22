#include "update_sweep.h"

namespace update_sweep {

bool mustAsk(const QDateTime &lastCheckedAt, const QDateTime &now,
             bool listUsable, bool inList, qint64 latestActivitySecs,
             int coveredSecs)
{
    // No usable list is the state every build before this one was in.
    if (!listUsable) return true;
    if (!lastCheckedAt.isValid() || !now.isValid()) return true;
    if (lastCheckedAt > now) return true;                 // clock skew
    if (lastCheckedAt.secsTo(now) > coveredSecs) return true;  // before the window
    if (!inList) return false;          // nothing happened to it in the period
    // Named by the list, so something happened - unless we looked after it.
    return lastCheckedAt.toSecsSinceEpoch() <= latestActivitySecs;
}

} // namespace update_sweep
