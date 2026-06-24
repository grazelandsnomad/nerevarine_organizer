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

    // No fomod_install/ produced. Keep the raw extract so plugins stay reachable.
    if (!fomodDir.exists()) {
        return {PromoteOutcome::EmptyFallback, currentModPath, /*extractDirRemoved=*/false};
    }

    // Empty fomod_install/ (nothing picked, or broken ModuleConfig). Fall back
    // to the raw extract; promoting an empty dir would lose the plugins.
    if (fomodDir.isEmpty()) {
        fomodDir.removeRecursively();
        return {PromoteOutcome::EmptyFallback, currentModPath, /*extractDirRemoved=*/false};
    }

    // fomod_install/ holds only the picked plugins. The rest of extractDir is
    // the raw archive (unticked variants too), and collectDataFolders walks
    // siblings, so leaving it leaks unselected variants as data= paths. Move
    // fomod_install/ out, nuke the wrapper, rename to final.

    QString targetName = titleHintSanitized;
    if (targetName.isEmpty())
        targetName = QFileInfo(extractDir).fileName();

    if (targetName.isEmpty()) {
        return {PromoteOutcome::Promoted, fomodPath, /*extractDirRemoved=*/false};
    }

    // Colliding with extractDir is fine (it's about to be deleted); reusing
    // that slot recovers the familiar archive name.
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
    // sibling first so deleting extractDir can't touch it, then rename to final.
    const QString staging = QDir(modsDir).filePath(
        QStringLiteral("_fomod_staging_%1_%2")
            .arg(QDateTime::currentMSecsSinceEpoch())
            .arg(targetName));

    if (!QDir().rename(fomodPath, staging)) {
        // Couldn't move fomod_install out; leave disk alone.
        return result;
    }

    QDir(extractDir).removeRecursively();
    result.extractDirRemoved = true;

    if (QDir().rename(staging, target)) {
        result.finalModPath = target;
    } else {
        // Final rename raced; keep staging as the install path, data is safe.
        result.finalModPath = staging;
    }
    return result;
}

} // namespace fomod_install
