// safe_fs (snapshotBackup / copyTreeVerified / forceRemoveRecursively) plus
// fs_utils sanitizeFolderName, all driven off a QTemporaryDir.

#include "safe_fs.h"
#include "fs_utils.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QTime>

#include <iostream>

#include "test_harness.h"

// -- safe_fs section --

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

// -- snapshotBackup --

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

    // Seed 6 snapshots with lexically-ordered timestamps (QDir::Name sort ==
    // chronological). Pin keep=5 explicitly so this exercises the pruning logic
    // regardless of the (larger) production default; the oldest drops after one
    // more snapshot.
    const QString base = live + ".bak.";
    writeFile(base + "20000101-000000", "oldest");
    writeFile(base + "20010101-000000", "old-2");
    writeFile(base + "20020101-000000", "old-3");
    writeFile(base + "20030101-000000", "old-4");
    writeFile(base + "20040101-000000", "old-5");
    writeFile(base + "20050101-000000", "old-6");
    check("pre-seeded 6 backups", countBackups(live) == 6);

    (void)safefs::snapshotBackup(live, /*keep=*/5);
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

    // keep=0 rotates away everything, including the snapshot just made.
    (void)safefs::snapshotBackup(live, /*keep=*/0);
    check("keep=0 leaves no backups on disk", countBackups(live) == 0);
}

