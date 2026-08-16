#include "mod_package.h"

#include "subprocess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace mod_package {

Format formatForPath(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("7z"),
                                            Qt::CaseInsensitive) == 0
        ? Format::SevenZip : Format::Zip;
}

QString suggestedFileName(const QString &modName, Format f)
{
    // Mod names carry things a filename should not: path separators, quotes,
    // and the colons and question marks a Windows user downloading this would
    // trip over. Spaces are fine and are kept - a mod page filename with
    // spaces is completely ordinary.
    static const QRegularExpression kBad(QStringLiteral(R"([\\/:*?"<>|])"));
    QString base = modName.trimmed();
    base.replace(kBad, QStringLiteral("-"));
    base = base.simplified();
    if (base.isEmpty()) base = QStringLiteral("mod");

    return base + (f == Format::SevenZip ? QStringLiteral(".7z")
                                         : QStringLiteral(".zip"));
}

Result create(const QString &modFolder, const QString &dstPath)
{
    Result r;
    r.archivePath = dstPath;

    const QDir src(modFolder);
    if (modFolder.isEmpty() || !src.exists()) {
        r.error = QStringLiteral("mod folder does not exist");
        return r;
    }
    if (src.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        r.error = QStringLiteral("mod folder is empty");
        return r;
    }
    if (dstPath.isEmpty()) {
        r.error = QStringLiteral("no destination given");
        return r;
    }

    // 7z ADDS to an archive that already exists, so re-packaging over an old
    // file would ship both versions of the mod in one download.
    if (QFile::exists(dstPath) && !QFile::remove(dstPath)) {
        r.error = QStringLiteral("cannot overwrite %1").arg(dstPath);
        return r;
    }
    QDir().mkpath(QFileInfo(dstPath).absolutePath());

    QStringList args{QStringLiteral("a")};
    if (formatForPath(dstPath) == Format::Zip)
        args << QStringLiteral("-tzip");
    args << QStringLiteral("-y")
         << QDir::toNativeSeparators(QFileInfo(dstPath).absoluteFilePath())
         // "." rather than "*": it stores entries relative to the working
         // directory with no leading "./", and unlike a wildcard it also
         // picks up dotfiles.
         << QStringLiteral(".");

    // Driven directly rather than through subprocess::execute because the
    // working directory IS the feature - it is what puts the mod's files at
    // the archive root instead of inside a folder named after it.
    QProcess p;
    subprocess::applyEnv(p);
    p.setWorkingDirectory(src.absolutePath());
    p.start(QStringLiteral("7z"), args);

    if (!p.waitForStarted(10000)) {
        r.error = QStringLiteral("could not run 7z - is p7zip installed?");
        return r;
    }
    // No timeout: a texture mod is gigabytes and the caller is off the UI
    // thread. A hung 7z would be a worse bug than a slow one.
    p.waitForFinished(-1);

    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        const QString tail =
            QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed().right(300);
        r.error = QStringLiteral("7z failed (exit %1)%2")
                      .arg(p.exitCode())
                      .arg(tail.isEmpty() ? QString()
                                          : QStringLiteral(":\n") + tail);
        QFile::remove(dstPath);   // never leave a half-written archive behind
        return r;
    }
    if (!QFile::exists(dstPath)) {
        r.error = QStringLiteral("7z reported success but wrote nothing");
        return r;
    }

    r.bytes = QFileInfo(dstPath).size();
    r.ok    = true;
    return r;
}

} // namespace mod_package
