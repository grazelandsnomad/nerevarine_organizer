#include "safe_fs.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace safefs {

std::expected<QString, QString>
snapshotBackup(const QString &liveFile, int keep)
{
    QFileInfo fi(liveFile);
    if (!fi.exists() || !fi.isFile())
        return std::unexpected(QStringLiteral("no source file"));
    if (keep < 0) keep = 0;

    const QString stamp  = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString backup = liveFile + ".bak." + stamp;
    const bool copied    = QFile::copy(liveFile, backup);

    QDir dir(fi.absolutePath());
    QStringList olds = dir.entryList({fi.fileName() + ".bak.*"},
                                      QDir::Files, QDir::Name);
    while (olds.size() > keep)
        QFile::remove(dir.absoluteFilePath(olds.takeFirst()));

    if (!copied) return std::unexpected(QStringLiteral("copy failed"));
    return backup;
}

std::expected<void, QString>
copyTreeVerified(const QString &src, const QString &dst,
                 std::function<bool()> isCancelled)
{
    auto fail = [&](QString reason) -> std::expected<void, QString> {
        QDir(dst).removeRecursively();
        return std::unexpected(std::move(reason));
    };

    if (!QDir().mkpath(dst))
        return std::unexpected(QStringLiteral("could not create destination"));

    auto cancelled = [&]() {
        return isCancelled && isCancelled();
    };

    const int srcPrefixLen = src.length() + 1;
    QDirIterator it(src, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (cancelled())
            return fail(QStringLiteral("cancelled"));
        it.next();
        const QFileInfo fi = it.fileInfo();
        const QString rel  = fi.absoluteFilePath().mid(srcPrefixLen);
        const QString target = QDir(dst).filePath(rel);

        if (fi.isDir()) {
            if (!QDir().mkpath(target))
                return fail(QStringLiteral("mkpath failed: ") + rel);
            continue;
        }

        // Defensive: QDirIterator can visit a file before its containing
        // directory on some filesystems.
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(fi.absoluteFilePath(), target))
            return fail(QStringLiteral("copy failed: ") + rel);
        if (QFileInfo(target).size() != fi.size())
            return fail(QStringLiteral("size mismatch after copy: ") + rel);
        QCoreApplication::processEvents();
    }
    return {};
}

} // namespace safefs