static void testSnapshotCollisionSameSecond()
{
    std::cout << "testSnapshotCollisionSameSecond\n";
    QTemporaryDir dir;
    const QString live = dir.filePath("a.txt");
    writeFile(live, "original");

    // Two snapshots in the same second collide on filename; QFile::copy
    // refuses to overwrite, so the first must survive intact (not clobbered
    // or pruned to zero).
    //
    // The two calls have to actually land in the same second for the collision
    // to exist. That was left to luck, and on a CI runner the pair eventually
    // straddled a second boundary: the second snapshot then succeeds with a
    // different name (correctly!) and the assert fired as a flake. Wait for the
    // top of a fresh second so both calls fit inside it comfortably.
    while (QTime::currentTime().msec() > 200)
        QThread::msleep(10);
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

// -- backup mirror --
//
// Every mirror test passes its own mirrorRoot: the default is the real
// AppDataLocation, and a fake checkout under a QTemporaryDir must not leave
// snapshots there.

// A checkout the way build.sh lays one out: state in bin/Release_Linux/,
// which is ignored, so `git clean -xdf` deletes it.
struct FakeCheckout {
    QTemporaryDir tmp;
    QString root() const   { return tmp.filePath(QStringLiteral("checkout")); }
    QString live() const   { return root() + QStringLiteral("/bin/Release_Linux/modlist_morrowind.txt"); }
    QString mirrorRoot() const { return tmp.filePath(QStringLiteral("appdata/backups")); }
    explicit FakeCheckout(bool gitIsFile = false)
    {
        if (gitIsFile)
            writeFile(root() + QStringLiteral("/.git"), "gitdir: /elsewhere/.git/worktrees/x\n");
        else
            QDir().mkpath(root() + QStringLiteral("/.git"));
        writeFile(live(), "order-1\n");
    }
};

static void testMirrorOnlyForCheckouts()
{
    std::cout << "testMirrorOnlyForCheckouts\n";
    QTemporaryDir dir;
    const QString live = dir.filePath("bin/modlist.txt");
    writeFile(live, "order\n");
    const QString root = dir.filePath("appdata/backups");

    check("no work tree -> no mirror dir", safefs::backupMirrorDir(live, root).isEmpty());
    check("snapshot still succeeds", safefs::snapshotBackup(live, 20, root).has_value());
    check("and nothing is written under the mirror root", !QFileInfo::exists(root));
}

static void testMirrorWritesBothCopies()
{
    std::cout << "testMirrorWritesBothCopies\n";
    FakeCheckout co;
    check("the work tree is found from the live file",
          safefs::gitWorkTreeOf(co.live()) == QDir::cleanPath(co.root()));

    const QString mirror = safefs::backupMirrorDir(co.live(), co.mirrorRoot());
    check("mirror dir is under the mirror root, named for the checkout",
          mirror.startsWith(co.mirrorRoot() + QStringLiteral("/checkout-")), mirror);

    const auto bak = safefs::snapshotBackup(co.live(), 20, co.mirrorRoot());
    check("snapshot succeeds", bak.has_value());
    const QString name = QFileInfo(bak.value_or(QString())).fileName();
    check("the returned path is still the one beside the live file",
          bak.value_or(QString()).startsWith(co.live() + QStringLiteral(".bak.")));
    check("the mirror holds the same snapshot, same name",
          readFile(QDir(mirror).filePath(name)) == "order-1\n");
}

static void testMirrorInLinkedWorktree()
{
    std::cout << "testMirrorInLinkedWorktree\n";
    // A linked worktree's .git is a file pointing at the main repo. It is
    // just as much a checkout, and just as cleanable.
    FakeCheckout co(/*gitIsFile=*/true);
    check(".git as a file still marks a work tree",
          !safefs::backupMirrorDir(co.live(), co.mirrorRoot()).isEmpty());
}

static void testMirrorSurvivesDeepClean()
{
    std::cout << "testMirrorSurvivesDeepClean\n";
    FakeCheckout co;
    const QString before = safefs::backupMirrorDir(co.live(), co.mirrorRoot());
    const QString name =
        QFileInfo(safefs::snapshotBackup(co.live(), 20, co.mirrorRoot()).value_or(QString()))
            .fileName();

    // What `git clean -xdf` does to it: the ignored state dir goes, .git stays.
    QDir(co.root() + QStringLiteral("/bin")).removeRecursively();
    check("the live file and its neighbours are gone", !QFileInfo::exists(co.live()));
    check("the mirror dir is found again from the path alone",
          safefs::backupMirrorDir(co.live(), co.mirrorRoot()) == before);
    check("and the snapshot is still in it",
          readFile(QDir(before).filePath(name)) == "order-1\n");
}

static void testMirrorRotation()
{
    std::cout << "testMirrorRotation\n";
    FakeCheckout co;
    const QString mirror = safefs::backupMirrorDir(co.live(), co.mirrorRoot());
    const QString base = QDir(mirror).filePath(QStringLiteral("modlist_morrowind.txt.bak."));
    writeFile(base + "20000101-000000", "oldest");
    writeFile(base + "20010101-000000", "old-2");
    writeFile(base + "20020101-000000", "old-3");

    (void)safefs::snapshotBackup(co.live(), /*keep=*/2, co.mirrorRoot());
    check("the mirror is rotated to keep, like the live dir",
          QDir(mirror).entryList({"modlist_morrowind.txt.bak.*"}, QDir::Files).size() == 2);
    check("the oldest went first", !QFileInfo::exists(base + "20000101-000000"));
    check("the newest seeded one stayed", QFileInfo::exists(base + "20020101-000000"));
}

static void testMirrorSkipsUnchangedState()
{
    std::cout << "testMirrorSkipsUnchangedState\n";
    FakeCheckout co;
    const QString mirror = safefs::backupMirrorDir(co.live(), co.mirrorRoot());
    const QStringList pattern{"modlist_morrowind.txt.bak.*"};
    // The mirror already ends on exactly what the live file holds.
    writeFile(QDir(mirror).filePath("modlist_morrowind.txt.bak.20000101-000000"), "order-1\n");

    (void)safefs::snapshotBackup(co.live(), 20, co.mirrorRoot());
    check("a save that changed nothing takes no mirror slot",
          QDir(mirror).entryList(pattern, QDir::Files).size() == 1);
    check("the copy beside the live file is still written, as before",
          countBackups(co.live()) == 1);

    writeFile(co.live(), "order-2\n");
    (void)safefs::snapshotBackup(co.live(), 20, co.mirrorRoot());
    check("a changed list does",
          QDir(mirror).entryList(pattern, QDir::Files).size() == 2);
}

static void testMirrorKeyedByDirectory()
{
    std::cout << "testMirrorKeyedByDirectory\n";
    // Two clones with the same folder name must not read each other's lists.
    QTemporaryDir tmp;
    const QString root = tmp.filePath("appdata/backups");
    const QString a = tmp.filePath("one/checkout/bin/modlist.txt");
    const QString b = tmp.filePath("two/checkout/bin/modlist.txt");
    QDir().mkpath(tmp.filePath("one/checkout/.git"));
    QDir().mkpath(tmp.filePath("two/checkout/.git"));
    const QString ma = safefs::backupMirrorDir(a, root);
    const QString mb = safefs::backupMirrorDir(b, root);
    check("both are mirrored", !ma.isEmpty() && !mb.isEmpty());
    check("into different folders", ma != mb, ma + QStringLiteral(" / ") + mb);
}

// -- copyTreeVerified --

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

    // Enough files that the iterator still has work when we cancel.
    for (int i = 0; i < 20; ++i)
        writeFile(src + QString("/f%1.bin").arg(i, 2, 10, QChar('0')),
                  QByteArray(256, char('a' + i % 26)));

    auto res = safefs::copyTreeVerified(
        src, dst,
        /*isCancelled=*/[]{ return true; });   // cancelled up front
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

    // Mix of sizes to catch chunked-read errors at chunk boundaries.
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
    // Parent dir doesn't exist yet; mkpath must create it.
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
    // Pre-create the dst dir but not the file.
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
    writeFile(dst + "/conflict.txt", "old");  // collision

    auto res = safefs::copyTreeVerified(src, dst);
    const bool ok = res.has_value();
    const QString err = ok ? QString() : res.error();
    // QFile::copy won't overwrite. Must report failure AND wipe the dst tree:
    // all-or-nothing, since a half-migrated mod is the worst outcome.
    check("collision produces failure", !ok);
    check("err mentions copy failure",
          err.contains("copy failed"), "got: " + err);
    check("destination tree removed after failure",
          !QFileInfo::exists(dst));
}

