#include "user_state_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

QString resolveUserStatePath(const QString &filename)
{
    // The AppImage runtime sets $APPIMAGE; under it the app dir is read-only.
    const bool inAppImage = !qEnvironmentVariableIsEmpty("APPIMAGE");
    const QString data = QStandardPaths::writableLocation(
                             QStandardPaths::AppDataLocation) + "/" + filename;

    if (inAppImage) {
        QDir().mkpath(QFileInfo(data).absolutePath());
        return QFileInfo(data).absoluteFilePath();
    }

    const QString next = QCoreApplication::applicationDirPath() + "/" + filename;
    const QString up   = QCoreApplication::applicationDirPath() + "/../" + filename;
    QFileInfo binDir(QCoreApplication::applicationDirPath());

    // One-time migration: an older in-tree dev layout (binary under
    // bin/Release_Linux/, state files in bin/) wrote modlist/loadorder/
    // forbidden one directory up from the binary. The earlier "fall back
    // to up if it exists" lookup made that path sticky forever. Promote
    // any leftover up-file into the binary-adjacent location once, then
    // forget about up.
    if (QFile::exists(up) && !QFile::exists(next) && binDir.isWritable())
        QFile::rename(up, next);

    // Existing file wins so an in-place upgrade keeps the user's state.
    for (const QString &p : {next, data})
        if (QFile::exists(p)) return QFileInfo(p).absoluteFilePath();

    // Fresh-install default: save NEXT TO the binary if writable, otherwise
    // fall through to AppDataLocation so a /opt-style install still works.
    if (binDir.isWritable())
        return QFileInfo(next).absoluteFilePath();
    QDir().mkpath(QFileInfo(data).absolutePath());
    return QFileInfo(data).absoluteFilePath();
}
