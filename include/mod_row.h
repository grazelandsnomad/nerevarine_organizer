#pragma once

// mod_row - typed reads and writes of the two roles every row check starts
// with: what kind of row this is, and how far along its install is. The
// roles store a string ("mod"/"separator") and an int (0/1/2), which is also
// what the modlist file holds; the string and integer compares that used to
// spell these out at a hundred-odd sites are these calls now. All of them
// take a null item and answer "no".

#include "modroles.h"

#include <QLatin1String>
#include <QListWidgetItem>
#include <QModelIndex>
#include <QUuid>
#include <QVariant>

namespace mod_row {

inline bool isMod(const QListWidgetItem *item)
{
    return item && item->data(ModRole::ItemType).toString() == QLatin1String(ItemType::Mod);
}

inline bool isSeparator(const QListWidgetItem *item)
{
    return item
        && item->data(ModRole::ItemType).toString() == QLatin1String(ItemType::Separator);
}

inline InstallStatus installStatusOf(const QListWidgetItem *item)
{
    return item ? InstallStatus(item->data(ModRole::InstallStatus).toInt())
                : InstallStatus::NotInstalled;
}

inline void setInstallStatus(QListWidgetItem *item, InstallStatus status)
{
    if (item) item->setData(ModRole::InstallStatus, int(status));
}

inline bool isInstalled(const QListWidgetItem *item)
{
    return installStatusOf(item) == InstallStatus::Installed;
}

inline bool isInstalling(const QListWidgetItem *item)
{
    return installStatusOf(item) == InstallStatus::Installing;
}

// The row's identity (ModRole::RowId) for anything that answers later - a
// Nexus reply, a download, a queued install. Minted on first use and kept for
// the session; carried through undo snapshots (ModEntry::rowId) but never
// written to disk, so a row rebuilt from the modlist file is a new row.
// Controllers and queues hold this, never the item, and MainWindow::rowById
// turns it back into a row at the moment of use - or into nothing, when the
// row is gone. A freed pointer can be reused by a new row; an id cannot.
inline QUuid rowId(QListWidgetItem *item)
{
    if (!item) return {};
    QUuid id = item->data(ModRole::RowId).toUuid();
    if (id.isNull()) {
        id = QUuid::createUuid();
        item->setData(ModRole::RowId, id);
    }
    return id;
}

// The identity a row already has, or a null id when nothing has asked for
// one yet - for lookups that must not mint.
inline QUuid rowIdIfAny(const QListWidgetItem *item)
{
    return item ? item->data(ModRole::RowId).toUuid() : QUuid();
}

// The same questions asked of a model index, for the delegate, which paints
// from the index rather than the item.
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
