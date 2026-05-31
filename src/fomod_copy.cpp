#include "fomod_copy.h"
#include "fomod_path.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace fomod_copy {

void copyContents(const QString &srcDir, const QString &dstDir)
{
    QDir src(srcDir);
    if (!src.exists()) return;
    const auto entries = src.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty()) return;
    QDir().mkpath(dstDir);
    for (const QFileInfo &fi : entries) {
        // Reconcile each child against what's already staged so a file or
        // folder differing only in letter case ("meshes" vs an existing
        // "Meshes") merges in place instead of forking a duplicate directory
        // on a case-sensitive filesystem (the Project Atlas report).
        const QString dst = fomod::resolveDest(dstDir, fi.fileName());
        if (fi.isDir()) {
            copyDir(fi.absoluteFilePath(), dst);
        } else {
            // Last writer wins: a later FOMOD option (e.g. a patch) must be
            // able to overwrite an earlier file; QFile::copy won't clobber.
            QFile::remove(dst);
            QFile::copy(fi.absoluteFilePath(), dst);
        }
    }
}

void copyDir(const QString &srcDir, const QString &dstDir)
{
    QDir src(srcDir);
    if (!src.exists()) return;
    const auto entries = src.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty()) return;
    QDir().mkpath(dstDir);
    for (const QFileInfo &fi : entries) {
        // Reconcile each child against what's already staged so a file or
        // folder differing only in letter case ("meshes" vs an existing
        // "Meshes") merges in place instead of forking a duplicate directory
        // on a case-sensitive filesystem (the Project Atlas report).
        const QString dst = fomod::resolveDest(dstDir, fi.fileName());
        if (fi.isDir()) {
            copyDir(fi.absoluteFilePath(), dst);
        } else {
            // Last writer wins: a later FOMOD option (e.g. a patch) must be
            // able to overwrite an earlier file; QFile::copy won't clobber.
            QFile::remove(dst);
            QFile::copy(fi.absoluteFilePath(), dst);
        }
    }
}

} // namespace fomod_copy
