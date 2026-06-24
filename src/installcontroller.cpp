#include "installcontroller.h"

#include "install_layout.h"
#include "archive_magic.h"
#include "logging.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QtConcurrent>

InstallController::InstallController(QObject *parent)
    : QObject(parent) {}

void InstallController::verifyArchive(const QString &archivePath,
                                      const QUuid &installToken,
                                      const QString &expectedMd5Lower,
                                      qint64 expectedSize)
{
    // Nothing to check against.
    if (expectedMd5Lower.isEmpty() && expectedSize <= 0) {
        emit verified(archivePath, installToken);
        return;
    }

    // Free size check catches truncated downloads before we burn MD5 cycles on
    // a multi-hundred-MB file.
    if (expectedSize > 0) {
        const qint64 actualSize = QFileInfo(archivePath).size();
        if (actualSize != expectedSize) {
            emit verificationFailed(archivePath, installToken,
                                    VerifyFailKind::Size,
                                    QString::number(actualSize),
                                    QString::number(expectedSize));
            return;
        }
    }

    // Size matched, no md5 to check.
    if (expectedMd5Lower.isEmpty()) {
        emit verified(archivePath, installToken);
        return;
    }

    // MD5 is expensive: hash on a worker thread. QPointer guards against the
    // controller being destroyed mid-hash (shutdown) and dangling the emit.
    emit verificationStarted(archivePath);
    QPointer<InstallController> safeSelf(this);
    (void)QtConcurrent::run(
        [safeSelf, archivePath, installToken, expectedMd5Lower]() {
        QString actualMd5;
        {
            QFile f(archivePath);
            if (f.open(QIODevice::ReadOnly)) {
                QCryptographicHash h(QCryptographicHash::Md5);
                // Stream in ~1 MB chunks so we don't pull the whole archive
                // into RAM.
                constexpr qint64 kChunk = 1 << 20;
                QByteArray buf;
                while (!f.atEnd()) {
                    buf = f.read(kChunk);
                    if (buf.isEmpty()) break;
                    h.addData(buf);
                }
                actualMd5 = QString::fromLatin1(h.result().toHex());
            }
        }

        if (!safeSelf) return;
        QMetaObject::invokeMethod(safeSelf.data(),
            [safeSelf, archivePath, installToken, actualMd5, expectedMd5Lower]() {
            if (!safeSelf) return;
            if (actualMd5.compare(expectedMd5Lower, Qt::CaseInsensitive) != 0) {
                emit safeSelf->verificationFailed(
                    archivePath, installToken,
                    VerifyFailKind::Md5,
                    actualMd5.isEmpty()
                        ? QStringLiteral("(read error)")
                        : actualMd5,
                    expectedMd5Lower);
                return;
            }
            emit safeSelf->verified(archivePath, installToken);
        }, Qt::QueuedConnection);
    });
}

