#include "installcontroller.h"

#include "install_layout.h"

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
                                      QListWidgetItem *placeholder,
                                      const QString &expectedMd5Lower,
                                      qint64 expectedSize)
{
    // No expectations → nothing to verify, proceed.
    if (expectedMd5Lower.isEmpty() && expectedSize <= 0) {
        emit verified(archivePath, placeholder);
        return;
    }

    // Size check is free and catches truncated downloads before we spend
    // multi-hundred-MB of MD5 cycles.
    if (expectedSize > 0) {
        const qint64 actualSize = QFileInfo(archivePath).size();
        if (actualSize != expectedSize) {
            emit verificationFailed(archivePath, placeholder,
                                    VerifyFailKind::Size,
                                    QString::number(actualSize),
                                    QString::number(expectedSize));
            return;
        }
    }

    // Size matched with no md5 to check → done.
    if (expectedMd5Lower.isEmpty()) {
        emit verified(archivePath, placeholder);
        return;
    }

    // MD5 is expensive - run on a worker thread.  QPointer guards against
    // the controller being destroyed mid-hash (app shutdown), which would
    // otherwise leave the emit target dangling.
    emit verificationStarted(archivePath);
    QPointer<InstallController> safeSelf(this);
    (void)QtConcurrent::run(
        [safeSelf, archivePath, placeholder, expectedMd5Lower]() {
        QString actualMd5;
        {
            QFile f(archivePath);
            if (f.open(QIODevice::ReadOnly)) {
                QCryptographicHash h(QCryptographicHash::Md5);
                // Stream-hash in ~1 MB chunks so we don't load the whole
                // archive into RAM and don't starve the worker of
                // responsiveness.
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
            [safeSelf, archivePath, placeholder, actualMd5, expectedMd5Lower]() {
            if (!safeSelf) return;
            if (actualMd5.compare(expectedMd5Lower, Qt::CaseInsensitive) != 0) {
                emit safeSelf->verificationFailed(
                    archivePath, placeholder,
                    VerifyFailKind::Md5,
                    actualMd5.isEmpty()
                        ? QStringLiteral("(read error)")
                        : actualMd5,
                    expectedMd5Lower);
                return;
            }
            emit safeSelf->verified(archivePath, placeholder);
        }, Qt::QueuedConnection);
    });
}

namespace {
// Folder name matches "this archive" if it's exactly the archive basename,
// or "<baseName>_<digits>" (the shape left behind by the legacy timestamp
// fallback below).  The digits-only check keeps coincidental prefix
// matches out - e.g. we won't treat "foo-1-0_extra" as a wrapper for a
// "foo-1-0" archive, only "foo-1-0" or "foo-1-0_1776436249".
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

// Returns absolute path of the in-place wrapper dir to reuse, or empty
// when the hint doesn't match this archive.  See extractArchive() doc.
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
    return rootDir.filePath(top);
}
} // namespace

