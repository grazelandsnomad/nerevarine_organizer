#include "fomod_copy.h"
#include "fomod_path.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtGlobal>

namespace fomod_copy {

bool copyFile(const QString &src, const fomod::ResolvedPath &dst)
{
    const QString &d = dst.str();
    QDir().mkpath(QFileInfo(d).absolutePath());
    QFile::remove(d);   // last writer wins: QFile::copy won't clobber an existing file
    if (!QFile::copy(src, d)) {
        qWarning("fomod_copy: failed to copy '%s' -> '%s'",
                 qUtf8Printable(src), qUtf8Printable(d));
        return false;
    }
    return true;
}

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
        const fomod::ResolvedPath dst = fomod::resolveDest(dstDir, fi.fileName());
        if (fi.isDir())
            copyDir(fi.absoluteFilePath(), dst);   // dst decays to const QString&
        else
            copyFile(fi.absoluteFilePath(), dst);  // last-writer-wins, warns on failure
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
        const fomod::ResolvedPath dst = fomod::resolveDest(dstDir, fi.fileName());
        if (fi.isDir())
            copyDir(fi.absoluteFilePath(), dst);   // dst decays to const QString&
        else
            copyFile(fi.absoluteFilePath(), dst);  // last-writer-wins, warns on failure
    }
}

} // namespace fomod_copy