// -- forceRemoveRecursively ---

static void testForceRemoveMissingPath()
{
    std::cout << "testForceRemoveMissingPath\n";
    QTemporaryDir dir;
    const QString gone = dir.filePath("never-existed");
    // No-op success on already-gone paths; no spurious failure.
    check("missing path treated as removed",
          safefs::forceRemoveRecursively(gone));
}

static void testForceRemovePlainTree()
{
    std::cout << "testForceRemovePlainTree\n";
    QTemporaryDir dir;
    const QString tree = dir.filePath("tree");
    writeFile(tree + "/top.txt", "x");
    writeFile(tree + "/sub/inner.txt", "y");

    check("plain tree removed", safefs::forceRemoveRecursively(tree));
    check("path no longer exists", !QFileInfo::exists(tree));
}

// A slow delete set aside for later: the real path free at once, under a name
// no scan mistakes for a mod folder, never clobbering one already set aside.
static void testSetAside()
{
    std::cout << "testSetAside\n";
    QTemporaryDir dir;
    const QString unpack = dir.filePath("8674709d_2");
    writeFile(unpack + "/00 Core/Mod.esm", "x");

    const QString first = safefs::setAside(unpack);
    check("renamed beside itself", first == unpack + ".__deleting__", first);
    check("the real path is free at once", !QFileInfo::exists(unpack));
    check("its contents came along", QFileInfo::exists(first + "/00 Core/Mod.esm"));

    writeFile(unpack + "/again.txt", "y");
    const QString second = safefs::setAside(unpack);
    check("a second one takes the next name", second == unpack + ".__deleting__1", second);
    check("the first is untouched", QFileInfo::exists(first + "/00 Core/Mod.esm"));

    check("nothing to set aside answers empty",
          safefs::setAside(dir.filePath("never-existed")).isEmpty()
              && safefs::setAside(QString()).isEmpty());
}