void InstallController::extractArchive(const QString &archivePath,
                                       const QString &modsDir,
                                       QListWidgetItem *placeholder,
                                       const QString &reuseHintPath)
{
    const QFileInfo fi(archivePath);
    const QString baseName = fi.completeBaseName();
    QString extractDir = QDir(modsDir).filePath(baseName);

    // Stable-path reuse: when the caller has a prior wrapper for this
    // placeholder, extract back into it. Keeps cross-machine modlist syncs
    // pointing at the same folder and stops reinstalls from piling up
    // "<mod>_<ts1>", "<mod>_<ts2>" duplicates. Falls back to the timestamp
    // suffix when there's no usable hint.
    const QString reuseWrapper = resolveReuseWrapper(reuseHintPath, modsDir, baseName);
    if (!reuseWrapper.isEmpty()) {
        QDir(reuseWrapper).removeRecursively();
        extractDir = reuseWrapper;
    } else if (QDir(extractDir).exists()) {
        extractDir += "_" + QString::number(QDateTime::currentSecsSinceEpoch());
    }
    QDir().mkpath(extractDir);

    // After extraction, dive into a single-subdir archive so FOMOD
    // detection and addModFromPath see the real mod root.
    // install_layout::diveTarget owns the suppression rules.
    auto emitSuccess = [this, archivePath, extractDir, placeholder]() {
        const QDir dir(extractDir);
        const QStringList subdirs = dir.entryList(QDir::Dirs  | QDir::NoDotAndDotDot);
        const QStringList files   = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        const QString diveInto = install_layout::diveTarget(subdirs, files);
        const QString modPath = diveInto.isEmpty()
            ? extractDir
            : dir.filePath(diveInto);
        emit extractionSucceeded(archivePath, extractDir, modPath, placeholder);
    };

    // 7z handles most formats. For .rar, try unrar first (RAR5/solid),
    // then fall back to 7z's RAR codec. unrar-free fails on RAR5 archives,
    // and many distros ship it as the system unrar.
    const QString ext = fi.suffix().toLower();

    // Helper: launch a process; returns false if it couldn't start.
    auto launch = [this](QProcess *p, const QString &prog,
                         const QStringList &a) -> bool {
        p->start(prog, a);
        return p->waitForStarted(3000);
    };

    if (ext == QStringLiteral("zip")) {
        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, this,
                [this, proc, archivePath, extractDir, placeholder, emitSuccess]
                (int code, QProcess::ExitStatus) {
            proc->deleteLater();
            if (code != 0) {
                emit extractionFailed(archivePath, extractDir, placeholder,
                                      ExtractFailKind::NonzeroExit,
                                      QString::number(code));
                return;
            }
            emitSuccess();
        });
        if (!launch(proc, "unzip",
                    {"-o", archivePath, "-d", extractDir})) {
            proc->deleteLater();
            emit extractionFailed(archivePath, extractDir, placeholder,
                                  ExtractFailKind::ProgramMissing, "unzip");
        }

    } else if (ext == QStringLiteral("rar")) {
        // Try unrar; on any failure fall back to 7z.
        auto trySevenZ = [this, archivePath, extractDir, placeholder,
                          emitSuccess]() {
            auto *p2 = new QProcess(this);
            connect(p2, &QProcess::finished, this,
                    [this, p2, archivePath, extractDir, placeholder, emitSuccess]
                    (int code2, QProcess::ExitStatus) {
                p2->deleteLater();
                if (code2 != 0) {
                    emit extractionFailed(archivePath, extractDir, placeholder,
                                          ExtractFailKind::NonzeroExit,
                                          QString::number(code2));
                    return;
                }
                emitSuccess();
            });
            p2->start("7z", {"x", archivePath,
                              "-o" + extractDir, "-y"});
            if (!p2->waitForStarted(3000)) {
                p2->deleteLater();
                // Neither unrar nor 7z could run.
                emit extractionFailed(archivePath, extractDir, placeholder,
                                      ExtractFailKind::ProgramMissing,
                                      "unrar|7z");
            }
        };

        auto *proc = new QProcess(this);
        connect(proc, &QProcess::finished, this,
                [this, proc, archivePath, extractDir, placeholder,
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
                [this, proc, archivePath, extractDir, placeholder, emitSuccess]
                (int code, QProcess::ExitStatus) {
            proc->deleteLater();
            if (code != 0) {
                emit extractionFailed(archivePath, extractDir, placeholder,
                                      ExtractFailKind::NonzeroExit,
                                      QString::number(code));
                return;
            }
            emitSuccess();
        });
        if (!launch(proc, "7z",
                    {"x", archivePath, "-o" + extractDir, "-y"})) {
            proc->deleteLater();
            emit extractionFailed(archivePath, extractDir, placeholder,
                                  ExtractFailKind::ProgramMissing, "7z");
        }
    }
}