namespace {
// Folder matches this archive if it's exactly the basename or
// "<baseName>_<digits>" (the legacy timestamp-fallback shape below). The
// digits-only test avoids coincidental prefixes: "foo-1-0_extra" is NOT a
// wrapper for "foo-1-0", only "foo-1-0" or "foo-1-0_1776436249" are.
bool wrapperMatchesBaseName(const QString &folderName, const QString &baseName)
{
    if (folderName == baseName) return true;
    if (!folderName.startsWith(baseName + "_")) return false;
    const QString suffix = folderName.mid(baseName.size() + 1);
    if (suffix.isEmpty()) return false;
    for (QChar c : suffix)
        if (!c.isDigit()) return false;
    return true;
}

// Absolute path of the in-place wrapper dir to reuse, or empty when the hint
// doesn't match this archive.
QString resolveReuseWrapper(const QString &hintPath,
                             const QString &modsDir,
                             const QString &baseName)
{
    if (hintPath.isEmpty()) return {};
    const QString absHint = QFileInfo(hintPath).absoluteFilePath();
    const QString absRoot = QFileInfo(modsDir).absoluteFilePath();
    if (absRoot.isEmpty() || !absHint.startsWith(absRoot + "/")) return {};
    const QDir rootDir(absRoot);
    const QString rel = rootDir.relativeFilePath(absHint);
    const QString top = rel.section('/', 0, 0);
    if (top.isEmpty()) return {};
    if (!wrapperMatchesBaseName(top, baseName)) return {};
    const QString wrapper = rootDir.filePath(top);
    // Reuse only a real directory. A stale hint could name a plain file (e.g.
    // an extensionless archive whose name == baseName); reusing it would
    // removeRecursively() + extract onto the archive file itself - the same
    // collision the extractDir guard prevents.
    if (!QFileInfo(wrapper).isDir()) return {};
    return wrapper;
}

// Log an extractor failure with the child's captured output. A bare "7z exit
// 2" is ambiguous (cannot create output dir, errno=N, real archive error,
// out-of-disk...); the captured stderr makes the next failure diagnosable.
void logExtractFailure(const char *prog, const QString &archivePath,
                       const QString &extractDir, int code, QProcess *p)
{
    qCWarning(logging::lcInstall).noquote()
        << "[extract]" << prog << "exit=" << code
        << "archiveExists=" << QFileInfo::exists(archivePath)
        << "archive=" << archivePath << "outDir=" << extractDir
        << "| stderr:" << QString::fromLocal8Bit(p->readAllStandardError()).trimmed()
        << "| stdout:" << QString::fromLocal8Bit(p->readAllStandardOutput()).trimmed();
}
} // namespace

