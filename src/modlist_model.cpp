#include "modlist_model.h"

#include "nxmurl.h"

ModlistModel::ModlistModel(QObject *parent)
    : QObject(parent)
{
}

int  ModlistModel::count()   const { return m_entries.size(); }
bool ModlistModel::isEmpty() const { return m_entries.isEmpty(); }

const ModEntry& ModlistModel::at(int row) const
{
    static const ModEntry empty;
    if (row < 0 || row >= m_entries.size()) return empty;
    return m_entries[row];
}

QList<ModEntry> ModlistModel::all() const
{
    return m_entries;
}

void ModlistModel::replace(QList<ModEntry> entries)
{
    m_entries = std::move(entries);
    emit modelReset();
}

int ModlistModel::append(ModEntry e)
{
    const int row = m_entries.size();
    m_entries.append(std::move(e));
    emit rowsInserted(row, 1);
    return row;
}

void ModlistModel::insertAt(int row, ModEntry e)
{
    if (row < 0)                  row = 0;
    if (row > m_entries.size())   row = m_entries.size();
    m_entries.insert(row, std::move(e));
    emit rowsInserted(row, 1);
}

void ModlistModel::removeAt(int row)
{
    if (row < 0 || row >= m_entries.size()) return;
    m_entries.removeAt(row);
    emit rowsRemoved(row, 1);
}

void ModlistModel::move(int from, int to)
{
    if (from < 0 || from >= m_entries.size()) return;
    if (to   < 0 || to   >= m_entries.size()) return;
    if (from == to) return;
    m_entries.move(from, to);
    emit rowsMoved(from, to);
}

void ModlistModel::update(int row, ModEntry e)
{
    if (row < 0 || row >= m_entries.size()) return;
    m_entries[row] = std::move(e);
    emit rowChanged(row);
}

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
