#include "backup_ops.h"

#include "safe_fs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace backup_ops {

QFileInfoList listSnapshots(const QString &livePath, const QString &mirrorRoot)
{
    const QFileInfo live(livePath);
    const QStringList pattern{live.fileName() + QStringLiteral(".bak.*")};
    QFileInfoList out = live.dir().entryInfoList(pattern, QDir::Files | QDir::Readable);

    const QString mirror = safefs::backupMirrorDir(livePath, mirrorRoot);
    if (!mirror.isEmpty()) {
        QSet<QString> beside;
        for (const QFileInfo &fi : out) beside.insert(fi.fileName());
        for (const QFileInfo &fi :
             QDir(mirror).entryInfoList(pattern, QDir::Files | QDir::Readable))
            if (!beside.contains(fi.fileName())) out << fi;
    }

    // The YYYYMMDD-HHMMSS suffix makes name order time order, so no stat().
    std::sort(out.begin(), out.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.fileName() > b.fileName();
    });
    return out;
}

std::expected<void, QString>
restoreSnapshot(const QString &livePath, const QString &snapshotPath)
{
    if (!QFile::exists(snapshotPath))
        return std::unexpected(QStringLiteral("snapshot is gone"));

    // Snapshot the current live file first so this restore can itself be undone
    // from the rotating Restore Backup dialog later.
    (void)safefs::snapshotBackup(livePath);

    if (QFile::exists(livePath) && !QFile::remove(livePath))
        return std::unexpected(QStringLiteral("could not remove ") + livePath);
    if (!QFile::copy(snapshotPath, livePath))
        return std::unexpected(QStringLiteral("could not write ") + livePath);
    return {};
}

std::expected<QString, QString>
markGoodState(const QString &livePath, const QString &stamp)
{
    const QString goodPath = livePath + ".good." + stamp;
    if (QFile::exists(goodPath) && !QFile::remove(goodPath))
        return std::unexpected(QStringLiteral("could not replace ") + goodPath);
    if (!QFile::copy(livePath, goodPath))
        return std::unexpected(QStringLiteral("could not write ") + goodPath);
    return goodPath;
}

std::expected<void, QString>
deleteSnapshot(const QString &path)
{
    if (!QFile::remove(path))
        return std::unexpected(QStringLiteral("could not delete ") + path);
    return {};
}

} // namespace backup_ops
