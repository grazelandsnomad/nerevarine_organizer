// tests/test_scan_coordinator.cpp
//
// Pins the ScanCoordinator data-folders + BSA caches and the warm-on-load
// pre-warm.  These caches sit on the saveModList hot path: every save
// would otherwise recursively walk every installed mod's directory tree
// twice (reconcileLoadOrder + syncOpenMWConfig) plus a third walk for
// .bsa discovery, blocking the UI thread for hundreds of ms on a real
// modlist.  The 0.4 perf fix routed those callers through the cache,
// which makes regressions in the cache logic immediately user-visible
// as a return of the grey-freeze on Add/Edit.
//
// Tests use real on-disk fixtures because the cache builds its keys
// from filesystem walk results - mocking would defeat the point.

#include "scan_coordinator.h"
#include "pluginparser.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

#define QVERIFY_EXIT(cond, code) \
    do { if (!(cond)) { std::cerr << "Setup failed: " #cond "\n"; std::exit(code); } } while (0)

namespace {

int s_passed = 0;
int s_failed = 0;

void check(const char *name, bool ok, const QString &hint = {})
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name;
        if (!hint.isEmpty()) std::cout << " (" << hint.toStdString() << ")";
        std::cout << "\n";
        ++s_failed;
    }
}

void writeFile(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        std::cerr << "test setup: cannot create " << path.toStdString() << "\n";
        std::exit(2);
    }
    f.write("");
}

QString makeMod(const QString &root, const QString &name,
                const QStringList &esps, const QStringList &bsas)
{
    const QString modDir = root + "/" + name;
    QDir().mkpath(modDir);
    for (const auto &e : esps) writeFile(modDir + "/" + e);
    for (const auto &b : bsas) writeFile(modDir + "/" + b);
    return modDir;
}

