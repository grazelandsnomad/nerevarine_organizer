#include "load_order_merge.h"
#include "scan_coordinator.h"
#include "pluginparser.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <iostream>

#include "test_harness.h"

// ---- load_order_merge ----

namespace lom {

static QString fmt(const QStringList &ls)
{
    return "[" + ls.join(", ") + "]";
}

static void expect(const char *name,
                   const QStringList &prev,
                   const QStringList &cfg,
                   const QStringList &expected)
{
    QStringList got = loadorder::mergeLoadOrder(prev, cfg);
    bool ok = (got == expected);
    if (ok) {
        check(name, true);
    } else {
        std::cout << "  \033[31m✗\033[0m " << name << "\n";
        std::cout << "     expected " << fmt(expected).toStdString() << "\n";
        std::cout << "     got      " << fmt(got).toStdString() << "\n";
        ++s_failed;
    }
}

} // namespace lom

static void run_load_order_merge()
{
    using lom::expect;
    using lom::fmt;

    std::cout << "=== load_order_merge tests ===\n";

    // User dragged OAAB_Data.esm to the top in the Launcher; m_loadOrder still
    // has it last. Launcher position wins.
    expect("OAAB_Data.esm pulled to top (core regression)",
           {"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm",
            "SomeMod.esp", "OAAB_Data.esm"},
           {"OAAB_Data.esm", "Morrowind.esm", "Tribunal.esm",
            "Bloodmoon.esm", "SomeMod.esp"},
           {"OAAB_Data.esm", "Morrowind.esm", "Tribunal.esm",
            "Bloodmoon.esm", "SomeMod.esp"});

    expect("identity - no reorder",
           {"A.esm", "B.esp", "C.esp"},
           {"A.esm", "B.esp", "C.esp"},
           {"A.esm", "B.esp", "C.esp"});

    expect("two-plugin swap",
           {"A.esm", "B.esp"},
           {"B.esp", "A.esm"},
           {"B.esp", "A.esm"});

    // Disabled X.esp is absent from openmw.cfg; its slot among the managed
    // entries must survive the merge.
    expect("disabled plugin preserved around reorder",
           {"A.esm", "X.esp", "B.esp", "C.esp"},
           {"C.esp", "A.esm", "B.esp"},
           {"C.esp", "X.esp", "A.esm", "B.esp"});

    // Launcher saw a file we didn't: append to tail in cfg's order.
    expect("new plugin from launcher appended",
           {"A.esm", "B.esp"},
           {"A.esm", "B.esp", "D.esp"},
           {"A.esm", "B.esp", "D.esp"});

    // prev has X.esp, cfg doesn't: not in cfgSet, so treated as disabled and
    // kept at its slot (matches how absorb invokes it).
    expect("cfg omits plugin from prev (treated as disabled)",
           {"A.esm", "X.esp", "B.esp"},
           {"A.esm", "B.esp"},
           {"A.esm", "X.esp", "B.esp"});

    expect("empty prev, empty cfg",
           {}, {}, {});

    // Fresh install: cfg introduces everything.
    expect("empty prev → everything from cfg",
           {},
           {"A.esm", "B.esp"},
           {"A.esm", "B.esp"});

    // openmw.cfg wiped: return prev unchanged, never an empty list.
    expect("empty cfg → prev preserved",
           {"A.esm", "B.esp"},
           {},
           {"A.esm", "B.esp"});

    expect("reorder + new plugin + disabled plugin",
           {"A.esm", "X.esp", "B.esp", "C.esp"},
           {"C.esp", "B.esp", "A.esm", "D.esp"},
           {"C.esp", "X.esp", "B.esp", "A.esm", "D.esp"});

    // prev with a dup entry: output must stay unique.
    {
        QStringList got = loadorder::mergeLoadOrder(
            {"A.esm", "B.esp", "A.esm"},
            {"A.esm", "B.esp"});
        // Contract is "no duplicates", not a fixed order. Count A.esm.
        int aCount = 0;
        for (const QString &s : got) if (s == "A.esm") ++aCount;
        bool ok = (aCount == 1);
        if (ok) {
            std::cout << "  \033[32m✓\033[0m duplicate entries collapsed\n";
            ++s_passed;
        } else {
            std::cout << "  \033[31m✗\033[0m duplicate entries collapsed - got "
                      << fmt(got).toStdString() << "\n";
            ++s_failed;
        }
    }

    // Stops the Stargazer - Telescopes Cyrodiil crash: the reconcile pass
    // sorts filenames alphabetically per mod folder, so a child .omwaddon
    // whose stem has an extra " Suffix" sorts above its parent.
    auto topoExpect = [&](const char *name,
                          const QStringList &in,
                          const QHash<QString, QStringList> &masters,
                          const QStringList &expected) {
        QStringList got = loadorder::topologicallySortByMasters(
            in,
            [&masters](const QString &n) -> QStringList {
                return masters.value(n.toLower());
            });
        bool ok = (got == expected);
        if (ok) {
            check(name, true);
        } else {
            std::cout << "  \033[31m✗\033[0m " << name << "\n";
            std::cout << "     expected " << fmt(expected).toStdString() << "\n";
            std::cout << "     got      " << fmt(got).toStdString() << "\n";
            ++s_failed;
        }
    };

    std::cout << "\ntopologicallySortByMasters:\n";

    topoExpect("no masters declared → input order preserved",
               {"Stargazer.omwaddon",
                "Stargazer - Telescopes.omwaddon",
                "Stargazer - Telescopes Cyrodiil.omwaddon"},
               {},
               {"Stargazer.omwaddon",
                "Stargazer - Telescopes.omwaddon",
                "Stargazer - Telescopes Cyrodiil.omwaddon"});

    // Child above parent: with the Cyrodiil -> Telescopes edge declared, the
    // helper lifts Telescopes above Cyrodiil.
    topoExpect("child above parent → parent lifted (Stargazer crash regression)",
               {"Stargazer - Telescopes Cyrodiil.omwaddon",  // child FIRST
                "Stargazer - Telescopes.omwaddon",
                "Stargazer.omwaddon"},
               {
                 {"stargazer - telescopes cyrodiil.omwaddon",
                  {"Stargazer - Telescopes.omwaddon"}},
               },
               {"Stargazer - Telescopes.omwaddon",
                "Stargazer - Telescopes Cyrodiil.omwaddon",
                "Stargazer.omwaddon"});

    // Master lands above any dependent, even one earlier in the input.
    topoExpect("cross-mod master - dependent moved below",
               {"DependentA.esp", "DependentB.esp", "OAAB_Data.esm"},
               {
                 {"dependenta.esp", {"OAAB_Data.esm"}},
                 {"dependentb.esp", {"OAAB_Data.esm"}},
               },
               {"OAAB_Data.esm", "DependentA.esp", "DependentB.esp"});

    // Nexus authors mix case freely.
    topoExpect("case-insensitive master match",
               {"dependent.esp", "BASE.ESM"},
               {
                 {"dependent.esp", {"base.esm"}},  // lowercase decl
               },
               {"BASE.ESM", "dependent.esp"});

    // Uninstalled parent is ignored here (missing-master detection is
    // elsewhere); the known chain still resolves.
    topoExpect("master absent from input → skipped, no crash",
               {"Child.esp", "RealParent.esm"},
               {
                 {"child.esp", {"MissingExternal.esm", "RealParent.esm"}},
               },
               {"RealParent.esm", "Child.esp"});

    // Two-plugin cycle must terminate, each entry once.
    topoExpect("cycle → terminates, all entries emitted once",
               {"A.esp", "B.esp"},
               {
                 {"a.esp", {"B.esp"}},
                 {"b.esp", {"A.esp"}},
               },
               {"B.esp", "A.esp"});  // DFS order; invariant is no loop, no dups

    // Masters already above deps (e.g. post-LOOT): identity. Needs a stable sort.
    topoExpect("already-sorted input → identity",
               {"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm", "MyMod.esp"},
               {
                 {"tribunal.esm",  {"Morrowind.esm"}},
                 {"bloodmoon.esm", {"Morrowind.esm"}},
                 {"mymod.esp",     {"Morrowind.esm", "Bloodmoon.esm"}},
               },
               {"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm", "MyMod.esp"});

    topoExpect("empty input → empty output", {}, {}, {});

    std::cout << "\n";
}

