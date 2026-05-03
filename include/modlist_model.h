#pragma once

// ModlistModel - typed, QtCore-only collection of ModEntry rows with
// observation signals.  This is Stage 1 of decoupling MainWindow's modlist
// from QListWidget: we land the value-typed model alongside m_modList,
// keeping QListWidget as the visual source of truth, then migrate readers
// to consume the model and finally flip mutation direction so the model
// is the source of truth and the widget is a view.
//
// Why a class (not QList<ModEntry>):
//   · Insert/remove/update signals let future controllers react to model
//     changes without poking the QListWidget.
//   · Centralized invariants (e.g. row indices stay in [0, count)) catch
//     mutation bugs at one place instead of every call site.
//   · QObject lets it cross thread boundaries via QueuedConnection if a
//     long-running scan ever wants to drive UI updates.
//
// QtCore-only on purpose: the ModEntry value type already lives in
// QtCore (the QtWidgets bridge is in modentry_item.cpp), so a
// ModlistModel test can link without QtWidgets and verify business
// logic that currently lives inside MainWindow's god-object.
//
// Snapshot helpers:
//   snapshotModEntries / applyModEntries are free functions in
//   modlist_model_widget_bridge.h (separate TU, depends on QtWidgets).
//   They cross the model<->QListWidget boundary at call sites that need
//   to.  This keeps the ModlistModel header clean of widget includes.

#include "modentry.h"

#include <QList>
#include <QObject>

class ModlistModel : public QObject {
    Q_OBJECT
public:
    explicit ModlistModel(QObject *parent = nullptr);

    // Read access.  Const reference into the internal storage; valid
    // until the next mutation.  Out-of-range indices return a static
    // default-constructed entry rather than asserting, mirroring how
    // the QListWidget handles bad rows in the existing code.
    int  count() const;
    bool isEmpty() const;
    const ModEntry& at(int row) const;

    // Snapshot copy of every row, in current order.  Use this when you
    // need to iterate without holding a stale reference across a mutation.
    QList<ModEntry> all() const;

    // Whole-list replacement.  Emits `modelReset` instead of N row
    // signals so subscribers can re-render in one pass.  Used by
    // refreshFromList() and by load-from-file paths.
    void replace(QList<ModEntry> entries);

    // Mutators - each emits the matching signal AFTER the change lands
    // so subscribers observe a consistent model.
    int  append(ModEntry e);                 // returns the new row index
    void insertAt(int row, ModEntry e);
    void removeAt(int row);
    void move(int from, int to);
    void update(int row, ModEntry e);

    // Convenience finders used by callers that need to look up a row by
    // identity rather than position.  Both linear-scan; the modlist is
    // small enough (typically < 2000 entries) that a hash isn't worth
    // the maintenance cost.  Return -1 when not found.
    int findByNexusUrl(const QString &url) const;
    int findByModPath(const QString &path) const;

    // Aggregate counts used by the modlist status bar and several other
    // places that previously walked m_modList row-by-row.  Mods only -
    // separators are excluded from both counts.  `active` counts mods
    // whose `checked` field is true; `total` counts all mods.
    struct ModCounts { int total = 0; int active = 0; };
    [[nodiscard]] ModCounts modCounts() const;

signals:
    void modelReset();
    void rowsInserted(int row, int count);
    void rowsRemoved(int row, int count);
    void rowsMoved(int from, int to);
    void rowChanged(int row);

private:
    QList<ModEntry> m_entries;
};
