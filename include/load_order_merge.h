#ifndef LOAD_ORDER_MERGE_H
#define LOAD_ORDER_MERGE_H

// Pure merge of an external load order (e.g. the OpenMW Launcher's openmw.cfg)
// into the in-memory m_loadOrder. Extracted from absorbExternalLoadOrder() so
// it can be tested without bringing up MainWindow.
//
// mergeLoadOrder(prev, cfg) returns a list where entries from `cfg` take the
// slots previously held by managed entries (in `cfg` order), entries in `prev`
// not in `cfg` keep their relative position among the managed ones, and
// entries new to `cfg` are appended in `cfg` order. No duplicates, no losses.

#include <QString>
#include <QStringList>

#include <functional>

namespace loadorder {

QStringList mergeLoadOrder(const QStringList &prev, const QStringList &cfg);

// Stable topological sort so every plugin's declared masters precede it.
// Pairs not bound by a master relationship keep input order. `mastersOf`
// returns each plugin's MAST list; matching is case-insensitive. Masters not
// in `order` are ignored (LoadOrderController owns missing-master detection).
// Cycles terminate without crashing: every input appears once, in an order
// that may violate the cycle's edges.
QStringList topologicallySortByMasters(
    const QStringList &order,
    std::function<QStringList(const QString &)> mastersOf);

} // namespace loadorder

#endif // LOAD_ORDER_MERGE_H
