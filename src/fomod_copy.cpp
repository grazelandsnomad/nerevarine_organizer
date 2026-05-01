#include "fomod_copy.h"

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
        if (fi.isDir())
            copyDir(fi.absoluteFilePath(), dstDir + "/" + fi.fileName());
        else
            QFile::copy(fi.absoluteFilePath(), dstDir + "/" + fi.fileName());
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
        if (fi.isDir())
            copyDir(fi.absoluteFilePath(), dstDir + "/" + fi.fileName());
        else
            QFile::copy(fi.absoluteFilePath(), dstDir + "/" + fi.fileName());
    }
}

} // namespace fomod_copy
