#include "fomod_copy.h"
#include "fomod_path.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtGlobal>

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
            // able to overwrite an earlier file; QFile::copy won't clobber, so
            // drop any existing target first.  If the copy then fails (disk
            // full, permissions), warn - silently leaving a gone-but-not-
            // replaced file is exactly the "empty install" class of bug this
            // module exists to prevent.
            QFile::remove(dst);
            if (!QFile::copy(fi.absoluteFilePath(), dst))
                qWarning("fomod_copy: failed to copy '%s' -> '%s'",
                         qUtf8Printable(fi.absoluteFilePath()),
                         qUtf8Printable(dst));
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
            // Last writer wins (see copyContents); warn on failure rather than
            // silently dropping the file.
            QFile::remove(dst);
            if (!QFile::copy(fi.absoluteFilePath(), dst))
                qWarning("fomod_copy: failed to copy '%s' -> '%s'",
                         qUtf8Printable(fi.absoluteFilePath()),
                         qUtf8Printable(dst));
        }
    }
}

} // namespace fomod_copy
