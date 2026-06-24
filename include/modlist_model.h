#pragma once

// Typed, QtCore-only collection of ModEntry rows with observation signals.
// Stage 1 of decoupling MainWindow's modlist from QListWidget: model lands
// alongside m_modList (widget still the source of truth), then readers migrate
// to the model, then mutation flips so the model owns the data.
//
// A class, not QList<ModEntry>, because:
//   · insert/remove/update signals let controllers react without the widget;
//   · invariants (row in [0,count)) live in one place;
//   · QObject can cross threads via QueuedConnection for long scans.
//
// QtCore-only so tests link without QtWidgets (ModEntry's widget bridge is in
// modentry_item.cpp). The QListWidget snapshot helpers (snapshotModEntries /
// applyModEntries) live in modlist_model_widget_bridge.h to keep this header
// free of widget includes.

#include "modentry.h"

#include <QList>
#include <QObject>

class ModlistModel : public QObject {
    Q_OBJECT
public:
    explicit ModlistModel(QObject *parent = nullptr);

    // Reference into internal storage; valid until the next mutation.
    // Out-of-range returns a static blank entry instead of asserting, like
    // the existing QListWidget bad-row handling.
    int  count() const;
    bool isEmpty() const;
    const ModEntry& at(int row) const;

    // Copy of every row in order; use when iterating across a mutation.
    QList<ModEntry> all() const;

    // Whole-list replacement. Emits modelReset (not N row signals) so
    // subscribers re-render in one pass. Used by refreshFromList() and load.
    void replace(QList<ModEntry> entries);

    // Mutators - each emits its signal AFTER the change lands.
    int  append(ModEntry e);                 // returns the new row index
    void insertAt(int row, ModEntry e);
    void removeAt(int row);
    void move(int from, int to);
    void update(int row, ModEntry e);

    // Look up a row by identity instead of position. Linear scan; the modlist
    // is small (typically < 2000) so a hash isn't worth it. -1 if not found.
    int findByNexusUrl(const QString &url) const;
    int findByModPath(const QString &path) const;

    // Row of an INSTALLED mod (installStatus == 1) whose NexusUrl parses to
    // (game, modId). game matched case-insensitively (slugs stored lowercase).
    // exceptRow >= 0 is skipped, to ignore the placeholder being installed.
    // -1 if none. The "is this mod page already installed?" scan.
    int findInstalledByModId(const QString &game, int modId,
                             int exceptRow = -1) const;

    // Mod row display names in order; separators and empty names excluded.
    // installedModDisplayNames() narrows to installStatus == 1. Used for
    // duplicate-name hints (FOMOD wizard) and "is companion Y present?" checks.
    QStringList modDisplayNames() const;
    QStringList installedModDisplayNames() const;

    // Counts for the status bar. Mods only (separators excluded); `active` is
    // checked mods, `total` is all mods.
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