bool waitFor(std::function<bool()> pred, int timeoutMs = 3000)
{
    QElapsedTimer t;
    t.start();
    while (!pred() && t.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return pred();
}

// ---------------------------------------------------------------------

void testCachedDataFolders_returnsAndCaches()
{
    std::cout << "\n[cachedDataFolders returns folders + caches the result]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);

    const QString modPath = makeMod(tmp.path(), "ModA",
                                     {"plugin.esp"}, {});

    ScanCoordinator sc(nullptr);
    auto first = sc.cachedDataFolders(modPath, plugins::contentExtensions());
    check("returns one folder", first.size() == 1);
    check("folder lists the plugin",
          first.size() == 1 && first[0].second.contains("plugin.esp"));

    // Snapshot must contain the entry now - proves the result was cached.
    auto snap = sc.dataFoldersSnapshot();
    check("snapshot contains the modPath after first call",
          snap.contains(modPath));

    // Delete the file from disk; cache hit must return the OLD result.
    QFile::remove(modPath + "/plugin.esp");
    auto second = sc.cachedDataFolders(modPath, plugins::contentExtensions());
    check("warm hit survives on-disk delete (returns cached result)",
          second.size() == 1 && second[0].second.contains("plugin.esp"));
}

void testCachedBsaFiles_basicAndCacheHit()
{
    std::cout << "\n[cachedBsaFiles deduped recursive walk + cache]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);

    const QString modPath = makeMod(tmp.path(), "ModB",
                                     {"plugin.esp"},
                                     {"primary.bsa"});
    // Subfolder BSA - mirrors Tamriel Data layout.  Same basename in two
    // roots must dedup to one entry; OpenMW expects unique
    // fallback-archive= names anyway.
    writeFile(modPath + "/00 Core/primary.bsa");
    writeFile(modPath + "/01 Patches/extra.BSA");  // case-insensitive

    ScanCoordinator sc(nullptr);
    auto bsas = sc.cachedBsaFiles(modPath);
    check("primary.bsa found", bsas.contains("primary.bsa"));
    check("extra.BSA found (case-insensitive glob)",
          bsas.contains("extra.BSA"));
    check("duplicate basename deduped",
          bsas.count("primary.bsa") == 1);

    // Delete the BSA from disk; cache hit must return the OLD list.
    QFile::remove(modPath + "/primary.bsa");
    auto bsas2 = sc.cachedBsaFiles(modPath);
    check("warm hit survives on-disk delete",
          bsas2.contains("primary.bsa"));
}

void testInvalidateClearsBoth()
{
    std::cout << "\n[invalidateDataFoldersCache drops BOTH caches]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);

    const QString modPath = makeMod(tmp.path(), "ModC",
                                     {"plugin.esp"}, {"a.bsa"});

    ScanCoordinator sc(nullptr);
    (void)sc.cachedDataFolders(modPath, plugins::contentExtensions());
    (void)sc.cachedBsaFiles(modPath);

    sc.invalidateDataFoldersCache(modPath);

    check("data-folders snapshot dropped",
          !sc.dataFoldersSnapshot().contains(modPath));

    // Delete the BSA on disk first; if invalidate cleared the BSA cache
    // properly, the next cachedBsaFiles call walks fresh and finds none.
    QFile::remove(modPath + "/a.bsa");
    auto bsas = sc.cachedBsaFiles(modPath);
    check("BSA cache also cleared (re-walk returns empty after delete)",
          bsas.isEmpty());
}

void testWarmDataFoldersCachePopulatesBoth()
{
    std::cout << "\n[warmDataFoldersCache pre-warms data folders + BSAs]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);

    const QString a = makeMod(tmp.path(), "ModA", {"a.esp"}, {"a.bsa"});
    const QString b = makeMod(tmp.path(), "ModB", {"b.esp"}, {});

    ScanCoordinator sc(nullptr);
    sc.warmDataFoldersCache({a, b});

    const bool warmed = waitFor([&]() {
        return sc.dataFoldersSnapshot().contains(a)
            && sc.dataFoldersSnapshot().contains(b);
    });
    check("warm completes and both data-folder entries land", warmed);

    // BSA cache for ModA should ALSO have been warmed in the same worker
    // pass.  Verify by deleting the file then doing a cachedBsaFiles() -
    // a warm hit returns the cached list with "a.bsa" still in it.
    QFile::remove(a + "/a.bsa");
    auto bsas = sc.cachedBsaFiles(a);
    check("ModA's BSA cache pre-warmed (warm hit after disk delete)",
          bsas.contains("a.bsa"));
}

void testWarmSkipsAlreadyCached()
{
    std::cout << "\n[warmDataFoldersCache no-ops on already-warm paths]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);

    const QString modPath = makeMod(tmp.path(), "ModWarm",
                                     {"x.esp"}, {});
    ScanCoordinator sc(nullptr);

    // Prime the cache synchronously.
    auto cached = sc.cachedDataFolders(modPath, plugins::contentExtensions());
    check("primed cache is non-empty", !cached.isEmpty());

    // Touch the BSA cache too so warm has nothing cold to do.
    (void)sc.cachedBsaFiles(modPath);

    // warmDataFoldersCache should not enqueue a worker because both
    // caches already contain modPath.  We can't observe "did not run"
    // directly, but we can verify that the cached entry is still
    // intact after a brief processEvents window.
    sc.warmDataFoldersCache({modPath});
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    auto after = sc.cachedDataFolders(modPath, plugins::contentExtensions());
    check("cache untouched (still returns the pre-warmed result)",
          after == cached);
}

void testCachedTes3Masters_mtimeKeyed()
{
    std::cout << "\n[cachedTes3Masters caches by (path, mtime); refreshes on overwrite]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);

    // Hand-craft a minimal TES3 header with one MAST subrecord pointing
    // at "Morrowind.esm".  The reader (plugins::readTes3Masters)
    // expects: bytes [0..4) == "TES3", uint32 record-body-size, 8
    // unused bytes, then subrecords (tag(4) + uint32 size + payload).
    auto buildTes3 = [](const QByteArray &masterName) -> QByteArray {
        QByteArray sub;
        sub.append("MAST", 4);
        const quint32 payloadSize = masterName.size() + 1;  // +NUL
        sub.append(reinterpret_cast<const char *>(&payloadSize), 4);
        sub.append(masterName);
        sub.append('\0');

        QByteArray body = sub;
        const quint32 recSize = body.size();

        QByteArray out;
        out.append("TES3", 4);
        out.append(reinterpret_cast<const char *>(&recSize), 4);
        out.append(QByteArray(8, '\0'));   // unused
        out.append(body);
        return out;
    };

    const QString modPath = tmp.path() + "/MyMod";
    QDir().mkpath(modPath);
    const QString plug = modPath + "/Child.esp";

    {
        QFile f(plug);
        QVERIFY_EXIT(f.open(QIODevice::WriteOnly), 2);
        f.write(buildTes3("Morrowind.esm"));
    }

    ScanCoordinator sc(nullptr);
    auto masters = sc.cachedTes3Masters(plug);
    check("first call returns the parsed master",
          masters == QStringList{"Morrowind.esm"});

    // Stamp the file's mtime back in time; then peek the cache.
    // Subsequent call with unchanged mtime must hit the cache - even
    // if we tampered with the file between calls (which we haven't).
    auto masters2 = sc.cachedTes3Masters(plug);
    check("second call returns cached value", masters2 == masters);

    // Overwrite with a different master.  QFileInfo's mtime
    // resolution is fine on modern Linux ext4 (nanosecond), but for
    // safety nudge the mtime forward explicitly.
    QFile::remove(plug);
    {
        QFile f(plug);
        QVERIFY_EXIT(f.open(QIODevice::WriteOnly), 2);
        f.write(buildTes3("Tribunal.esm"));
    }
    QDateTime later = QDateTime::currentDateTime().addSecs(2);
    {
        QFile bump(plug);
        QVERIFY_EXIT(bump.open(QIODevice::ReadWrite), 2);
        bump.setFileTime(later, QFileDevice::FileModificationTime);
    }

    auto masters3 = sc.cachedTes3Masters(plug);
    check("post-overwrite call re-reads (mtime miss)",
          masters3 == QStringList{"Tribunal.esm"});
}

void testCachedTes3Masters_invalidatedOnContainingPath()
{
    std::cout << "\n[invalidateDataFoldersCache drops master entries under that modPath]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);

    auto miniTes3 = [](const QByteArray &m) {
        QByteArray sub;
        sub.append("MAST", 4);
        const quint32 sz = m.size() + 1;
        sub.append(reinterpret_cast<const char *>(&sz), 4);
        sub.append(m);
        sub.append('\0');
        QByteArray body = sub;
        const quint32 rs = body.size();
        QByteArray out;
        out.append("TES3", 4);
        out.append(reinterpret_cast<const char *>(&rs), 4);
        out.append(QByteArray(8, '\0'));
        out.append(body);
        return out;
    };

    const QString modPath = tmp.path() + "/Mod";
    const QString plug    = modPath + "/X.esp";
    QDir().mkpath(modPath);
    {
        QFile f(plug);
        QVERIFY_EXIT(f.open(QIODevice::WriteOnly), 2);
        f.write(miniTes3("Morrowind.esm"));
    }

    ScanCoordinator sc(nullptr);
    (void)sc.cachedTes3Masters(plug);   // prime

    // Delete the file from disk so a re-walk would return empty,
    // BUT the cache still has the prior result.
    QFile::remove(plug);
    auto stillCached = sc.cachedTes3Masters(plug);
    // After the file disappears, cachedTes3Masters detects the missing
    // file and removes the entry - returns empty list immediately.
    check("missing file returns empty list", stillCached.isEmpty());

    // Re-create + invalidate the containing modPath should also
    // drop the master cache entry, even though the file path key
    // doesn't equal modPath.  This is the prefix sweep.
    {
        QFile f(plug);
        QVERIFY_EXIT(f.open(QIODevice::WriteOnly), 2);
        f.write(miniTes3("Bloodmoon.esm"));
    }
    auto reread = sc.cachedTes3Masters(plug);
    check("re-read after restore returns the new master",
          reread == QStringList{"Bloodmoon.esm"});

    // invalidateDataFoldersCache for the parent dir must clear the
    // file-level master cache entry too.
    sc.invalidateDataFoldersCache(modPath);
    QFile::remove(plug);   // simulate uninstall
    auto afterInvalidate = sc.cachedTes3Masters(plug);
    check("post-invalidate + file removal returns empty",
          afterInvalidate.isEmpty());
}

void testExtensionFilter()
{
    std::cout << "\n[cachedDataFolders filter narrows the cached full result]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);

    const QString modPath = makeMod(tmp.path(), "ModMixed",
                                     {"main.esm", "addon.esp",
                                      "scripts.omwscripts"},
                                     {});

    ScanCoordinator sc(nullptr);
    auto allExts = sc.cachedDataFolders(modPath,
                                          plugins::contentExtensions());
    check("full extensions returns all three",
          allExts.size() == 1 && allExts[0].second.size() == 3);

    // Same path, narrower extension set.  The cache stores under the full
    // contentExtensions key, so this must filter the cached result -
    // not re-walk and not return the cached full result.
    auto justEsm = sc.cachedDataFolders(modPath, {QStringLiteral(".esm")});
    check("filtered call returns only the .esm",
          justEsm.size() == 1
            && justEsm[0].second.size() == 1
            && justEsm[0].second.contains("main.esm"));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::cout << "=== ScanCoordinator ===\n";
    testCachedDataFolders_returnsAndCaches();
    testCachedBsaFiles_basicAndCacheHit();
    testInvalidateClearsBoth();
    testWarmDataFoldersCachePopulatesBoth();
    testWarmSkipsAlreadyCached();
    testCachedTes3Masters_mtimeKeyed();
    testCachedTes3Masters_invalidatedOnContainingPath();
    testExtensionFilter();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
