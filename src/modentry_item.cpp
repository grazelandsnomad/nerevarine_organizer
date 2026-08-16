// ModEntry <-> QListWidgetItem role data. Separate TU so modentry.cpp
// stays free of <QListWidgetItem> and its test links QtCore only.

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
    e.isGeneratedTranslation =
        item->data(ModRole::IsGeneratedTranslation).toBool();
    e.fomodChoices = item->data(ModRole::FomodChoices).toString();
    e.bainChoices  = item->data(ModRole::BainChoices).toString();

    e.installStatus    = item->data(ModRole::InstallStatus).toInt();
    e.downloadProgress = item->data(ModRole::DownloadProgress).toInt();
    e.updateAvailable  = item->data(ModRole::UpdateAvailable).toBool();
    e.expectedMd5      = item->data(ModRole::ExpectedMd5).toString();
    e.expectedSize     = item->data(ModRole::ExpectedSize).toLongLong();
    e.installToken     = item->data(ModRole::InstallToken).toUuid();

    e.dependsOn            = item->data(ModRole::DependsOn).toStringList();
    e.highlightRole        = item->data(ModRole::HighlightRole).toInt();
    e.hasInListDependency  = item->data(ModRole::HasInListDependency).toBool();
    e.hasMissingDependency = item->data(ModRole::HasMissingDependency).toBool();
    e.missingDependencies  = item->data(ModRole::MissingDependencies).toStringList();

    e.hasConflict           = item->data(ModRole::HasConflict).toBool();
    e.conflictOverwrites    = item->data(ModRole::ConflictOverwrites).toStringList();
    e.conflictOverwrittenBy = item->data(ModRole::ConflictOverwrittenBy).toStringList();
    e.conflictSameRecords   = item->data(ModRole::ConflictSameRecords).toStringList();
    e.conflictHighlight     = item->data(ModRole::ConflictHighlight).toInt();
    e.hasMissingMaster = item->data(ModRole::HasMissingMaster).toBool();
    e.missingMasters   = item->data(ModRole::MissingMasters).toStringList();

    e.videoUrl        = item->data(ModRole::VideoUrl).toString();
    e.sourceUrl       = item->data(ModRole::SourceUrl).toString();
    e.prevModPath     = item->data(ModRole::PrevModPath).toString();
    e.mergeTargetPath = item->data(ModRole::MergeTargetPath).toString();

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
    item->setData(ModRole::IsGeneratedTranslation, isGeneratedTranslation);
    item->setData(ModRole::FomodChoices, fomodChoices);
    item->setData(ModRole::BainChoices,  bainChoices);

    item->setData(ModRole::InstallStatus,    installStatus);
    item->setData(ModRole::DownloadProgress, downloadProgress);
    item->setData(ModRole::UpdateAvailable,  updateAvailable);
    item->setData(ModRole::ExpectedMd5,      expectedMd5);
    item->setData(ModRole::ExpectedSize,     QVariant::fromValue(expectedSize));
    item->setData(ModRole::InstallToken,
                  installToken.isNull() ? QVariant() : QVariant(installToken));

    item->setData(ModRole::DependsOn,            dependsOn);
    item->setData(ModRole::HighlightRole,        highlightRole);
    item->setData(ModRole::HasInListDependency,  hasInListDependency);
    item->setData(ModRole::HasMissingDependency, hasMissingDependency);
    item->setData(ModRole::MissingDependencies,  missingDependencies);

    item->setData(ModRole::HasConflict,           hasConflict);
    item->setData(ModRole::ConflictOverwrites,    conflictOverwrites);
    item->setData(ModRole::ConflictOverwrittenBy, conflictOverwrittenBy);
    item->setData(ModRole::ConflictSameRecords,   conflictSameRecords);
    item->setData(ModRole::ConflictHighlight,     conflictHighlight);
    item->setData(ModRole::HasMissingMaster, hasMissingMaster);
    item->setData(ModRole::MissingMasters,   missingMasters);

    item->setData(ModRole::VideoUrl,        videoUrl);
    item->setData(ModRole::SourceUrl,       sourceUrl);
    item->setData(ModRole::PrevModPath,     prevModPath);
    item->setData(ModRole::MergeTargetPath, mergeTargetPath);
}
