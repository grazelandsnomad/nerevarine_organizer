#include "modlist_model.h"

#include "nxmurl.h"

#include <QMimeData>

#include <algorithm>

ModlistModel::ModlistModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

// --- QAbstractItemModel ------------------------------------------------------

int ModlistModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant ModlistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.model() != this || !validRow(index.row()))
        return {};
    return m_entries[index.row()].roleData(role);
}

bool ModlistModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.model() != this) return false;
    return setRole(index.row(), role, value);
}

Qt::ItemFlags ModlistModel::flags(const QModelIndex &index) const
{
    // Like QListWidget's model: drops are allowed outside the rows (that is
    // how a drag lands between two of them), and a row answers for itself.
    if (!index.isValid() || index.model() != this || !validRow(index.row()))
        return Qt::ItemIsDropEnabled;
    const ModEntry &e = m_entries[index.row()];
    Qt::ItemFlags f = e.itemFlags;
    if (e.isSeparator()) f &= ~Qt::ItemIsUserCheckable;
    return f;
}

Qt::DropActions ModlistModel::supportedDropActions() const { return Qt::MoveAction; }
Qt::DropActions ModlistModel::supportedDragActions() const { return Qt::MoveAction; }

bool ModlistModel::moveRows(const QModelIndex &sourceParent, int sourceRow, int count,
                            const QModelIndex &destinationParent, int destinationChild)
{
    if (sourceParent.isValid() || destinationParent.isValid()) return false;
    if (count <= 0 || sourceRow < 0 || sourceRow + count > m_entries.size()) return false;
    if (destinationChild < 0 || destinationChild > m_entries.size()) return false;
    // Qt's own no-op rule: a destination inside or just past the block.
    if (!beginMoveRows(sourceParent, sourceRow, sourceRow + count - 1,
                       destinationParent, destinationChild))
        return false;
    const QList<ModEntry> block = m_entries.mid(sourceRow, count);
    m_entries.remove(sourceRow, count);
    const int insertAt = destinationChild > sourceRow ? destinationChild - count
                                                      : destinationChild;
    for (int i = 0; i < count; ++i) m_entries.insert(insertAt + i, block.at(i));
    touchIndex();
    endMoveRows();
    return true;
}

bool ModlistModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid()) return false;
    if (count <= 0 || row < 0 || row + count > m_entries.size()) return false;
    beginRemoveRows(parent, row, row + count - 1);
    m_entries.remove(row, count);
    touchIndex();
    endRemoveRows();
    return true;
}

bool ModlistModel::dropMimeData(const QMimeData *, Qt::DropAction, int, int, const QModelIndex &)
{
    // Only the view's move path (moveRows) may rearrange rows. Qt's fallback
    // would re-create rows from the mime payload, which carries no ModRole.
    return false;
}

// --- Rows ------------------------------------------------------------------------

int  ModlistModel::count()   const { return int(m_entries.size()); }
bool ModlistModel::isEmpty() const { return m_entries.isEmpty(); }

const ModEntry &ModlistModel::at(int row) const
{
    static const ModEntry empty;
    if (!validRow(row)) return empty;
    return m_entries[row];
}

QList<ModEntry> ModlistModel::all() const
{
    return m_entries;
}

void ModlistModel::ensureRowId(ModEntry &e)
{
    if (e.rowId.isNull()) e.rowId = QUuid::createUuid();
}

void ModlistModel::replace(QList<ModEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    for (ModEntry &e : m_entries) ensureRowId(e);
    touchIndex();
    endResetModel();
}

void ModlistModel::clear()
{
    replace({});
}

int ModlistModel::append(ModEntry e)
{
    const int row = int(m_entries.size());
    insertAt(row, std::move(e));
    return row;
}

void ModlistModel::insertAt(int row, ModEntry e)
{
    row = std::clamp(row, 0, int(m_entries.size()));
    ensureRowId(e);
    beginInsertRows(QModelIndex(), row, row);
    m_entries.insert(row, std::move(e));
    touchIndex();
    endInsertRows();
}

void ModlistModel::removeAt(int row)
{
    if (!validRow(row)) return;
    removeRows(row, 1);
}

ModEntry ModlistModel::takeAt(int row)
{
    if (!validRow(row)) return {};
    ModEntry e = m_entries[row];
    removeRows(row, 1);
    return e;
}

void ModlistModel::move(int from, int to)
{
    if (!validRow(from) || !validRow(to) || from == to) return;
    // moveRows takes the destination in pre-move numbering: to land AT `to`
    // when moving down, the row goes before to + 1.
    moveRows(QModelIndex(), from, 1, QModelIndex(), to > from ? to + 1 : to);
}

