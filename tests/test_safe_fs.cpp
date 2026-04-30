// tests/test_safe_fs.cpp
//
// Round-trips the two data-safety fs helpers that previously lived inside
// mainwindow.cpp with no coverage:
//
//   · safefs::snapshotBackup   - runs before every modlist + openmw.cfg
//                                 write.  Silent failure here is how the
//                                 README's "back up your files" disclaimer
//                                 becomes a lie.
//   · safefs::copyTreeVerified - runs when "Move Mod Library" crosses a
//                                 filesystem.  Silent data loss here kills
//                                 the user's whole mod library.
//
// Tests build fixtures in QTemporaryDir, so no mock network, no real mods.
//
// Build + run:
//   ./build/tests/test_safe_fs

#include "safe_fs.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok, const QString &detail = {})
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name << "\n";
        if (!detail.isEmpty())
            std::cout << "    " << detail.toStdString() << "\n";
        ++s_failed;
    }
}

// -- Fixtures ---

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(bytes);
    f.close();
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

static int countBackups(const QString &liveFile)
{
    QFileInfo fi(liveFile);
    return QDir(fi.absolutePath())
             .entryList({fi.fileName() + ".bak.*"}, QDir::Files)
             .size();
}

// -- snapshotBackup tests ---

static void testSnapshotMissingFile()
{
    std::cout << "testSnapshotMissingFile\n";
    QTemporaryDir dir;
    const auto out = safefs::snapshotBackup(dir.filePath("nope.txt"));
    check("missing live file → error, no crash", !out.has_value());
    check("no backups created", countBackups(dir.filePath("nope.txt")) == 0);
}

static void testSnapshotHappyPath()
{
    std::cout << "testSnapshotHappyPath\n";
    QTemporaryDir dir;
    const QString live = dir.filePath("modlist.txt");
    writeFile(live, "v1 contents\n");

    const auto bakRes = safefs::snapshotBackup(live);
    check("returned path is non-empty", bakRes.has_value());
    const QString bak = bakRes.value_or(QString());
    check("returned path matches .bak. pattern",
          bak.startsWith(live + ".bak."));
    check("live file still intact", readFile(live) == "v1 contents\n");
    check("backup contents match live",
          readFile(bak) == "v1 contents\n");
    check("exactly one backup on disk", countBackups(live) == 1);
}

static void testSnapshotRotation()
{
    std::cout << "testSnapshotRotation\n";
    QTemporaryDir dir;
    const QString live = dir.filePath("loadorder.txt");
    writeFile(live, "v0");

    // Pre-seed 6 existing snapshots with lexically-ordered timestamps so
    // QDir::Name sort matches chronological order.  The rotation must
    // keep exactly the newest 5 - so pre-existing "20000101-000000" is
    // the one that should disappear when we snapshot once more.
    const QString base = live + ".bak.";
    writeFile(base + "20000101-000000", "oldest");
    writeFile(base + "20010101-000000", "old-2");
    writeFile(base + "20020101-000000", "old-3");
    writeFile(base + "20030101-000000", "old-4");
    writeFile(base + "20040101-000000", "old-5");
    writeFile(base + "20050101-000000", "old-6");
    check("pre-seeded 6 backups", countBackups(live) == 6);

    (void)safefs::snapshotBackup(live);
    check("rotation kept exactly 5", countBackups(live) == 5);
    check("oldest snapshot was pruned",
          !QFileInfo::exists(base + "20000101-000000"));
    check("second-oldest was pruned",
          !QFileInfo::exists(base + "20010101-000000"));
    check("newest pre-existing survived",
          QFileInfo::exists(base + "20050101-000000"));
}

static void testSnapshotKeepZero()
{
    std::cout << "testSnapshotKeepZero\n";
    QTemporaryDir dir;
    const QString live = dir.filePath("x.txt");
    writeFile(live, "data");

    // Edge case: keep=0 means "rotate away everything, including the
    // snapshot we just made".  Not the documented use, but a sane output
    // (no lingering .bak files) beats "keeps the new one because we
    // counted wrong".
    (void)safefs::snapshotBackup(live, /*keep=*/0);
    check("keep=0 leaves no backups on disk", countBackups(live) == 0);
}

static void testSnapshotCollisionSameSecond()
{
    std::cout << "testSnapshotCollisionSameSecond\n";
    QTemporaryDir dir;
    const QString live = dir.filePath("a.txt");
    writeFile(live, "original");

    // First snapshot succeeds; a second call within the same second
    // cannot overwrite it (QFile::copy refuses) - the prior snapshot
    // must remain intact rather than getting silently clobbered or
    // somehow pruned to zero.
    const auto firstRes  = safefs::snapshotBackup(live);
    const auto secondRes = safefs::snapshotBackup(live);
    const QString first  = firstRes.value_or(QString());
    const QString second = secondRes.value_or(QString());
    check("first snapshot created", firstRes.has_value());
    check("second-in-same-second returned empty/error",
          !secondRes.has_value() || second == first);
    check("original snapshot still exists",
          QFileInfo::exists(first) && readFile(first) == "original");
}

// -- copyTreeVerified tests ---

