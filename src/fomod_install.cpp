#include "fomod_install.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace fomod_install {

PromoteResult promote(const QString &extractDir,
                      const QString &currentModPath,
                      const QString &fomodPath,
                      const QString &titleHintSanitized,
                      const QString &modsDir)
{
    QDir fomodDir(fomodPath);

    // Wizard never produced a fomod_install/. Keep the raw extract so the
    // plugins remain reachable.
    if (!fomodDir.exists()) {
        return {PromoteOutcome::EmptyFallback, currentModPath, /*extractDirRemoved=*/false};
    }

    // Wizard produced an empty fomod_install/ (user picked nothing, or a
    // broken ModuleConfig yielded no files). Fall back to the raw extract;
    // promoting an empty dir would lose the plugins.
    if (fomodDir.isEmpty()) {
        fomodDir.removeRecursively();
        return {PromoteOutcome::EmptyFallback, currentModPath, /*extractDirRemoved=*/false};
    }

    // applySelections() has already filtered fomod_install/ to the picked
    // plugins. The rest of extractDir is the raw archive (every unticked
    // radio variant included), and collectDataFolders walks siblings, so
    // leaving it in place leaks unselected variants as data= paths. Move
    // fomod_install/ out of extractDir, nuke the wrapper, rename to final.

    QString targetName = titleHintSanitized;
    if (targetName.isEmpty())
        targetName = QFileInfo(extractDir).fileName();

    if (targetName.isEmpty()) {
        return {PromoteOutcome::Promoted, fomodPath, /*extractDirRemoved=*/false};
    }

    // extractDir itself is a permitted collision since it's about to be
    // deleted; aiming at that slot recovers the familiar archive name.
    const QString extractDirAbs = QFileInfo(extractDir).absoluteFilePath();
    QString target = QDir(modsDir).filePath(targetName);
    int suffix = 1;
    while (QFileInfo::exists(target)
           && QFileInfo(target).absoluteFilePath() != extractDirAbs) {
        target = QDir(modsDir).filePath(
            targetName + "_" + QString::number(++suffix));
    }

    PromoteResult result{PromoteOutcome::Promoted, fomodPath, /*extractDirRemoved=*/false};

    // Two-step rename for crash-safety: move fomod_install/ to a staging
    // sibling first so deleting extractDir can't touch it, then rename
    // staging to the final target.
    const QString staging = QDir(modsDir).filePath(
        QStringLiteral("_fomod_staging_%1_%2")
            .arg(QDateTime::currentMSecsSinceEpoch())
            .arg(targetName));

    if (!QDir().rename(fomodPath, staging)) {
        // Couldn't move fomod_install out. Leave disk state alone.
        return result;
    }

    QDir(extractDir).removeRecursively();
    result.extractDirRemoved = true;

    if (QDir().rename(staging, target)) {
        result.finalModPath = target;
    } else {
        // Final rename raced. Keep staging as the install path; data is safe.
        result.finalModPath = staging;
    }
    return result;
}

} // namespace fomod_install
