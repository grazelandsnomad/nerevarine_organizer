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
