#include "placeholder_state.h"

#include "modroles.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QString>
#include <QVariant>

namespace placeholder_state {

void restoreInteractiveFlags(QListWidgetItem *item)
{
    if (!item) return;
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                   Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable);
}

void setBusyFlags(QListWidgetItem *item)
{
    if (!item) return;
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
}

bool folderInstalled(int installStatus, const QString &modPath)
{
    if (installStatus == 1) return true;
    return installStatus == 2 && !modPath.isEmpty() && QFileInfo(modPath).isDir();
}

bool folderInstalled(const QListWidgetItem *item)
{
    return item
        && folderInstalled(item->data(ModRole::InstallStatus).toInt(),
                           item->data(ModRole::ModPath).toString());
}

void clearInstallTransients(QListWidgetItem *item)
{
    if (!item) return;
    item->setData(ModRole::IntendedModPath, QVariant());
    item->setData(ModRole::PrevModPath,     QVariant());
    item->setData(ModRole::MergeTargetPath, QVariant());
    item->setData(ModRole::InstallToken,    QVariant());
    item->setData(ModRole::PendingFileId,   QVariant());
}

bool restoreInstalled(QListWidgetItem *item)
{
    if (!item) return false;
    // A Merge's target, else the row's own folder: an update keeps it in
    // ModPath, a Replace too (with PrevModPath stashed).
    // An empty folder is not an install, as loadModList reads it.
    auto installedAt = [](const QString &f) {
        return !f.isEmpty() && QFileInfo(f).isDir() && !QDir(f).isEmpty();
    };
    QString folder = item->data(ModRole::MergeTargetPath).toString();
    if (!installedAt(folder))
        folder = item->data(ModRole::ModPath).toString();
    if (!installedAt(folder)) return false;

    item->setData(ModRole::ItemType,         ItemType::Mod);
    item->setData(ModRole::ModPath,          folder);
    item->setData(ModRole::InstallStatus,    1);
    item->setData(ModRole::DownloadProgress, QVariant());
    item->setData(ModRole::PrevModPath,      QVariant());
    item->setData(ModRole::MergeTargetPath,  QVariant());
    item->setData(ModRole::InstallToken,     QVariant());
    item->setData(ModRole::PendingFileId,    QVariant());
    restoreInteractiveFlags(item);
    const QString cn = item->data(ModRole::CustomName).toString();
    item->setText(cn.isEmpty() ? QFileInfo(folder).fileName() : cn);
    item->setToolTip(folder);
    return true;
}

void resetToNotInstalled(QListWidgetItem *item, const QString &fallbackName)
{
    if (!item) return;
    item->setData(ModRole::InstallStatus,    0);
    item->setData(ModRole::DownloadProgress, QVariant());
    // The extracted folder was deleted - clear its path so the modlist doesn't
    // store a reference to a non-existent dir.
    item->setData(ModRole::ModPath,          QVariant());
    // Drop the per-install token: no more InstallController signals will arrive
    // for an aborted install, and status-0 rows never persist it anyway.
    item->setData(ModRole::InstallToken,     QVariant());
    restoreInteractiveFlags(item);
    // Recover the display name into CustomName so it survives a save/reload
    // cycle (loadModList rebuilds the display name from CustomName, not text()).
    QString name = item->data(ModRole::CustomName).toString();
    if (name.isEmpty()) name = fallbackName;
    if (!name.isEmpty()) {
        item->setText(name);
        item->setData(ModRole::CustomName, name);
    }
}

void markInstalled(QListWidgetItem *item, const QString &modPath)
{
    if (!item) return;
    const bool wasUpdate = item->data(ModRole::UpdateAvailable).toBool();
    QString cn = item->data(ModRole::CustomName).toString();
    item->setText(cn.isEmpty() ? QFileInfo(modPath).fileName() : cn);
    restoreInteractiveFlags(item);
    item->setData(ModRole::ItemType,         ItemType::Mod);
    item->setData(ModRole::ModPath,          modPath);
    item->setData(ModRole::InstallStatus,    1);
    item->setData(ModRole::DownloadProgress, QVariant());
    item->setData(ModRole::UpdateAvailable,  false);
    clearInstallTransients(item);
    if (wasUpdate || !item->data(ModRole::DateAdded).toDateTime().isValid())
        item->setData(ModRole::DateAdded, QDateTime::currentDateTime());
    item->setCheckState(Qt::Checked);
    item->setToolTip(modPath);
}

} // namespace placeholder_state
