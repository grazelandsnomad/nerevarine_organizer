#include "bulk_check.h"

namespace bulk_check {

QList<int> rowsToSet(const QList<ModEntry> &entries, const QList<int> &candidates,
                     bool enable)
{
    QList<int> out;
    out.reserve(candidates.size());
    for (int row : candidates) {
        if (row < 0 || row >= entries.size()) continue;
        const ModEntry &e = entries.at(row);
        if (!e.isMod()) continue;          // separators carry no check state
        if (e.checked == enable) continue; // already there; writing costs a pass
        out.append(row);
    }
    return out;
}

} // namespace bulk_check
