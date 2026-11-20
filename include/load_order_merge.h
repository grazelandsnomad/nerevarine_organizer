#ifndef LOAD_ORDER_MERGE_H
#define LOAD_ORDER_MERGE_H

// Pure merge of an external load order (e.g. the Launcher's openmw.cfg) into
// the window's order. Split out of absorbExternalLoadOrder() so it's testable without
// MainWindow.
//
// mergeLoadOrder(prev, cfg): `cfg` entries take the slots managed entries held
// (in cfg order); `prev` entries absent from cfg keep their relative position;
// entries new to cfg get appended in cfg order. No dups, no losses.

#include <QHash>
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

// One plugin order per load-order file.
//
// The window used to keep a single list and read it only at startup, so after
// a game switch it still held the previous game's plugins - and the deploy
// fingerprint hashed them, which is how switching away and back reported that
// a mod list had changed when nothing had. The file was always per game; only
// the copy in memory was not. Keyed by the file the list is read from and
// written to, the two cannot describe different games.
//
// A key nobody has put anything under reads as an empty list, which is what a
// game with no load-order file on disk has to look like.
class Store {
public:
    [[nodiscard]] bool has(const QString &key) const { return m_byKey.contains(key); }
    // Empty when absent, and without inserting: callers that only read must
    // not grow the map.
    [[nodiscard]] const QStringList &get(const QString &key) const;
    // The list to mutate in place, created empty when absent.
    QStringList &ref(const QString &key) { return m_byKey[key]; }
    void put(const QString &key, const QStringList &order) { m_byKey[key] = order; }
    // After a rename or a delete the old key would shadow the file it no
    // longer names, so profile churn drops entries rather than rewriting them.
    void forget(const QString &key) { m_byKey.remove(key); }
    void clear() { m_byKey.clear(); }
    [[nodiscard]] int size() const { return int(m_byKey.size()); }

private:
    QHash<QString, QStringList> m_byKey;
};

} // namespace loadorder

#endif // LOAD_ORDER_MERGE_H