// One flush per install, where copying used to sync every file. It must be
// harmless on anything a job can hand it.
static void testFlushFileSystemOf()
{
    std::cout << "testFlushFileSystemOf\n";
    QTemporaryDir dir;
    writeFile(dir.filePath("mod/a.esp"), "x");
    safefs::flushFileSystemOf(dir.filePath("mod"));
    safefs::flushFileSystemOf(dir.filePath("mod/a.esp"));
    safefs::flushFileSystemOf(dir.filePath("never-existed"));
    safefs::flushFileSystemOf(QString());
    check("a folder, a file, nothing at all: no harm done",
          QFileInfo::exists(dir.filePath("mod/a.esp")));
}

static void testForceRemoveReadOnlyDirs()
{
    std::cout << "testForceRemoveReadOnlyDirs\n";
    // The "Move Mod Library" data-loss case: a mod dir missing the user-write
    // bit (common in trees from Windows-ACL zips, e.g. dr-xr-xr-x). Plain
    // QDir::removeRecursively can't unlink children inside it.
    QTemporaryDir dir;
    const QString tree = dir.filePath("readonly_tree");
    writeFile(tree + "/Textures/inside_readonly.dds", "pixels");
    writeFile(tree + "/sibling/normal.txt", "ok");

    // Strip user-write after seeding, else writeFile can't create the files.
    QFile::Permissions perms = QFile::permissions(tree + "/Textures");
    QFile::setPermissions(tree + "/Textures",
                          perms & ~QFile::WriteUser);

    // Plain remove is expected to fail on this tree.
    check("plain QDir::removeRecursively trips on read-only dir",
          !QDir(tree).removeRecursively() || !QFileInfo::exists(tree));

    // Reseed in case the plain remove above happened to succeed on some FS.
    if (!QFileInfo::exists(tree + "/Textures/inside_readonly.dds")) {
        QFile::setPermissions(tree + "/Textures", perms | QFile::WriteUser);
        writeFile(tree + "/Textures/inside_readonly.dds", "pixels");
        writeFile(tree + "/sibling/normal.txt", "ok");
        QFile::setPermissions(tree + "/Textures", perms & ~QFile::WriteUser);
    }

    check("forceRemoveRecursively succeeds on read-only-dir tree",
          safefs::forceRemoveRecursively(tree));
    check("entire tree gone after force remove",
          !QFileInfo::exists(tree));
}

// writableFilePath - the guard for the "Cannot write to <mods dir>/<uuid>"
// wedge: a bare-UUID CDN download extracts into a same-named FOLDER, so the
// next download of that same (stable) UUID resolved onto a directory and
// QFile::open(WriteOnly) failed forever, no matter how much space was freed.
static void testWritablePathFreeName()
{
    QTemporaryDir tmp;
    std::cout << "testWritablePathFreeName\n";
    const QString got = safefs::writableFilePath(tmp.path(), "Mod-123-1-0.7z");
    check("free name is returned unchanged",
          got == QDir(tmp.path()).filePath("Mod-123-1-0.7z"), got);
}

static void testWritablePathOverwritesExistingFile()
{
    QTemporaryDir tmp;
    std::cout << "testWritablePathOverwritesExistingFile\n";
    const QString live = QDir(tmp.path()).filePath("Mod-123-1-0.7z");
    writeFile(live, "old");
    const QString got = safefs::writableFilePath(tmp.path(), "Mod-123-1-0.7z");
    check("an existing regular file is reused (re-download overwrites)",
          got == live, got);
}

