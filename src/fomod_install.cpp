#include "fomod_install.h"

#include "fomod_copy.h"
#include "fomod_path.h"
#include "fomod_scripts.h"
#include "fs_progress.h"
#include "safe_fs.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace fomod_install {

QStringList execute(const Plan &plan, FsProgress *progress)
{
    const QString &installDir = plan.installDir;
    if (QDir(installDir).exists())
        QDir(installDir).removeRecursively();
    QDir().mkpath(installDir);

    // A destination that climbs out of the install dir is refused, as
    // resolveDest refuses it - here, up front, so it is not counted either.
    auto climbsOut = [](QString dest) {
        dest.replace('\\', '/');
        return dest.split('/', Qt::SkipEmptyParts).contains(QStringLiteral(".."));
    };

    // Sources resolved once, case-insensitively (installers are written on
    // Windows); their sizes are the total the progress counts against.
    QList<QString> sources;
    sources.reserve(plan.copies.size());
    qint64 total = 0;
    for (const PlannedCopy &c : plan.copies) {
        QString src;
        if (!climbsOut(c.destination)) {
            src = fomod::resolvePath(plan.archiveRoot, c.source);
            if (!src.isEmpty() && !QFileInfo::exists(src)) src.clear();
        }
        sources << src;
        if (!src.isEmpty()) total += fomod_copy::sizeOf(src);
    }
    if (progress) progress->begin(FsProgress::Phase::Staging, total);

    QStringList failed;
    for (int i = 0; i < plan.copies.size(); ++i) {
        const PlannedCopy &c = plan.copies[i];
        const QString &src = sources[i];
        if (src.isEmpty()) {
            failed << c.source;
            continue;
        }
        QString dest = c.destination;
        dest.replace('\\', '/');
        if (c.isFolder) {
            const QString dst = dest.isEmpty()
                ? installDir
                : QString(fomod::resolveDest(installDir, dest));
            // resolveDest refuses a "../" destination with an empty path, and
            // an empty path is the working directory to QDir: the refusal
            // used to copy the folder there.
            if (dst.isEmpty()) {
                failed << c.source;
                continue;
            }
            fomod_copy::copyContents(src, dst, progress);
            continue;
        }
        const QString rel = dest.isEmpty() ? QFileInfo(src).fileName() : dest;
        const fomod::ResolvedPath dst = fomod::resolveDest(installDir, rel);
        if (fomod_copy::copyFile(src, dst)) {
            if (progress) progress->add(QFileInfo(src).size());
        } else {
            failed << c.source;
        }

        // Patch-hub rescue: an .omwscripts manifest declares lua bodies the
        // FOMOD often doesn't list as separate <file>/<folder> entries
        // (Completionist Patch Hub, Nexus 58523: ships manifest +
        // scripts/.../*.lua but lists only the manifest). Pull the lua from
        // the manifest's parent dir so the install matches what OpenMW loads.
        if (src.endsWith(QLatin1String(".omwscripts"), Qt::CaseInsensitive))
            fomod_scripts::installDeclaredScripts(src, plan.archiveRoot, installDir);
    }
    for (const QString &f : failed)
        qWarning("fomod_install: could not install '%s'", qUtf8Printable(f));
    return failed;
}

PromoteResult promote(const QString &extractDir,
                      const QString &currentModPath,
                      const QString &fomodPath,
                      const QString &titleHintSanitized,
                      const QString &modsDir,
                      bool leaveExtract)
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

    if (leaveExtract) result.leftover = safefs::setAside(extractDir);
    if (result.leftover.isEmpty()) QDir(extractDir).removeRecursively();
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