static void testCopyHappyPath()
{
    std::cout << "testCopyHappyPath\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("src");
    const QString dst = dir.filePath("dst");

    writeFile(src + "/meshes/foo.nif",    QByteArray(1024, 'A'));
    writeFile(src + "/textures/bar.dds",  QByteArray(512,  'B'));
    writeFile(src + "/readme.txt",        "hello");

    auto res = safefs::copyTreeVerified(src, dst);
    const bool ok = res.has_value();
    const QString err = ok ? QString() : res.error();
    check("returns true", ok, err);
    check("nested file copied",
          readFile(dst + "/meshes/foo.nif") == QByteArray(1024, 'A'));
    check("sibling dir copied",
          readFile(dst + "/textures/bar.dds") == QByteArray(512, 'B'));
    check("top-level file copied",
          readFile(dst + "/readme.txt") == "hello");
    check("source untouched",
          QFileInfo::exists(src + "/readme.txt"));
}

static void testCopyEmptyTree()
{
    std::cout << "testCopyEmptyTree\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("empty_src");
    QDir().mkpath(src);

    auto res = safefs::copyTreeVerified(src, dir.filePath("empty_dst"));
    const bool ok = res.has_value();
    const QString err = ok ? QString() : res.error();
    check("empty source tree → success", ok, err);
    check("destination directory created",
          QFileInfo(dir.filePath("empty_dst")).isDir());
}

static void testCopyCancelRemovesDst()
{
    std::cout << "testCopyCancelRemovesDst\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("src");
    const QString dst = dir.filePath("dst");

    // Enough files that the iterator has work left to cancel against.
    for (int i = 0; i < 20; ++i)
        writeFile(src + QString("/f%1.bin").arg(i, 2, 10, QChar('0')),
                  QByteArray(256, char('a' + i % 26)));

    auto res = safefs::copyTreeVerified(
        src, dst,
        /*isCancelled=*/[]{ return true; });   // cancelled from the start
    const bool ok = res.has_value();
    const QString err = ok ? QString() : res.error();
    check("cancel returns false", !ok);
    check("err reports cancellation", err == "cancelled");
    check("destination tree cleaned up after cancel",
          !QFileInfo::exists(dst));
    check("source survives unharmed",
          QFileInfo::exists(src + "/f00.bin"));
}

static void testCopyPreservesContentBytes()
{
    std::cout << "testCopyPreservesContentBytes\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("src");
    const QString dst = dir.filePath("dst");

    // Mix of exact sizes to catch chunked-read errors at boundaries.
    writeFile(src + "/zero.bin",  QByteArray());
    writeFile(src + "/one.bin",   QByteArray(1, '\xFF'));
    writeFile(src + "/4k.bin",    QByteArray(4096, '\x42'));
    writeFile(src + "/odd.bin",   QByteArray(4097, '\x17'));

    const bool ok = safefs::copyTreeVerified(src, dst).has_value();
    check("returns true", ok);
    check("zero-byte file copied with matching size",
          QFileInfo(dst + "/zero.bin").exists()
          && QFileInfo(dst + "/zero.bin").size() == 0);
    check("1-byte file exact", readFile(dst + "/one.bin").size() == 1
                             && readFile(dst + "/one.bin").at(0) == '\xFF');
    check("4 KB file exact",   readFile(dst + "/4k.bin")    == QByteArray(4096, '\x42'));
    check("4097-byte file exact (off-boundary)",
          readFile(dst + "/odd.bin") == QByteArray(4097, '\x17'));
}

static void testCopyDestinationParentMissing()
{
    std::cout << "testCopyDestinationParentMissing\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("src");
    // Destination's parent doesn't exist yet - mkpath should handle it.
    const QString dst = dir.filePath("a/b/c/dst");

    writeFile(src + "/foo.txt", "x");
    const bool ok = safefs::copyTreeVerified(src, dst).has_value();
    check("deep destination path created via mkpath", ok);
    check("file at destination",
          readFile(dst + "/foo.txt") == "x");
}

static void testCopyIdempotentDstAlreadyExists()
{
    std::cout << "testCopyIdempotentDstAlreadyExists\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("src");
    const QString dst = dir.filePath("dst");

    writeFile(src + "/new.txt", "fresh");
    // Pre-create the dst directory but NOT the file.
    QDir().mkpath(dst);

    const bool ok = safefs::copyTreeVerified(src, dst).has_value();
    check("empty pre-existing dst dir still succeeds", ok);
    check("file lands inside pre-existing dst",
          readFile(dst + "/new.txt") == "fresh");
}

static void testCopyCollidingFileFails()
{
    std::cout << "testCopyCollidingFileFails\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("src");
    const QString dst = dir.filePath("dst");

    writeFile(src + "/conflict.txt", "new");
    writeFile(dst + "/conflict.txt", "old");  // pre-existing collision

    auto res = safefs::copyTreeVerified(src, dst);
    const bool ok = res.has_value();
    const QString err = ok ? QString() : res.error();
    // QFile::copy refuses to overwrite; we should report a copy failure
    // AND the destination tree must be cleaned up so the operation is
    // all-or-nothing - otherwise a half-migrated mod is the worst case.
    check("collision produces failure", !ok);
    check("err mentions copy failure",
          err.contains("copy failed"), "got: " + err);
    check("destination tree removed after failure",
          !QFileInfo::exists(dst));
}

// -- Entry point ---

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testSnapshotMissingFile();
    testSnapshotHappyPath();
    testSnapshotRotation();
    testSnapshotKeepZero();
    testSnapshotCollisionSameSecond();

    testCopyHappyPath();
    testCopyEmptyTree();
    testCopyCancelRemovesDst();
    testCopyPreservesContentBytes();
    testCopyDestinationParentMissing();
    testCopyIdempotentDstAlreadyExists();
    testCopyCollidingFileFails();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