// ---- scan_coordinator ----

#define SC_QVERIFY_EXIT(cond, code) \
    do { if (!(cond)) { std::cerr << "Setup failed: " #cond "\n"; std::exit(code); } } while (0)

namespace sc_test {

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
    SC_QVERIFY_EXIT(tmp.isValid(), 1);

    const QString modPath = makeMod(tmp.path(), "ModA",
                                     {"plugin.esp"}, {});

    ScanCoordinator sc(nullptr);
    auto first = sc.cachedDataFolders(modPath, plugins::contentExtensions());
    check("returns one folder", first.size() == 1);
    check("folder lists the plugin",
          first.size() == 1 && first[0].second.contains("plugin.esp"));

    // Snapshot now holds the entry, proving it was cached.
    auto snap = sc.dataFoldersSnapshot();
    check("snapshot contains the modPath after first call",
          snap.contains(modPath));

    // Delete on disk; warm hit must still return the old result.
    QFile::remove(modPath + "/plugin.esp");
    auto second = sc.cachedDataFolders(modPath, plugins::contentExtensions());
    check("warm hit survives on-disk delete (returns cached result)",
          second.size() == 1 && second[0].second.contains("plugin.esp"));
}

void testCachedBsaFiles_basicAndCacheHit()
{
    std::cout << "\n[cachedBsaFiles deduped recursive walk + cache]\n";
    QTemporaryDir tmp;
    SC_QVERIFY_EXIT(tmp.isValid(), 1);

    const QString modPath = makeMod(tmp.path(), "ModB",
                                     {"plugin.esp"},
                                     {"primary.bsa"});
    // Subfolder BSA, like Tamriel Data. Same basename in two roots must dedup
    // to one entry; OpenMW wants unique fallback-archive= names anyway.
    writeFile(modPath + "/00 Core/primary.bsa");
    writeFile(modPath + "/01 Patches/extra.BSA");  // case-insensitive

    ScanCoordinator sc(nullptr);
    auto bsas = sc.cachedBsaFiles(modPath);
    check("primary.bsa found", bsas.contains("primary.bsa"));
    check("extra.BSA found (case-insensitive glob)",
          bsas.contains("extra.BSA"));
    check("duplicate basename deduped",
          bsas.count("primary.bsa") == 1);

    // Delete on disk; warm hit must still return the old list.
    QFile::remove(modPath + "/primary.bsa");
    auto bsas2 = sc.cachedBsaFiles(modPath);
    check("warm hit survives on-disk delete",
          bsas2.contains("primary.bsa"));
}

void testInvalidateClearsBoth()
{
    std::cout << "\n[invalidateDataFoldersCache drops BOTH caches]\n";
    QTemporaryDir tmp;
    SC_QVERIFY_EXIT(tmp.isValid(), 1);

    const QString modPath = makeMod(tmp.path(), "ModC",
                                     {"plugin.esp"}, {"a.bsa"});

    ScanCoordinator sc(nullptr);
    (void)sc.cachedDataFolders(modPath, plugins::contentExtensions());
    (void)sc.cachedBsaFiles(modPath);

    sc.invalidateDataFoldersCache(modPath);

    check("data-folders snapshot dropped",
          !sc.dataFoldersSnapshot().contains(modPath));

    // If invalidate cleared the BSA cache, the next call re-walks and, with
    // the file gone, finds none.
    QFile::remove(modPath + "/a.bsa");
    auto bsas = sc.cachedBsaFiles(modPath);
    check("BSA cache also cleared (re-walk returns empty after delete)",
          bsas.isEmpty());
}

void testWarmDataFoldersCachePopulatesBoth()
{
    std::cout << "\n[warmDataFoldersCache pre-warms data folders + BSAs]\n";
    QTemporaryDir tmp;
    SC_QVERIFY_EXIT(tmp.isValid(), 1);

    const QString a = makeMod(tmp.path(), "ModA", {"a.esp"}, {"a.bsa"});
    const QString b = makeMod(tmp.path(), "ModB", {"b.esp"}, {});

    ScanCoordinator sc(nullptr);
    sc.warmDataFoldersCache({a, b});

    const bool warmed = waitFor([&]() {
        return sc.dataFoldersSnapshot().contains(a)
            && sc.dataFoldersSnapshot().contains(b);
    });
    check("warm completes and both data-folder entries land", warmed);

    // ModA's BSA cache should have warmed in the same pass: delete the file,
    // then a cachedBsaFiles() warm hit still lists "a.bsa".
    QFile::remove(a + "/a.bsa");
    auto bsas = sc.cachedBsaFiles(a);
    check("ModA's BSA cache pre-warmed (warm hit after disk delete)",
          bsas.contains("a.bsa"));
}

void testWarmSkipsAlreadyCached()
{
    std::cout << "\n[warmDataFoldersCache no-ops on already-warm paths]\n";
    QTemporaryDir tmp;
    SC_QVERIFY_EXIT(tmp.isValid(), 1);

    const QString modPath = makeMod(tmp.path(), "ModWarm",
                                     {"x.esp"}, {});
    ScanCoordinator sc(nullptr);

    // Prime synchronously.
    auto cached = sc.cachedDataFolders(modPath, plugins::contentExtensions());
    check("primed cache is non-empty", !cached.isEmpty());

    // Prime the BSA cache too so warm has nothing cold to do.
    (void)sc.cachedBsaFiles(modPath);

    // warm shouldn't enqueue a worker since both caches already hold modPath.
    // Can't observe "did not run", so just check the entry survives a brief
    // processEvents window.
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
    SC_QVERIFY_EXIT(tmp.isValid(), 1);

    // Minimal TES3 header with one MAST subrecord -> "Morrowind.esm". Reader
    // layout: "TES3", uint32 body-size, 8 unused bytes, then subrecords
    // (tag(4) + uint32 size + payload).
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
        out.append(QByteArray(8, '\0'));
        out.append(body);
        return out;
    };

    const QString modPath = tmp.path() + "/MyMod";
    QDir().mkpath(modPath);
    const QString plug = modPath + "/Child.esp";

    {
        QFile f(plug);
        SC_QVERIFY_EXIT(f.open(QIODevice::WriteOnly), 2);
        f.write(buildTes3("Morrowind.esm"));
    }

    ScanCoordinator sc(nullptr);
    auto masters = sc.cachedTes3Masters(plug);
    check("first call returns the parsed master",
          masters == QStringList{"Morrowind.esm"});

    // Unchanged mtime must hit the cache.
    auto masters2 = sc.cachedTes3Masters(plug);
    check("second call returns cached value", masters2 == masters);

    // Overwrite with a different master. ext4 mtime is nanosecond, but nudge
    // it forward explicitly to be safe.
    QFile::remove(plug);
    {
        QFile f(plug);
        SC_QVERIFY_EXIT(f.open(QIODevice::WriteOnly), 2);
        f.write(buildTes3("Tribunal.esm"));
    }
    QDateTime later = QDateTime::currentDateTime().addSecs(2);
    {
        QFile bump(plug);
        SC_QVERIFY_EXIT(bump.open(QIODevice::ReadWrite), 2);
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
    SC_QVERIFY_EXIT(tmp.isValid(), 1);

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
        SC_QVERIFY_EXIT(f.open(QIODevice::WriteOnly), 2);
        f.write(miniTes3("Morrowind.esm"));
    }

    ScanCoordinator sc(nullptr);
    (void)sc.cachedTes3Masters(plug);   // prime

    // File gone: cachedTes3Masters detects the missing file, drops the entry,
    // returns empty.
    QFile::remove(plug);
    auto stillCached = sc.cachedTes3Masters(plug);
    check("missing file returns empty list", stillCached.isEmpty());

    // Re-create, then invalidating the parent modPath must drop the master
    // entry too even though the file-path key != modPath (prefix sweep).
    {
        QFile f(plug);
        SC_QVERIFY_EXIT(f.open(QIODevice::WriteOnly), 2);
        f.write(miniTes3("Bloodmoon.esm"));
    }
    auto reread = sc.cachedTes3Masters(plug);
    check("re-read after restore returns the new master",
          reread == QStringList{"Bloodmoon.esm"});

    // Invalidating the parent dir must clear the file-level master entry too.
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
    SC_QVERIFY_EXIT(tmp.isValid(), 1);

    const QString modPath = makeMod(tmp.path(), "ModMixed",
                                     {"main.esm", "addon.esp",
                                      "scripts.omwscripts"},
                                     {});

    ScanCoordinator sc(nullptr);
    auto allExts = sc.cachedDataFolders(modPath,
                                          plugins::contentExtensions());
    check("full extensions returns all three",
          allExts.size() == 1 && allExts[0].second.size() == 3);

    // Same path, narrower ext set. Cache is keyed on full contentExtensions,
    // so this must filter the cached result, not re-walk and not return all.
    auto justEsm = sc.cachedDataFolders(modPath, {QStringLiteral(".esm")});
    check("filtered call returns only the .esm",
          justEsm.size() == 1
            && justEsm[0].second.size() == 1
            && justEsm[0].second.contains("main.esm"));
}

} // namespace sc_test

static void run_scan_coordinator()
{
    std::cout << "=== ScanCoordinator ===\n";
    sc_test::testCachedDataFolders_returnsAndCaches();
    sc_test::testCachedBsaFiles_basicAndCacheHit();
    sc_test::testInvalidateClearsBoth();
    sc_test::testWarmDataFoldersCachePopulatesBoth();
    sc_test::testWarmSkipsAlreadyCached();
    sc_test::testCachedTes3Masters_mtimeKeyed();
    sc_test::testCachedTes3Masters_invalidatedOnContainingPath();
    sc_test::testExtensionFilter();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    run_load_order_merge();
    run_scan_coordinator();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