static void testWritablePathDivertsAroundDirectory()
{
    QTemporaryDir tmp;
    std::cout << "testWritablePathDivertsAroundDirectory\n";
    const QString uuid = "486264d1-8e9a-4882-9d8e-2d64d17906fc";
    QDir().mkpath(QDir(tmp.path()).filePath(uuid));

    const QString got = safefs::writableFilePath(tmp.path(), uuid);
    check("directory collision does not return the directory path",
          got != QDir(tmp.path()).filePath(uuid), got);
    check("extensionless name gets a bare _2 suffix, no dangling dot",
          got == QDir(tmp.path()).filePath(uuid + "_2"), got);

    QFile f(got);
    check("the diverted path is actually openable for writing",
          f.open(QIODevice::WriteOnly));
    f.close();
}

static void testWritablePathKeepsExtensionAndWalksSuffixes()
{
    QTemporaryDir tmp;
    std::cout << "testWritablePathKeepsExtensionAndWalksSuffixes\n";
    QDir().mkpath(QDir(tmp.path()).filePath("Mod-123-1-0.7z"));
    QDir().mkpath(QDir(tmp.path()).filePath("Mod-123-1-0_2.7z"));

    const QString got = safefs::writableFilePath(tmp.path(), "Mod-123-1-0.7z");
    check("suffix lands before the extension and skips taken names",
          got == QDir(tmp.path()).filePath("Mod-123-1-0_3.7z"), got);
}

// -- Is a download finished? -----------------------------------------------
//
// Cancelling an install keeps its archive so installing again reuses it
// instead of pulling half a gigabyte down a second time. What must never
// happen is reusing a HALF-written one: it would pass any magic-byte test,
// fail verification, and be deleted - costing the user the very download the
// feature exists to save.
static void run_is_complete_download()
{
    std::cout << "\n[safefs::isCompleteDownload]\n";
    QTemporaryDir dir;
    const QString full = dir.filePath("mod.zip");
    {
        QFile f(full);
        check("fixture opens", f.open(QIODevice::WriteOnly));
        f.write(QByteArray(1000, 'x'));
    }

    check("the promised size, exactly, is a finished download",
          safefs::isCompleteDownload(full, 1000));
    // The case that matters: a transfer that stopped early.
    check("short of it is not",
          !safefs::isCompleteDownload(full, 5000));
    check("and longer than it is not either",
          !safefs::isCompleteDownload(full, 900));
    // No promised size means no way to tell complete from truncated, and
    // guessing wrong deletes the file. Refusing costs one fresh download,
    // which is what would have happened regardless.
    check("an unknown size refuses rather than guesses",
          !safefs::isCompleteDownload(full, 0)
          && !safefs::isCompleteDownload(full, -1));
    check("a missing file is not a download",
          !safefs::isCompleteDownload(dir.filePath("nope.zip"), 1000));
    // A directory can own the name - an earlier extract of a bare-UUID
    // download lands exactly like that (see writableFilePath).
    QDir().mkpath(dir.filePath("adir"));
    check("nor is a directory",
          !safefs::isCompleteDownload(dir.filePath("adir"), 1000));
}

static void run_safe_fs()
{
    testWritablePathFreeName();
    testWritablePathOverwritesExistingFile();
    testWritablePathDivertsAroundDirectory();
    testWritablePathKeepsExtensionAndWalksSuffixes();

    testSnapshotMissingFile();
    testSnapshotHappyPath();
    testSnapshotRotation();
    testSnapshotKeepZero();
    testSnapshotCollisionSameSecond();

    testMirrorOnlyForCheckouts();
    testMirrorWritesBothCopies();
    testMirrorInLinkedWorktree();
    testMirrorSurvivesDeepClean();
    testMirrorRotation();
    testMirrorSkipsUnchangedState();
    testMirrorKeyedByDirectory();

    testForceRemoveMissingPath();
    testForceRemovePlainTree();
    testForceRemoveReadOnlyDirs();
    testSetAside();
    testFlushFileSystemOf();

    testCopyHappyPath();
    testCopyEmptyTree();
    testCopyCancelRemovesDst();
    testCopyPreservesContentBytes();
    testCopyDestinationParentMissing();
    testCopyIdempotentDstAlreadyExists();
    testCopyCollidingFileFails();
}

