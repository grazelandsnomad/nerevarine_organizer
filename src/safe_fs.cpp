#include "safe_fs.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace safefs {

namespace {

void pruneSnapshots(const QDir &dir, const QString &liveName, int keep)
{
    QStringList olds = dir.entryList({liveName + ".bak.*"}, QDir::Files, QDir::Name);
    while (olds.size() > keep)
        QFile::remove(dir.absoluteFilePath(olds.takeFirst()));
}

bool sameContents(const QString &a, const QString &b)
{
    QFile fa(a), fb(b);
    if (fa.size() != fb.size()) return false;
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly)) return false;
    return fa.readAll() == fb.readAll();
}

} // namespace

std::expected<QString, QString>
snapshotBackup(const QString &liveFile, int keep, const QString &mirrorRoot)
{
    QFileInfo fi(liveFile);
    if (!fi.exists() || !fi.isFile())
        return std::unexpected(QStringLiteral("no source file"));
    if (keep < 0) keep = 0;

    const QString stamp  = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    const QString name   = fi.fileName() + ".bak." + stamp;
    const QString backup = liveFile + ".bak." + stamp;
    const bool copied    = QFile::copy(liveFile, backup);
    pruneSnapshots(QDir(fi.absolutePath()), fi.fileName(), keep);

    const QString mirror = backupMirrorDir(liveFile, mirrorRoot);
    if (!mirror.isEmpty() && QDir().mkpath(mirror)) {
        // Only a state the mirror does not already end on. The mirror is what
        // a deep clean leaves, and the session after one starts saving a new,
        // empty list: if every save took a slot, the ~20 saves one session
        // can hold would rotate the lost list out of the one place it
        // survived. This way each slot costs a real change.
        const QDir md(mirror);
        const QStringList mirrored =
            md.entryList({fi.fileName() + ".bak.*"}, QDir::Files, QDir::Name);
        if (mirrored.isEmpty() || !sameContents(liveFile, md.filePath(mirrored.last())))
            (void)QFile::copy(liveFile, md.filePath(name));
        pruneSnapshots(md, fi.fileName(), keep);
    }

    if (!copied) return std::unexpected(QStringLiteral("copy failed"));
    return backup;
}

QString gitWorkTreeOf(const QString &path)
{
    QString dir = QDir::cleanPath(QFileInfo(path).absolutePath());
    while (!dir.isEmpty()) {
        if (QFileInfo::exists(dir + QStringLiteral("/.git")))
            return dir;
        const QString up = QFileInfo(dir).path();
        if (up == dir) break;   // reached the root
        dir = up;
    }
    return {};
}

QString backupMirrorDir(const QString &liveFile, const QString &mirrorRoot)
{
    if (mirrorRoot.isEmpty()) return {};
    const QString tree = gitWorkTreeOf(liveFile);
    if (tree.isEmpty()) return {};
    const QString liveDir = QDir::cleanPath(QFileInfo(liveFile).absolutePath());
    const QByteArray hash = QCryptographicHash::hash(
        liveDir.toUtf8(), QCryptographicHash::Sha1).toHex().left(8);
    return QDir(mirrorRoot).filePath(QFileInfo(tree).fileName() + QLatin1Char('-')
                                     + QString::fromLatin1(hash));
}

QString defaultBackupMirrorRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
         + QStringLiteral("/backups");
}

bool forceRemoveRecursively(const QString &path)
{
    QFileInfo rootFi(path);
    if (!rootFi.exists())
        return true;

    auto ensureWritable = [](const QString &p) -> bool {
        const QFile::Permissions perms = QFile::permissions(p);
        if (perms == 0)
            return true;
        if (perms & QFile::WriteUser)
            return true;
        return QFile::setPermissions(p, perms | QFile::WriteUser);
    };

    if (!ensureWritable(path))
        return false;

    QDirIterator it(path,
                    QDir::AllEntries | QDir::NoDotAndDotDot
                        | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (!ensureWritable(it.filePath()))
            return false;
    }

    return QDir(path).removeRecursively();
}

bool isCompleteDownload(const QString &path, qint64 expectedBytes)
{
    if (expectedBytes <= 0) return false;
    const QFileInfo fi(path);
    return fi.exists() && fi.isFile() && fi.size() == expectedBytes;
}

QString writableFilePath(const QString &dir, const QString &filename)
{
    const QDir d(dir);
    QString candidate = d.filePath(filename);

    QFileInfo fi(candidate);
    if (!fi.exists() || fi.isFile())
        return candidate;

    // Something non-regular (a directory left by a prior extract) owns the
    // name. Suffix until free. Keep the extension only when there is one - an
    // extensionless CDN id would otherwise become "name_2." with a dangling dot.
    const QFileInfo src(filename);
    const QString base = src.completeBaseName();
    const QString ext  = src.suffix();
    for (int n = 2; n < 10000; ++n) {
        const QString rebuilt = ext.isEmpty()
            ? QStringLiteral("%1_%2").arg(base).arg(n)
            : QStringLiteral("%1_%2.%3").arg(base).arg(n).arg(ext);
        candidate = d.filePath(rebuilt);
        fi = QFileInfo(candidate);
        if (!fi.exists() || fi.isFile())
            return candidate;
    }
    return candidate;   // pathological; caller reports the open failure
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

        // QDirIterator can visit a file before its parent dir on some FSes.
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(fi.absoluteFilePath(), target))
            return fail(QStringLiteral("copy failed: ") + rel);
        if (QFileInfo(target).size() != fi.size())
            return fail(QStringLiteral("size mismatch after copy: ") + rel);
        // Deliberately NO processEvents() here: this is a Qt-Core helper with no
        // event loop of its own, and pumping the *caller's* loop mid-copy would
        // re-enter arbitrary UI slots (the debounced save / conflict-scan
        // timers) over a half-copied tree during a data-destructive move.
        // Cancellation is delivered out-of-band via isCancelled(), checked at
        // the top of the loop; the caller keeps the UI responsive itself.
    }
    return {};
}

} // namespace safefs
