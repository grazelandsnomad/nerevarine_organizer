// src/modentry.cpp
//
// Sort predicates and other non-widget helpers for ModEntry.  Kept
// deliberately free of <QListWidgetItem> so this translation unit links
// against QtCore alone - the unit test in tests/test_modentry.cpp relies
// on that to exercise sorting logic without pulling in QtWidgets.

#include "modentry.h"

#include <algorithm>
#include <limits>

bool lessByDisplayName(const ModEntry &a, const ModEntry &b)
{
    // Separators always trail mods.  Relative order among separators is
    // preserved by stable_sort; for plain std::sort the pair is treated
    // as equivalent.
    if (a.isSeparator() && b.isSeparator()) return false;
    if (a.isSeparator()) return false;
    if (b.isSeparator()) return true;
    return a.effectiveName().compare(b.effectiveName(), Qt::CaseInsensitive) < 0;
}

bool lessByDateAdded(const ModEntry &a, const ModEntry &b)
{
    if (a.isSeparator() && b.isSeparator()) return false;
    if (a.isSeparator()) return false;
    if (b.isSeparator()) return true;

    const bool av = a.dateAdded.isValid();
    const bool bv = b.dateAdded.isValid();
    // Mods with no recorded date fall to the end in either direction.
    if (!av && !bv) return false;
    if (!av) return false;
    if (!bv) return true;
    return a.dateAdded < b.dateAdded;
}

bool lessByModSize(const ModEntry &a, const ModEntry &b)
{
    if (a.isSeparator() && b.isSeparator()) return false;
    if (a.isSeparator()) return false;
    if (b.isSeparator()) return true;

    // Size is populated lazily (Settings → compute sizes).  Rows without a
    // known size trail the sorted ones, matching the existing UI rule.
    if (a.modSize <= 0 && b.modSize <= 0) return false;
    if (a.modSize <= 0) return false;
    if (b.modSize <= 0) return true;
    return a.modSize < b.modSize;
}

QList<int> canonicalOrderFromAnchors(const QList<qint64> &anchors)
{
    QList<int> order;
    order.reserve(anchors.size());
    for (int i = 0; i < anchors.size(); ++i)
        order.append(i);

    // stable_sort so unstamped rows (key == max) keep their relative order at
    // the end, and equal anchors (shouldn't happen for stamped rows) are stable.
    constexpr qint64 kLast = std::numeric_limits<qint64>::max();
    std::stable_sort(order.begin(), order.end(), [&anchors](int a, int b) {
        const qint64 ka = anchors[a] < 0 ? kLast : anchors[a];
        const qint64 kb = anchors[b] < 0 ? kLast : anchors[b];
        return ka < kb;
    });
    return order;
}