// -- fs_utils section --

static void fsutils_expect(const char *name, const QString &input, const QString &expected)
{
    QString got = fsutils::sanitizeFolderName(input);
    check(name, got == expected, got);
}

static void run_fs_utils()
{
    std::cout << "=== fs_utils tests ===\n";

    // empty / trivial
    fsutils_expect("empty → empty",                "",                        "");
    fsutils_expect("plain ASCII unchanged",        "OAAB Data",               "OAAB Data");
    fsutils_expect("underscores kept",             "OAAB_Data",               "OAAB_Data");
    fsutils_expect("digits kept",                  "Mod v2.3",                "Mod v2.3");

    // allowed punctuation
    fsutils_expect("hyphen",                       "a-b",                     "a-b");
    fsutils_expect("dot",                          "a.b",                     "a.b");
    fsutils_expect("parens",                       "Mod (v1)",                "Mod (v1)");
    fsutils_expect("apostrophe",                   "Arkngthand's Lost",       "Arkngthand's Lost");
    fsutils_expect("ampersand",                    "Tombs & Towers",          "Tombs & Towers");

    // filesystem-hostile chars get dropped
    fsutils_expect("forward slash dropped",        "bad/name",                "badname");
    fsutils_expect("backslash dropped",            "bad\\name",               "badname");
    fsutils_expect("colon dropped",                "bad:name",                "badname");
    fsutils_expect("star dropped",                 "bad*name",                "badname");
    fsutils_expect("question mark dropped",        "bad?name",                "badname");
    fsutils_expect("pipe dropped",                 "bad|name",                "badname");
    fsutils_expect("quote dropped",                "bad\"name",               "badname");
    fsutils_expect("angle brackets dropped",       "<bad>",                   "bad");
    fsutils_expect("null byte dropped",            QString("bad") + QChar(0) + "name",
                                                   "badname");

    // whitespace normalisation
    fsutils_expect("leading space stripped",       "   OAAB",                 "OAAB");
    fsutils_expect("trailing space stripped",      "OAAB   ",                 "OAAB");
    fsutils_expect("internal run collapsed",       "a    b",                  "a b");
    fsutils_expect("tab → space",                  "a\tb",                    "a b");
    fsutils_expect("newline → space",              "a\nb",                    "a b");
    fsutils_expect("nbsp → space",                 QString("a") + QChar(0x00A0) + "b",
                                                   "a b");

    // unicode letters preserved
    fsutils_expect("accented Latin preserved",     "Néréwarine",              "Néréwarine");
    fsutils_expect("Cyrillic preserved",           "Морровинд",               "Морровинд");
    fsutils_expect("CJK preserved",                "魔法師",                   "魔法師");
    fsutils_expect("Greek preserved",              "Μόροουιντ",               "Μόροουιντ");
    fsutils_expect("Arabic preserved",             "اللحن",                   "اللحن");

    // Symbols aren't letter-or-number, so they drop. BMP char keeps the
    // QChar literal ASCII-safe; surrogate pairs covered by Cyrillic/CJK above.
    fsutils_expect("symbol dropped",               QString("A") + QChar(0x00A9) + "B", // © copyright
                                                   "AB");

    // mixed good + bad
    fsutils_expect("mixed filesystem garbage",     "Mod<>:|Name/\\v1",        "ModNamev1");
    fsutils_expect("whitespace + garbage",         "   Mod  *  Name   ",      "Mod Name");

    // worst-case inputs shouldn't crash
    fsutils_expect("all invalid → empty",          "///***",                  "");
    fsutils_expect("only whitespace → empty",      " \t\n ",                  "");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    run_safe_fs();
    run_is_complete_download();
    run_fs_utils();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
