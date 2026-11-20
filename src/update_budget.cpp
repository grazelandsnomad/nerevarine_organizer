#include "update_budget.h"

namespace update_budget {

bool stillFresh(const QDateTime &lastCheckedAt, const QDateTime &now,
                int windowSecs)
{
    if (!lastCheckedAt.isValid() || !now.isValid()) return false;
    if (windowSecs <= 0) return false;
    const qint64 age = lastCheckedAt.secsTo(now);
    if (age < 0) return false;          // stamped in the future; ask anyway
    return age < windowSecs;
}

bool stopForQuota(int remaining, int floor)
{
    if (remaining < 0) return false;    // unknown is not empty
    return remaining <= floor;
}

} // namespace update_budget
