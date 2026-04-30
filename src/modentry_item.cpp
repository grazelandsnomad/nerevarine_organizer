// Bidirectional conversion between a ModEntry value and the role data carried
// on a QListWidgetItem. Lives in its own TU so modentry.cpp can stay free of
// <QListWidgetItem> and the unit test can link against QtCore only.

#include "modentry.h"
#include "modroles.h"

#include <QListWidgetItem>
#include <QVariant>

ModEntry ModEntry::fromItem(const QListWidgetItem *item)
{
    ModEntry e;
    if (!item) return e;

    const QString t = item->data(ModRole::ItemType).toString();
    if (!t.isEmpty()) e.itemType = t;
    e.displayName = item->text();
    e.checked     = (item->checkState() == Qt::Checked);
    e.bgColor     = item->data(ModRole::BgColor).value<QColor>();
    e.fgColor     = item->data(ModRole::FgColor).value<QColor>();

    e.collapsed   = item->data(ModRole::Collapsed).toBool();
    e.activeCount = item->data(ModRole::ActiveCount).toInt();
    e.totalCount  = item->data(ModRole::TotalCount).toInt();

    e.nexusId    = item->data(ModRole::NexusId).toInt();
    e.nexusUrl   = item->data(ModRole::NexusUrl).toString();
    e.nexusTitle = item->data(ModRole::NexusTitle).toString();

    e.modPath         = item->data(ModRole::ModPath).toString();
    e.intendedModPath = item->data(ModRole::IntendedModPath).toString();
    e.modSize         = item->data(ModRole::ModSize).toLongLong();

    e.customName   = item->data(ModRole::CustomName).toString();
    e.annotation   = item->data(ModRole::Annotation).toString();
    e.dateAdded    = item->data(ModRole::DateAdded).toDateTime();
    e.isUtility    = item->data(ModRole::IsUtility).toBool();
    e.isFavorite   = item->data(ModRole::IsFavorite).toBool();
    e.fomodChoices = item->data(ModRole::FomodChoices).toString();

    e.installStatus    = item->data(ModRole::InstallStatus).toInt();
    e.downloadProgress = item->data(ModRole::DownloadProgress).toInt();
    e.updateAvailable  = item->data(ModRole::UpdateAvailable).toBool();
    e.expectedMd5      = item->data(ModRole::ExpectedMd5).toString();
    e.expectedSize     = item->data(ModRole::ExpectedSize).toLongLong();

    e.dependsOn            = item->data(ModRole::DependsOn).toStringList();
    e.highlightRole        = item->data(ModRole::HighlightRole).toInt();
    e.hasInListDependency  = item->data(ModRole::HasInListDependency).toBool();
    e.hasMissingDependency = item->data(ModRole::HasMissingDependency).toBool();
    e.missingDependencies  = item->data(ModRole::MissingDependencies).toStringList();

    e.hasConflict      = item->data(ModRole::HasConflict).toBool();
    e.conflictsWith    = item->data(ModRole::ConflictsWith).toStringList();
    e.hasMissingMaster = item->data(ModRole::HasMissingMaster).toBool();
    e.missingMasters   = item->data(ModRole::MissingMasters).toStringList();

    return e;
}

void ModEntry::applyToItem(QListWidgetItem *item) const
{
    if (!item) return;

    item->setData(ModRole::ItemType, itemType);
    item->setText(displayName);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    item->setData(ModRole::BgColor, bgColor);
    item->setData(ModRole::FgColor, fgColor);

    item->setData(ModRole::Collapsed,   collapsed);
    item->setData(ModRole::ActiveCount, activeCount);
    item->setData(ModRole::TotalCount,  totalCount);

    item->setData(ModRole::NexusId,    nexusId);
    item->setData(ModRole::NexusUrl,   nexusUrl);
    item->setData(ModRole::NexusTitle, nexusTitle);

    item->setData(ModRole::ModPath,         modPath);
    item->setData(ModRole::IntendedModPath, intendedModPath);
    item->setData(ModRole::ModSize,         QVariant::fromValue(modSize));

    item->setData(ModRole::CustomName,   customName);
    item->setData(ModRole::Annotation,   annotation);
    item->setData(ModRole::DateAdded,    dateAdded);
    item->setData(ModRole::IsUtility,    isUtility);
    item->setData(ModRole::IsFavorite,   isFavorite);
    item->setData(ModRole::FomodChoices, fomodChoices);

    item->setData(ModRole::InstallStatus,    installStatus);
    item->setData(ModRole::DownloadProgress, downloadProgress);
    item->setData(ModRole::UpdateAvailable,  updateAvailable);
    item->setData(ModRole::ExpectedMd5,      expectedMd5);
    item->setData(ModRole::ExpectedSize,     QVariant::fromValue(expectedSize));

    item->setData(ModRole::DependsOn,            dependsOn);
    item->setData(ModRole::HighlightRole,        highlightRole);
    item->setData(ModRole::HasInListDependency,  hasInListDependency);
    item->setData(ModRole::HasMissingDependency, hasMissingDependency);
    item->setData(ModRole::MissingDependencies,  missingDependencies);

    item->setData(ModRole::HasConflict,      hasConflict);
    item->setData(ModRole::ConflictsWith,    conflictsWith);
    item->setData(ModRole::HasMissingMaster, hasMissingMaster);
    item->setData(ModRole::MissingMasters,   missingMasters);
}
