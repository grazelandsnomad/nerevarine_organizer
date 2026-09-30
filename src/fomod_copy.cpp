#include "fomod_copy.h"
#include "fomod_path.h"
#include "fs_progress.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtGlobal>
#include <QtTypes>

#ifdef Q_OS_LINUX
#include <linux/fs.h>     // FICLONE
#include <sys/ioctl.h>
#include <unistd.h>       // copy_file_range
#endif

namespace fomod_copy {

namespace {

constexpr QDir::Filters kEntries = QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot;

#ifdef Q_OS_LINUX
// The bytes of `in` into `out`, inside the kernel: a reflink where the
// filesystem shares blocks (btrfs, XFS), else copy_file_range. False when
// neither works for this pair, for the caller to copy by hand.
bool kernelCopy(int in, int out)
{
    if (::ioctl(out, FICLONE, in) == 0) return true;
    for (;;) {
        const ssize_t n = ::copy_file_range(in, nullptr, out, nullptr, 1 << 30, 0);
        if (n == 0) return true;   // end of file
        if (n < 0) return false;
    }
}

// Both files are open unbuffered, so their descriptors and QFile agree on
// where they stand.
bool copyBytes(QFile &in, QFile &out)
{
    // Trusted only when every byte arrived: a filesystem whose
    // copy_file_range reports the end early must not leave a short file.
    if (kernelCopy(in.handle(), out.handle()) && out.size() == in.size())
        return true;
    // Refused, failed part-way, or short: start the file over by hand.
    if (!in.seek(0) || !out.resize(0) || !out.seek(0)) return false;
    QByteArray buf(1 << 20, Qt::Uninitialized);
    for (;;) {
        const qint64 n = in.read(buf.data(), buf.size());
        if (n == 0) return true;
        if (n < 0 || out.write(buf.constData(), n) != n) return false;
    }
}
#endif

} // namespace

bool copyFile(const QString &src, const fomod::ResolvedPath &dst)
{
    const QString &d = dst.str();
    QDir().mkpath(QFileInfo(d).absolutePath());
    QFile::remove(d);   // last writer wins: a copy won't clobber an existing file
#ifdef Q_OS_LINUX
    // Not QFile::copy, which on Linux syncs every file it copies to disk: an
    // fdatasync of 2.7 ms a file on an NTFS mods drive, two thirds of copying
    // 54,000 of them. It also copies in the kernel (reflink, else
    // copy_file_range), so this does the same minus the sync; install_job
    // flushes once when the whole install is in place.
    QFile in(src), out(d);
    bool ok = in.open(QIODevice::ReadOnly | QIODevice::Unbuffered)
           && out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Unbuffered)
           && copyBytes(in, out);
    if (ok) out.setPermissions(in.permissions());   // as QFile::copy does
    out.close();
    if (!ok) QFile::remove(d);
#else
    const bool ok = QFile::copy(src, d);
#endif
    if (!ok) {
        qWarning("fomod_copy: failed to copy '%s' -> '%s'",
                 qUtf8Printable(src), qUtf8Printable(d));
        return false;
    }
    return true;
}

void copyContents(const QString &srcDir, const QString &dstDir, FsProgress *progress)
{
    // An empty destination is resolveDest refusing a "../" path, never "here":
    // to QDir it is the working directory.
    if (dstDir.isEmpty()) return;
    QDir src(srcDir);
    if (!src.exists()) return;
    const auto entries = src.entryInfoList(kEntries);
    if (entries.isEmpty()) return;
    QDir().mkpath(dstDir);
    // Reconcile each child against what's already staged so a file or folder
    // differing only in letter case ("meshes" vs an existing "Meshes") merges
    // in place instead of forking a duplicate directory on a case-sensitive
    // filesystem (the Project Atlas report). One listing of dstDir serves every
    // child; see fomod::DestIndex.
    fomod::DestIndex index(dstDir);
    for (const QFileInfo &fi : entries) {
        const fomod::ResolvedPath dst = index.child(fi.fileName());
        const QFileInfo made(dst.str());
        // Whatever now exists joins the index, as a fresh listing would show
        // it: an empty source folder creates nothing, a failed copy nothing.
        if (fi.isDir()) {
            copyDir(fi.absoluteFilePath(), dst, progress);   // dst decays to const QString&
            if (QFileInfo(made.absoluteFilePath()).isDir()) index.add(made.fileName());
        } else if (copyFile(fi.absoluteFilePath(), dst)) {   // last writer wins
            if (progress) progress->add(fi.size());
            index.add(made.fileName());
        }
    }
}

void copyDir(const QString &srcDir, const QString &dstDir, FsProgress *progress)
{
    // dstDir IS the new directory, so copying its children is copyContents.
    copyContents(srcDir, dstDir, progress);
}

qint64 sizeOf(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.exists()) return 0;
    if (!fi.isDir()) return fi.size();
    qint64 total = 0;
    for (const QFileInfo &child : QDir(path).entryInfoList(kEntries))
        total += sizeOf(child.absoluteFilePath());
    return total;
}

} // namespace fomod_copy