void InstallController::extractArchive(const QString &archivePath,
                                       const QString &modsDir,
                                       const QUuid &installToken,
                                       const QString &reuseHintPath,
                                       const QString &looseNameHint)
{
    const QFileInfo fi(archivePath);
    const QString baseName = fi.completeBaseName();
    QString extractDir = QDir(modsDir).filePath(baseName);

    // Stable-path reuse: if the caller has a prior wrapper for this
    // placeholder, extract back into it. Keeps cross-machine syncs on the same
    // folder and stops reinstalls piling up "<mod>_<ts1>", "<mod>_<ts2>".
    // Falls back to the timestamp suffix with no usable hint.
    const QString reuseWrapper = resolveReuseWrapper(reuseHintPath, modsDir, baseName);
    if (!reuseWrapper.isEmpty()) {
        QDir(reuseWrapper).removeRecursively();
        extractDir = reuseWrapper;
    } else if (QFileInfo::exists(extractDir)) {
        // Suffix when anything already sits at extractDir - wrapper dir OR
        // file. The file case bites: an extensionless CDN download (bare id)
        // has completeBaseName == the whole filename, so extractDir == the
        // archive file. The old QDir().exists() saw only dirs, skipped the
        // suffix, mkpath couldn't make a dir over the file, and 7z died with
        // "Cannot create output directory: errno=17" (exit 2), misread as a
        // corrupt archive.
        extractDir += "_" + QString::number(QDateTime::currentSecsSinceEpoch());
    }
    QDir().mkpath(extractDir);

    // Dive into a single-subdir archive so FOMOD detection and addModFromPath
    // see the real mod root. diveTarget owns the suppression rules.
    auto emitSuccess = [this, archivePath, extractDir, installToken]() {
        const QDir dir(extractDir);
        const QStringList subdirs = dir.entryList(QDir::Dirs  | QDir::NoDotAndDotDot);
        const QStringList files   = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        const QString diveInto = install_layout::diveTarget(subdirs, files);
        const QString modPath = diveInto.isEmpty()
            ? extractDir
            : dir.filePath(diveInto);
        emit extractionSucceeded(archivePath, extractDir, modPath, installToken);
    };

    // A non-archive download is a bare loose file (an .omwaddon/.esp uploaded
    // unzipped, or an extensionless CDN id). unzip/7z would fatal on it, so
    // place it as a loose mod. An empty/unreadable file is a real failure.
    QByteArray header;
    qint64 fileSize = 0;
    {
        QFile hf(archivePath);
        if (hf.open(QIODevice::ReadOnly)) { fileSize = hf.size(); header = hf.read(16); }
    }
    if (fileSize <= 0) {
        emit extractionFailed(archivePath, extractDir, installToken,
                              ExtractFailKind::NonzeroExit, QStringLiteral("2"));
        return;
    }
    if (!archive_magic::looksLikeArchive(header)) {
        const QString destName =
            archive_magic::looseFileName(fi.fileName(), header, looseNameHint);
        if (QFile::copy(archivePath, QDir(extractDir).filePath(destName)))
            emitSuccess();
        else
            emit extractionFailed(archivePath, extractDir, installToken,
                                  ExtractFailKind::NonzeroExit, QStringLiteral("2"));
        return;
    }

    // For .rar, try unrar first (RAR5/solid) then fall back to 7z's RAR codec.
    // unrar-free fails on RAR5, and many distros ship it as the system unrar.
    const QString ext = fi.suffix().toLower();

    // Launch a process; false if it couldn't start.
    auto launch = [this](QProcess *p, const QString &prog,
                         const QStringList &a) -> bool {
        p->start(prog, a);
        return p->waitForStarted(3000);
    };

    if (ext == QStringLiteral("zip")) {
        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, this,
                [this, proc, archivePath, extractDir, installToken, emitSuccess]
                (int code, QProcess::ExitStatus) {
            if (code != 0) {
                logExtractFailure("unzip", archivePath, extractDir, code, proc);
                proc->deleteLater();
                emit extractionFailed(archivePath, extractDir, installToken,
                                      ExtractFailKind::NonzeroExit,
                                      QString::number(code));
                return;
            }
            proc->deleteLater();
            emitSuccess();
        });
        if (!launch(proc, "unzip",
                    {"-o", archivePath, "-d", extractDir})) {
            proc->deleteLater();
            emit extractionFailed(archivePath, extractDir, installToken,
                                  ExtractFailKind::ProgramMissing, "unzip");
        }

    } else if (ext == QStringLiteral("rar")) {
        auto trySevenZ = [this, archivePath, extractDir, installToken,
                          emitSuccess]() {
            auto *p2 = new QProcess(this);
            connect(p2, &QProcess::finished, this,
                    [this, p2, archivePath, extractDir, installToken, emitSuccess]
                    (int code2, QProcess::ExitStatus) {
                if (code2 != 0) {
                    logExtractFailure("7z", archivePath, extractDir, code2, p2);
                    p2->deleteLater();
                    emit extractionFailed(archivePath, extractDir, installToken,
                                          ExtractFailKind::NonzeroExit,
                                          QString::number(code2));
                    return;
                }
                p2->deleteLater();
                emitSuccess();
            });
            p2->start("7z", {"x", archivePath,
                              "-o" + extractDir, "-y"});
            if (!p2->waitForStarted(3000)) {
                p2->deleteLater();
                // Neither unrar nor 7z ran.
                emit extractionFailed(archivePath, extractDir, installToken,
                                      ExtractFailKind::ProgramMissing,
                                      "unrar|7z");
            }
        };

        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, this,
                [this, proc, archivePath, extractDir, installToken,
                 emitSuccess, trySevenZ]
                (int code, QProcess::ExitStatus) {
            proc->deleteLater();
            if (code != 0) {
                trySevenZ();   // unrar failed - retry with 7z
                return;
            }
            emitSuccess();
        });
        if (!launch(proc, "unrar",
                    {"x", "-o+", archivePath, extractDir + "/"}))  {
            proc->deleteLater();
            trySevenZ();   // unrar not found - try 7z directly
        }

    } else {
        // 7z handles everything else (7z, fomod, tar, gz, bz2, xz, iso, …)
        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, this,
                [this, proc, archivePath, extractDir, installToken, emitSuccess]
                (int code, QProcess::ExitStatus) {
            if (code != 0) {
                logExtractFailure("7z", archivePath, extractDir, code, proc);
                proc->deleteLater();
                emit extractionFailed(archivePath, extractDir, installToken,
                                      ExtractFailKind::NonzeroExit,
                                      QString::number(code));
                return;
            }
            proc->deleteLater();
            emitSuccess();
        });
        if (!launch(proc, "7z",
                    {"x", archivePath, "-o" + extractDir, "-y"})) {
            proc->deleteLater();
            emit extractionFailed(archivePath, extractDir, installToken,
                                  ExtractFailKind::ProgramMissing, "7z");
        }
    }
}
