#pragma once

// ModlistModel - the mod list. A QAbstractListModel over QList<ModEntry>,
// the one copy of the rows: the view paints from it through the role table
// (ModEntry::roleData), the window reads and writes it through ModRow
// handles or by row number, and everything that answers later (a Nexus
// reply, a download, a queued install) names a row by its rowId, minted
// here on insertion and kept for the session.
//
// Structural changes go through begin/end* so a view follows them: moveRows
// is what QListView's InternalMove drag calls, reorder() is a whole
// permutation under layoutChanged (selection survives), replace() is a reset.
// dropMimeData is refused: the only drop path is the move, never Qt's
// encode/decode fallback, which would drop every ModRole on the floor.
//
// QtCore-only, so logic over the list links without QtWidgets.

#include "modentry.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QUuid>

class ModlistModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit ModlistModel(QObject *parent = nullptr);

    // --- QAbstractItemModel ---------------------------------------------
    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool     setData(const QModelIndex &index, const QVariant &value,
                     int role = Qt::EditRole) override;
    // The entry's flags; a separator is never user-checkable; the root (an
    // invalid index) accepts drops so a drag can land between rows.
    Qt::ItemFlags   flags(const QModelIndex &index) const override;
    Qt::DropActions supportedDropActions() const override;
    Qt::DropActions supportedDragActions() const override;
    bool moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                  const QModelIndex &destinationParent, int destinationChild) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

    // --- Rows --------------------------------------------------------------
    int  count() const;
    bool isEmpty() const;
    // Reference into internal storage; valid until the next mutation.
    // Out-of-range returns a static blank entry instead of asserting.
    const ModEntry &at(int row) const;
    // Copy of every row in order; use when iterating across a mutation.
    QList<ModEntry> all() const;

    // Whole-list replacement (a model reset). Entries keep the row ids they
    // carry, so an undo snapshot restores the rows a reply in flight was
    // addressed to; entries without one get one.
    void replace(QList<ModEntry> entries);
    void clear();

    // Each returns after the change landed and the view heard about it.
    int      append(ModEntry e);                  // the new row
    void     insertAt(int row, ModEntry e);       // clamped into [0, count]
    void     removeAt(int row);
    ModEntry takeAt(int row);                     // the entry, with its id, or blank
    // Row `from` ends up at row `to` (QList::move semantics).
    void     move(int from, int to);
    // `order[newRow]` = the current row that moves there. Ignored unless it
    // is a permutation of every row. One layoutChanged, not N moves.
    void     reorder(const QList<int> &order);
    // Whole-row replacement. A null rowId on `e` keeps the row's identity.
    // An identical entry is not a change and emits nothing.
    void     update(int row, ModEntry e);
    // One role; false (and nothing emitted) when the row or role is unknown.
    bool     setRole(int row, int role, const QVariant &value);
    QVariant role(int row, int role) const;
    // Presentation that is not a role: the item flags and the hidden state.
    void     setFlags(int row, Qt::ItemFlags flags);
    void     setHidden(int row, bool hidden);

    // --- Identity ------------------------------------------------------------
    // The row's session id; null only for a row that does not exist.
    QUuid rowId(int row) const;
    // Row with this id, -1 if none. Constant time.
    int   findByRowId(const QUuid &row) const;
    QModelIndex indexOfRowId(const QUuid &row) const;

    // --- Lookups ------------------------------------------------------------
    // Linear scans; the modlist is small (typically < 2000). -1 if not found.
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

    // Nexus mod-page URLs of every INSTALLED mod, for "does the user have mod
    // X?" checks that can match by id instead of by name. Rows with no stored
    // URL (hand-added folders, MO2 imports without one) are skipped, so an
    // absent id means "not found by URL", not "definitely not installed".
    QStringList installedNexusUrls() const;

    // Counts for the status bar. Mods only (separators excluded); `active` is
    // checked mods, `total` is all mods.
    struct ModCounts { int total = 0; int active = 0; };
    [[nodiscard]] ModCounts modCounts() const;

signals:
    // The hidden state is not a role, so it has its own notice for the view.
    void rowHiddenChanged(int row, bool hidden);

private:
    bool validRow(int row) const { return row >= 0 && row < m_entries.size(); }
    static void ensureRowId(ModEntry &e);
    void touchIndex() { m_indexDirty = true; }
    void reindex() const;

    QList<ModEntry>             m_entries;
    mutable QHash<QUuid, int>   m_rowOfId;
    mutable bool                m_indexDirty = true;
};
