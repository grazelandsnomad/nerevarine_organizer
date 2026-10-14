#pragma once

// mod_row - typed reads and writes of the two roles every row check starts
// with: what kind of row this is, and how far along its install is. The
// roles store a string ("mod"/"separator") and an int (0/1/2), which is also
// what the modlist file holds; the string and integer compares that used to
// spell these out at a hundred-odd sites are these calls now. They answer
// for a row handle (a gone or null row answers "no"), for an entry that is
// being built before insertion, and for a model index (the delegate).

#include "modentry.h"
#include "modroles.h"
#include "modrow.h"

#include <QLatin1String>
#include <QModelIndex>
#include <QUuid>
#include <QVariant>

namespace mod_row {

inline bool isMod(const ModRow &row)       { return row.isMod(); }
inline bool isSeparator(const ModRow &row) { return row.isSeparator(); }

inline InstallStatus installStatusOf(const ModRow &row)
{
    return InstallStatus(row.entry().installStatus);
}

inline void setInstallStatus(ModRow row, InstallStatus status)
{
    row.setData(ModRole::InstallStatus, int(status));
}

inline bool isInstalled(const ModRow &row)  { return row.isInstalled(); }
inline bool isInstalling(const ModRow &row) { return row.isInstalling(); }

// The row's identity (ModEntry::rowId) for anything that answers later - a
// Nexus reply, a download, a queued install. The model mints it on
// insertion and keeps it for the session; it travels through undo snapshots
// but is never written to disk, so a row rebuilt from the modlist file is a
// new row. Controllers and queues hold this, never the row, and
// MainWindow::rowById turns it back into a row at the moment of use - or
// into nothing, when the row is gone. Null for a null or gone row.
inline QUuid rowId(const ModRow &row)      { return row.isValid() ? row.id() : QUuid(); }
inline QUuid rowIdIfAny(const ModRow &row) { return rowId(row); }

// The same questions asked of an entry that is still being built.
inline bool isMod(const ModEntry &e)       { return e.isMod(); }
inline bool isSeparator(const ModEntry &e) { return e.isSeparator(); }
inline InstallStatus installStatusOf(const ModEntry &e) { return InstallStatus(e.installStatus); }
inline void setInstallStatus(ModEntry &e, InstallStatus status) { e.installStatus = int(status); }
inline bool isInstalled(const ModEntry &e)  { return e.isInstalled(); }
inline bool isInstalling(const ModEntry &e) { return e.isInstalling(); }

// And of a model index, for the delegate, which paints from the index.
inline bool isMod(const QModelIndex &index)
{
    return index.data(ModRole::ItemType).toString() == QLatin1String(ItemType::Mod);
}

inline bool isSeparator(const QModelIndex &index)
{
    return index.data(ModRole::ItemType).toString() == QLatin1String(ItemType::Separator);
}

inline InstallStatus installStatusOf(const QModelIndex &index)
{
    return InstallStatus(index.data(ModRole::InstallStatus).toInt());
}

inline bool isInstalled(const QModelIndex &index)
{
    return installStatusOf(index) == InstallStatus::Installed;
}

inline bool isInstalling(const QModelIndex &index)
{
    return installStatusOf(index) == InstallStatus::Installing;
}

} // namespace mod_row
