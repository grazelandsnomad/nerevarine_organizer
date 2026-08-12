#include "load_order_merge.h"

#include <QHash>
#include <QSet>

namespace loadorder {

QStringList mergeLoadOrder(const QStringList &prev, const QStringList &cfg)
{
    QSet<QString> cfgSet(cfg.begin(), cfg.end());
    QStringList merged;
    int idx = 0;
    for (const QString &cf : prev) {
        if (cfgSet.contains(cf)) {
            // Slot belongs to a managed plugin - fill it with the next
            // cfg entry (in cfg-order), not with the prev entry.  This is
            // how the launcher's reorder propagates into our order.
            if (idx < cfg.size())
                merged.append(cfg[idx++]);
        } else {
            // Disabled plugin / not in cfg - keep it at its original slot.
            merged.append(cf);
        }
    }
    // New content= entries the launcher introduced since we last saved.
    while (idx < cfg.size()) {
        if (!merged.contains(cfg[idx])) merged.append(cfg[idx]);
        ++idx;
    }
    return merged;
}

QStringList topologicallySortByMasters(
    const QStringList &order,
    std::function<QStringList(const QString &)> mastersOf)
{
    // Case-insensitive index: lowercase name → exact spelling from input.
    QHash<QString, QString> exactByLower;
    exactByLower.reserve(order.size());
    for (const QString &n : order)
        exactByLower.insert(n.toLower(), n);

    QSet<QString> visited;   // lowercase names already emitted
    QSet<QString> onStack;   // lowercase names in the current DFS chain (cycle guard)
    QStringList out;
    out.reserve(order.size());

    // Iterative DFS with an explicit stack frame - modlists can be 500+
    // plugins deep in dependency chains, and recursive DFS would tempt fate
    // on the default thread stack size (ModLimitMap, Tamriel Data, etc.).
    struct Frame {
        QString  lc;            // lowercase name of this node
        QStringList masters;    // its masters (already fetched once)
        int      cursor = 0;    // next master index to visit
    };

    for (const QString &root : order) {
        const QString rootLc = root.toLower();
        if (visited.contains(rootLc)) continue;

        QList<Frame> stack;
        stack.append({rootLc, mastersOf(exactByLower.value(rootLc, root)), 0});
        onStack.insert(rootLc);

        while (!stack.isEmpty()) {
            Frame &top = stack.last();
            // Advance past any masters that are already visited / not in
            // scope / self-referential (malformed plugin) so we don't even
            // push frames for them.
            while (top.cursor < top.masters.size()) {
                const QString mLc = top.masters.at(top.cursor).toLower();
                ++top.cursor;
                if (mLc == top.lc)               continue;  // self-master
                if (!exactByLower.contains(mLc)) continue;  // master absent from order
                if (visited.contains(mLc))       continue;  // already emitted
                if (onStack.contains(mLc))       continue;  // cycle - skip back-edge
                // Push this master, then resume the outer loop so the inner
                // while exits and the new frame is processed.
                onStack.insert(mLc);
                stack.append({mLc, mastersOf(exactByLower.value(mLc)), 0});
                goto resume_outer;
            }
            // All masters handled - emit this node.
            visited.insert(top.lc);
            onStack.remove(top.lc);
            out.append(exactByLower.value(top.lc));
            stack.removeLast();
            resume_outer:;
        }
    }
    return out;
}

} // namespace loadorder
