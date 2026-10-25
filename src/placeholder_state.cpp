#include "placeholder_state.h"

#include "download_integrity.h"
#include "modlist_model.h"
#include "modroles.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QString>

namespace placeholder_state {

namespace {

// Run an entry transition on a live row: one read, one write-back, one
// change notice for the whole row.
template <typename Fn>
void onRow(ModRow row, Fn fn)
{
    if (!row) return;
    ModEntry e = row.entry();
    fn(e);
    row.model()->update(row.row(), std::move(e));
}

} // namespace

void restoreInteractiveFlags(ModEntry &e)
{
    e.itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable
                | Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable;
}
void restoreInteractiveFlags(ModRow row) { onRow(row, [](ModEntry &e) { restoreInteractiveFlags(e); }); }

void setBusyFlags(ModEntry &e)
{
    e.itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}
void setBusyFlags(ModRow row) { onRow(row, [](ModEntry &e) { setBusyFlags(e); }); }

bool folderInstalled(int installStatus, const QString &modPath)
{
    if (installStatus == 1) return true;
    return installStatus == 2 && !modPath.isEmpty() && QFileInfo(modPath).isDir();
}

bool folderInstalled(const ModEntry &e) { return folderInstalled(e.installStatus, e.modPath); }
bool folderInstalled(const ModRow &row) { return row && folderInstalled(row.entry()); }

void clearInstallTransients(ModEntry &e)
{
    e.intendedModPath.clear();
    e.prevModPath.clear();
    e.mergeTargetPath.clear();
    e.installToken = QUuid();
    e.pendingFileId = 0;
    e.awaitingUpdateDownload = false;
}
void clearInstallTransients(ModRow row) { onRow(row, [](ModEntry &e) { clearInstallTransients(e); }); }

Expectations applicableExpectations(const ModEntry &e)
{
    Expectations x;
    if (!download_integrity::expectationsApply(e.expectedFileId, e.pendingFileId)) return x;
    x.md5  = e.expectedMd5.trimmed().toLower();
    x.size = e.expectedSize;
    return x;
}
Expectations applicableExpectations(const ModRow &row)
{
    return row ? applicableExpectations(row.entry()) : Expectations{};
}

void clearExpectations(ModEntry &e)
{
    e.expectedMd5.clear();
    e.expectedSize = 0;
    e.expectedFileId = 0;
    e.nexusFileName.clear();
}
void clearExpectations(ModRow row) { onRow(row, [](ModEntry &e) { clearExpectations(e); }); }

bool restoreInstalled(ModEntry &e)
{
    // A Merge's target, else the row's own folder: an update keeps it in
    // modPath, a Replace too (with prevModPath stashed).
    // An empty folder is not an install, as loadModList reads it.
    auto installedAt = [](const QString &f) {
        return !f.isEmpty() && QFileInfo(f).isDir() && !QDir(f).isEmpty();
    };
    QString folder = e.mergeTargetPath;
    if (!installedAt(folder)) folder = e.modPath;
    if (!installedAt(folder)) return false;

    e.itemType = ItemType::Mod;
    e.modPath = folder;
    e.installStatus = int(InstallStatus::Installed);
    e.downloadProgress = 0;
    e.prevModPath.clear();
    e.mergeTargetPath.clear();
    e.installToken = QUuid();
    e.pendingFileId = 0;
    restoreInteractiveFlags(e);
    e.displayName = e.customName.isEmpty() ? QFileInfo(folder).fileName() : e.customName;
    e.toolTip = folder;
    return true;
}
bool restoreInstalled(ModRow row)
{
    if (!row) return false;
    ModEntry e = row.entry();
    if (!restoreInstalled(e)) return false;
    row.model()->update(row.row(), std::move(e));
    return true;
}

void resetToNotInstalled(ModEntry &e, const QString &fallbackName)
{
    e.installStatus = int(InstallStatus::NotInstalled);
    e.downloadProgress = 0;
    // The extracted folder was deleted - clear its path so the modlist doesn't
    // store a reference to a non-existent dir.
    e.modPath.clear();
    // Drop the per-install token: no more InstallController signals will arrive
    // for an aborted install, and status-0 rows never persist it anyway.
    e.installToken = QUuid();
    restoreInteractiveFlags(e);
    // Recover the display name into customName so it survives a save/reload
    // cycle (loadModList rebuilds the display name from customName, not text).
    QString name = e.customName;
    if (name.isEmpty()) name = fallbackName;
    if (!name.isEmpty()) {
        e.displayName = name;
        e.customName  = name;
    }
}
void resetToNotInstalled(ModRow row, const QString &fallbackName)
{
    onRow(row, [&](ModEntry &e) { resetToNotInstalled(e, fallbackName); });
}

void markInstalled(ModEntry &e, const QString &modPath)
{
    const bool wasUpdate = e.updateAvailable;
    e.displayName = e.customName.isEmpty() ? QFileInfo(modPath).fileName() : e.customName;
    restoreInteractiveFlags(e);
    e.itemType = ItemType::Mod;
    e.modPath = modPath;
    e.installStatus = int(InstallStatus::Installed);
    e.downloadProgress = 0;
    e.updateAvailable = false;
    e.updateFoundThisSession = false;
    clearInstallTransients(e);
    if (wasUpdate || !e.dateAdded.isValid())
        e.dateAdded = QDateTime::currentDateTime();
    // A new mod goes in enabled; an update leaves the mod as the user had it.
    if (!wasUpdate) e.checked = true;
    e.toolTip = modPath;
}
void markInstalled(ModRow row, const QString &modPath)
{
    onRow(row, [&](ModEntry &e) { markInstalled(e, modPath); });
}

} // namespace placeholder_state
