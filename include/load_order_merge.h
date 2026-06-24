#ifndef LOAD_ORDER_MERGE_H
#define LOAD_ORDER_MERGE_H

// Pure merge of an external load order (e.g. the Launcher's openmw.cfg) into
// m_loadOrder. Split out of absorbExternalLoadOrder() so it's testable without
// MainWindow.
//
// mergeLoadOrder(prev, cfg): `cfg` entries take the slots managed entries held
// (in cfg order); `prev` entries absent from cfg keep their relative position;
// entries new to cfg get appended in cfg order. No dups, no losses.

#include <QString>
#include <QStringList>

#include <functional>

namespace loadorder {

QStringList mergeLoadOrder(const QStringList &prev, const QStringList &cfg);

// Stable topo sort so each plugin's declared masters precede it. Unrelated
// pairs keep input order. `mastersOf` returns a plugin's MAST list;
// case-insensitive. Masters not in `order` are ignored (LoadOrderController
// owns missing-master detection). Cycles don't crash: every input appears
// once, in an order that may violate the cycle's edges.
QStringList topologicallySortByMasters(
    const QStringList &order,
    std::function<QStringList(const QString &)> mastersOf);

} // namespace loadorder

#endif // LOAD_ORDER_MERGE_H
