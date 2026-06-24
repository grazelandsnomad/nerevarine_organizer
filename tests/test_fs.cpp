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

#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

// Third arg is failure-only diagnostic text.
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
    // chronological). Rotation keeps the newest 5, so the oldest drops after
    // one more snapshot.
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

static void run_safe_fs()
{
    testSnapshotMissingFile();
    testSnapshotHappyPath();
    testSnapshotRotation();
    testSnapshotKeepZero();
    testSnapshotCollisionSameSecond();

    testForceRemoveMissingPath();
    testForceRemovePlainTree();
    testForceRemoveReadOnlyDirs();

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
    run_fs_utils();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