void ModlistModel::reorder(const QList<int> &order)
{
    const int n = int(m_entries.size());
    if (order.size() != n) return;
    QList<int> newRowOf(n, -1);
    for (int newRow = 0; newRow < n; ++newRow) {
        const int old = order.at(newRow);
        if (old < 0 || old >= n || newRowOf[old] != -1) return;   // not a permutation
        newRowOf[old] = newRow;
    }
    bool identity = true;
    for (int i = 0; i < n && identity; ++i) identity = (order.at(i) == i);
    if (identity) return;

    emit layoutAboutToBeChanged();
    QList<ModEntry> next;
    next.reserve(n);
    for (int old : order) next.append(m_entries.at(old));
    m_entries = std::move(next);
    touchIndex();
    // Persistent indexes (the view's selection and current row) follow the
    // rows they pointed at.
    const QModelIndexList from = persistentIndexList();
    QModelIndexList to;
    to.reserve(from.size());
    for (const QModelIndex &idx : from)
        to.append(idx.isValid() && idx.row() < n ? index(newRowOf[idx.row()]) : QModelIndex());
    changePersistentIndexList(from, to);
    emit layoutChanged();
}

void ModlistModel::update(int row, ModEntry e)
{
    if (!validRow(row)) return;
    if (e.rowId.isNull()) e.rowId = m_entries[row].rowId;
    if (m_entries[row] == e) return;
    if (m_entries[row].rowId != e.rowId) touchIndex();
    m_entries[row] = std::move(e);
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

bool ModlistModel::setRole(int row, int role, const QVariant &value)
{
    if (!validRow(row)) return false;
    ModEntry &e = m_entries[row];
    if (role == ModRole::RowId) touchIndex();
    if (!e.setRoleData(role, value)) return false;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {role});
    return true;
}

QVariant ModlistModel::role(int row, int role) const
{
    return validRow(row) ? m_entries[row].roleData(role) : QVariant();
}

void ModlistModel::setFlags(int row, Qt::ItemFlags flags)
{
    if (!validRow(row) || m_entries[row].itemFlags == flags) return;
    m_entries[row].itemFlags = flags;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

void ModlistModel::setHidden(int row, bool hidden)
{
    if (!validRow(row) || m_entries[row].hidden == hidden) return;
    m_entries[row].hidden = hidden;
    emit rowHiddenChanged(row, hidden);
}

// --- Identity -------------------------------------------------------------------

QUuid ModlistModel::rowId(int row) const
{
    return validRow(row) ? m_entries[row].rowId : QUuid();
}

void ModlistModel::reindex() const
{
    m_rowOfId.clear();
    m_rowOfId.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i)
        m_rowOfId.insert(m_entries[i].rowId, i);
    m_indexDirty = false;
}

int ModlistModel::findByRowId(const QUuid &row) const
{
    if (row.isNull()) return -1;
    if (m_indexDirty) reindex();
    return m_rowOfId.value(row, -1);
}

QModelIndex ModlistModel::indexOfRowId(const QUuid &row) const
{
    const int r = findByRowId(row);
    return r < 0 ? QModelIndex() : index(r);
}

// --- Lookups --------------------------------------------------------------------

int ModlistModel::findByNexusUrl(const QString &url) const
{
    if (url.isEmpty()) return -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].nexusUrl == url) return i;
    }
    return -1;
}

int ModlistModel::findByModPath(const QString &path) const
{
    if (path.isEmpty()) return -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].modPath == path) return i;
    }
    return -1;
}

int ModlistModel::findInstalledByModId(const QString &game, int modId,
                                       int exceptRow) const
{
    const QString gameLc = game.toLower();
    for (int i = 0; i < m_entries.size(); ++i) {
        if (i == exceptRow) continue;
        const ModEntry &e = m_entries[i];
        if (!e.isMod() || e.installStatus != 1) continue;
        const auto ref = parseNexusModUrl(e.nexusUrl);
        if (ref && ref->modId == modId && ref->game == gameLc)
            return i;
    }
    return -1;
}

QStringList ModlistModel::modDisplayNames() const
{
    QStringList out;
    for (const ModEntry &e : m_entries) {
        if (!e.isMod() || e.displayName.isEmpty()) continue;
        out.append(e.displayName);
    }
    return out;
}

QStringList ModlistModel::installedModDisplayNames() const
{
    QStringList out;
    for (const ModEntry &e : m_entries) {
        if (!e.isMod() || e.installStatus != 1 || e.displayName.isEmpty()) continue;
        out.append(e.displayName);
    }
    return out;
}

QStringList ModlistModel::installedNexusUrls() const
{
    QStringList out;
    for (const ModEntry &e : m_entries) {
        if (!e.isMod() || e.installStatus != 1 || e.nexusUrl.isEmpty()) continue;
        out.append(e.nexusUrl);
    }
    return out;
}

ModlistModel::ModCounts ModlistModel::modCounts() const
{
    ModCounts c;
    for (const ModEntry &e : m_entries) {
        if (!e.isMod()) continue;
        ++c.total;
        if (e.checked) ++c.active;
    }
    return c;
}
