#include "conflict_direction.h"
#include "plugin_records.h"
#include "plugin_strings.h"
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

// ---- conflict_direction ----

namespace cd_test {

using conflict_direction::Mod;
using conflict_direction::Directions;

// Three mods, load order top to bottom. Index 2 is the bottom = the winner.
static QList<Mod> mods()
{
    return { {"/mods/base", "Amazing Companion Tweaks"},
             {"/mods/other", "Unrelated Mod"},
             {"/mods/es",   "Amazing Companion Tweaks Spanish"} };
}

static QStringList names(const QList<conflict_direction::Counterpart> &cps)
{
    QStringList out;
    for (const auto &c : cps) out << c.name;
    return out;
}

static void testBottomWins()
{
    std::cout << "\n[the lower mod overwrites the higher one]\n";
    // Both ship the same plugin; the Spanish translation sits below.
    const QHash<QString, QList<int>> owners{
        {"amazing_companion_tweaks_sp0ckrates.esm", {0, 2}},
    };
    const auto res = conflict_direction::resolve(mods(), owners);

    check("both mods are reported", res.size() == 2,
          QString::number(res.size()));
    check("the lower mod overwrites the higher",
          names(res.value("/mods/es").overwrites)
              == QStringList{"Amazing Companion Tweaks"},
          names(res.value("/mods/es").overwrites).join(", "));
    check("the winner is not itself overwritten",
          res.value("/mods/es").overwrittenBy.isEmpty());
    check("the higher mod reports the loss",
          names(res.value("/mods/base").overwrittenBy)
              == QStringList{"Amazing Companion Tweaks Spanish"},
          names(res.value("/mods/base").overwrittenBy).join(", "));
    check("the loser overwrites nothing",
          res.value("/mods/base").overwrites.isEmpty());
    check("an uninvolved mod gets no entry", !res.contains("/mods/other"));
    check("the shared file is named",
          res.value("/mods/es").overwrites.value(0).files
              == QStringList{"amazing_companion_tweaks_sp0ckrates.esm"});
    check("the counterpart carries its path, not just a name",
          res.value("/mods/es").overwrites.value(0).path
              == QLatin1String("/mods/base"));
}

static void testReorderFlipsTheArrow()
{
    std::cout << "\n[moving a mod down flips who wins]\n";
    // Same pair, order swapped: the translation is now on top and loses.
    QList<Mod> swapped = { {"/mods/es", "Spanish"}, {"/mods/base", "Base"} };
    const QHash<QString, QList<int>> owners{{"shared.esm", {0, 1}}};
    const auto res = conflict_direction::resolve(swapped, owners);
    check("the mod on top is now the one overwritten",
          names(res.value("/mods/es").overwrittenBy) == QStringList{"Base"},
          names(res.value("/mods/es").overwrittenBy).join(", "));
    check("and it overwrites nothing",
          res.value("/mods/es").overwrites.isEmpty());
}

static void testMidStackWinsAndLoses()
{
    std::cout << "\n[a mod in the middle both overwrites and is overwritten]\n";
    const QHash<QString, QList<int>> owners{{"tex.dds", {0, 1, 2}}};
    const auto res = conflict_direction::resolve(mods(), owners);
    const Directions mid = res.value("/mods/other");
    check("middle mod overwrites the one above",
          names(mid.overwrites) == QStringList{"Amazing Companion Tweaks"},
          names(mid.overwrites).join(", "));
    check("middle mod is overwritten by the one below",
          names(mid.overwrittenBy) == QStringList{"Amazing Companion Tweaks Spanish"},
          names(mid.overwrittenBy).join(", "));
    check("the bottom mod beats both",
          names(res.value("/mods/es").overwrites).size() == 2,
          names(res.value("/mods/es").overwrites).join(", "));
}

static void testNoConflictWhenFilesDiffer()
{
    std::cout << "\n[mods that share no file produce nothing]\n";
    const QHash<QString, QList<int>> owners{
        {"a.esm", {0}}, {"b.esm", {1}}, {"c.esm", {2}},
    };
    check("no conflicts reported",
          conflict_direction::resolve(mods(), owners).isEmpty());
}

static void testFileListIsCappedAndSorted()
{
    std::cout << "\n[long shared-file lists are capped with a marker]\n";
    QHash<QString, QList<int>> owners;
    for (int i = 9; i >= 0; --i)   // inserted out of order on purpose
        owners.insert(QStringLiteral("f%1.dds").arg(i), {0, 2});
    const auto files =
        conflict_direction::resolve(mods(), owners, 3)
            .value("/mods/es").overwrites.value(0).files;
    check("capped to the limit plus one marker", files.size() == 4,
          QString::number(files.size()));
    check("files come back sorted", files.first() == QLatin1String("f0.dds"),
          files.join(", "));
    check("the marker counts what was hidden",
          files.last() == QLatin1String("+7 more"), files.last());
}

static void testOutOfRangeIndicesIgnored()
{
    std::cout << "\n[owner indices outside the mod list are dropped]\n";
    const QHash<QString, QList<int>> owners{{"x.esm", {0, 99}}, {"y.esm", {-1, 0}}};
    check("nothing is resolved from a bogus index",
          conflict_direction::resolve(mods(), owners).isEmpty());
}

} // namespace cd_test

static void run_conflict_direction()
{
    std::cout << "=== conflict_direction ===\n";
    cd_test::testBottomWins();
    cd_test::testReorderFlipsTheArrow();
    cd_test::testMidStackWinsAndLoses();
    cd_test::testNoConflictWhenFilesDiffer();
    cd_test::testFileListIsCappedAndSorted();
    cd_test::testOutOfRangeIndicesIgnored();
    std::cout << "\n";
}

// ---- plugin_records ----

namespace pr_test {

static void put32(QByteArray &b, quint32 v)
{
    b.append(char(v & 0xff));          b.append(char((v >>  8) & 0xff));
    b.append(char((v >> 16) & 0xff));  b.append(char((v >> 24) & 0xff));
}

// A minimal but structurally real TES4-family plugin: TES4 header with MAST
// fields, then one top-level GRUP holding `formIds` record headers.
static QByteArray makePlugin(const QStringList &masters,
                             const QList<quint32> &formIds,
                             const QByteArray &magic = "TES4")
{
    QByteArray fields;
    for (const QString &m : masters) {
        const QByteArray name = m.toLatin1() + '\0';
        fields.append("MAST");
        fields.append(char(name.size() & 0xff));
        fields.append(char((name.size() >> 8) & 0xff));
        fields.append(name);
        fields.append("DATA");                 // the size field that follows MAST
        fields.append(char(8)); fields.append(char(0));
        fields.append(QByteArray(8, '\0'));
    }

    QByteArray body;
    for (quint32 id : formIds) {
        body.append("NPC_");
        put32(body, 0);          // dataSize: no payload
        put32(body, 0);          // flags
        put32(body, id);
        body.append(QByteArray(8, '\0'));
    }

    QByteArray grup;
    grup.append("GRUP");
    put32(grup, quint32(24 + body.size()));    // groupSize INCLUDES the header
    grup.append("NPC_");
    put32(grup, 0);                            // groupType 0 = top level
    grup.append(QByteArray(8, '\0'));
    grup.append(body);

    QByteArray out;
    out.append(magic);
    put32(out, quint32(fields.size()));
    out.append(QByteArray(16, '\0'));          // rest of the 24-byte header
    out.append(fields);
    out.append(grup);
    return out;
}

static QString writePlugin(const QTemporaryDir &d, const QString &name,
                           const QByteArray &bytes)
{
    const QString path = d.filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) { f.write(bytes); f.close(); }
    return path;
}

static void testMastersAndOverrides()
{
    std::cout << "\n[a plugin's masters and overridden records are read]\n";
    QTemporaryDir d;
    // 0x00.. = first master, 0x01.. = second, 0x02.. = the plugin's own.
    const QString p = writePlugin(d, "a.esm",
        makePlugin({"Starfield.esm", "BlueprintShips-Starfield.esm"},
                   {0x0000a1c7, 0x0000a1c8, 0x01000005, 0x02000001, 0x02000002}));
    const auto pl = plugin_records::scan(p);
    check("plugin is recognised", pl.valid);
    check("masters read in order",
          pl.masters == QStringList{"Starfield.esm", "BlueprintShips-Starfield.esm"},
          pl.masters.join(", "));
    check("only master-owned records are collected", pl.overrides.size() == 3,
          QString::number(pl.overrides.size()));
    check("records key off the master they came from",
          pl.overrides.contains("starfield.esm:a1c7"),
          QStringList(pl.overrides.values()).join(", "));
    check("the second master is keyed separately",
          pl.overrides.contains("blueprintships-starfield.esm:5"));
    check("the plugin's own records are counted, not keyed",
          pl.ownRecords == 2, QString::number(pl.ownRecords));
}

static void testMorrowindPluginRejected()
{
    std::cout << "\n[a TES3 plugin is rejected on the magic]\n";
    QTemporaryDir d;
    const QString p = writePlugin(d, "mw.esm",
        makePlugin({"Morrowind.esm"}, {1, 2, 3}, "TES3"));
    const auto pl = plugin_records::scan(p);
    check("not treated as a TES4-family plugin", !pl.valid);
    check("nothing collected", pl.overrides.isEmpty());
}

static void testTranslationClashIsFound()
{
    std::cout << "\n[a translation clashes on records while sharing no file]\n";
    // The real shape: same masters, same overrides, own records that cannot
    // collide because each plugin renumbers them from its own load-order slot.
    QList<quint32> ids;
    for (quint32 i = 0; i < 40; ++i) ids << (0x00000100 + i);   // master overrides
    for (quint32 i = 0; i < 60; ++i) ids << (0x02000000 + i);   // own records

    QSet<QString> a, b;
    {
        QTemporaryDir d;
        a = plugin_records::scan(writePlugin(d, "Better Crowd Citizens.esm",
                makePlugin({"Starfield.esm", "Blueprint.esm"}, ids))).overrides;
        b = plugin_records::scan(writePlugin(d, "Better Crowd Citizens ES.esm",
                makePlugin({"Starfield.esm", "Blueprint.esm"}, ids))).overrides;
    }
    check("own records stay out of the comparison", a.size() == 40,
          QString::number(a.size()));

    const auto clashes = plugin_records::findClashes({
        {"/mods/en", "Better Crowd Citizens",         {"Better Crowd Citizens.esm"},    a},
        {"/mods/es", "Better Crowd Citizens Spanish", {"Better Crowd Citizens ES.esm"}, b},
    });
    check("both mods are reported", clashes.size() == 2,
          QString::number(clashes.size()));
    check("every overridable record is shared",
          clashes.value("/mods/es").value(0).shared == 40,
          QString::number(clashes.value("/mods/es").value(0).shared));
    check("the ratio is over the comparable records only, so 1.0",
          qFuzzyCompare(clashes.value("/mods/es").value(0).ofSmaller, 1.0),
          QString::number(clashes.value("/mods/es").value(0).ofSmaller));
    check("the counterpart is named and carries its path",
          clashes.value("/mods/es").value(0).modName
              == QLatin1String("Better Crowd Citizens")
       && clashes.value("/mods/es").value(0).modPath == QLatin1String("/mods/en"));
}

static void testSmallOrPartialOverlapIgnored()
{
    std::cout << "\n[a couple of coincidental shared records is not a clash]\n";
    QSet<QString> a, b, c;
    for (int i = 0; i < 40; ++i) a << QStringLiteral("starfield.esm:%1").arg(i, 0, 16);
    for (int i = 35; i < 80; ++i) b << QStringLiteral("starfield.esm:%1").arg(i, 0, 16);
    for (int i = 0; i < 4; ++i)  c << QStringLiteral("starfield.esm:%1").arg(i, 0, 16);

    // b shares 5 of its 45 with a: under both thresholds.
    check("a thin overlap is ignored",
          plugin_records::findClashes({{"/a", "A", {}, a}, {"/b", "B", {}, b}}).isEmpty());
    // c is fully contained in a but only 4 records: under minShared.
    check("a tiny set is ignored even at ratio 1.0",
          plugin_records::findClashes({{"/a", "A", {}, a}, {"/c", "C", {}, c}}).isEmpty());
    // Lowering the floor finds it, proving the threshold is what suppressed it.
    check("it is found once the floor is lowered",
          plugin_records::findClashes({{"/a", "A", {}, a}, {"/c", "C", {}, c}}, 4, 0.5)
              .size() == 2);
}

} // namespace pr_test

// ---- plugin_strings ----

namespace ps_test {

using pr_test::put32;
using pr_test::writePlugin;

// One record: 24-byte header then flat "SUB" size[2] data subrecords.
// `compressed` wraps the body the way the engine does - 4-byte little-endian
// uncompressed size, then a raw zlib stream - which is what qCompress produces
// once its big-endian length prefix is re-stamped.
static QByteArray makeRecord(const QByteArray &type, quint32 formId,
                             const QList<QPair<QByteArray, QByteArray>> &subs,
                             bool compressed = false)
{
    QByteArray body;
    for (const auto &s : subs) {
        const QByteArray data = s.second + '\0';
        body.append(s.first);
        body.append(char(data.size() & 0xff));
        body.append(char((data.size() >> 8) & 0xff));
        body.append(data);
    }
    quint32 flags = 0;
    if (compressed) {
        const quint32 raw = quint32(body.size());
        // qCompress emits big-endian size + zlib; swap the prefix for the
        // little-endian one the plugin format uses.
        QByteArray z = qCompress(body);
        z.remove(0, 4);
        QByteArray packed;
        put32(packed, raw);
        packed.append(z);
        body  = packed;
        flags = 0x00040000;
    }

    QByteArray out;
    out.append(type);
    put32(out, quint32(body.size()));
    put32(out, flags);
    put32(out, formId);
    out.append(QByteArray(8, '\0'));
    out.append(body);
    return out;
}

// TES4 header (optionally flagged Localized) + one top-level GRUP of records.
static QByteArray makeStringPlugin(const QByteArray &records,
                                   bool localized = false,
                                   const QByteArray &magic = "TES4")
{
    QByteArray grup;
    grup.append("GRUP");
    put32(grup, quint32(24 + records.size()));
    grup.append("WEAP");
    put32(grup, 0);
    grup.append(QByteArray(8, '\0'));
    grup.append(records);

    QByteArray out;
    out.append(magic);
    put32(out, 0);                       // no header fields
    put32(out, localized ? 0x80u : 0u);  // flags
    out.append(QByteArray(12, '\0'));    // formId + the rest of the 24 bytes
    out.append(grup);
    return out;
}

static void testOnlyPlayerFacingTypesAreRead()
{
    std::cout << "\n[only the record types a player reads are collected]\n";
    QTemporaryDir d;
    QByteArray recs;
    recs.append(makeRecord("WEAP", 0x01000001, {{"FULL", "Iron Sword"}}));
    recs.append(makeRecord("BOOK", 0x01000002, {{"FULL", "A Dance in Fire"},
                                                {"DESC", "Chapter one."}}));
    // Excluded types. NPC_ and QUST are the two that wrecked the raw
    // comparison on real data - proper names and editor ids that are supposed
    // to be identical in every language.
    recs.append(makeRecord("NPC_", 0x01000003, {{"FULL", "Addvar"}}));
    recs.append(makeRecord("QUST", 0x01000004, {{"FULL", "DialogueMarkarth"}}));
    // A type we do read, but a subrecord we do not.
    recs.append(makeRecord("WEAP", 0x01000005, {{"MODL", "weapons\\sword.nif"}}));

    const auto s = plugin_strings::extract(
        writePlugin(d, "a.esp", makeStringPlugin(recs)));
    check("plugin is recognised", s.valid);
    check("not flagged localized", !s.localized);
    check("three strings, from WEAP and BOOK only", s.byKey.size() == 3,
          QString::number(s.byKey.size()));
    check("FULL and DESC of one record are kept apart",
          s.byKey.value("BOOK:1000002:FULL:0") == "A Dance in Fire"
              && s.byKey.value("BOOK:1000002:DESC:0") == "Chapter one.");
    check("no NPC_ name leaked in",
          !s.byKey.values().contains(QStringLiteral("Addvar")));
    check("no QUST editor id leaked in",
          !s.byKey.values().contains(QStringLiteral("DialogueMarkarth")));
    check("a non-text subrecord is ignored",
          !s.byKey.values().contains(QStringLiteral("weapons\\sword.nif")));
}

// Skyrim compresses most sizeable records; missing this would silently read a
// large plugin as having almost no text, and report every mod as translated.
static void testCompressedRecordIsRead()
{
    std::cout << "\n[a zlib-compressed record body is decompressed]\n";
    QTemporaryDir d;
    const QByteArray rec = makeRecord("BOOK", 0x01000009,
        {{"FULL", "The Lusty Argonian Maid"}}, /*compressed=*/true);
    const auto s = plugin_strings::extract(
        writePlugin(d, "c.esp", makeStringPlugin(rec)));
    check("plugin is recognised", s.valid);
    check("the compressed record's text came through",
          s.byKey.value("BOOK:1000009:FULL:0") == "The Lusty Argonian Maid",
          s.byKey.value("BOOK:1000009:FULL:0"));
}

static void testLocalizedPluginReportsNoStrings()
{
    std::cout << "\n[a localized plugin holds string ids, not text]\n";
    QTemporaryDir d;
    const QByteArray rec = makeRecord("WEAP", 0x01000001, {{"FULL", "\x01\x00\x00\x00"}});
    const auto s = plugin_strings::extract(
        writePlugin(d, "loc.esp", makeStringPlugin(rec, /*localized=*/true)));
    check("plugin is recognised", s.valid);
    check("flagged localized", s.localized);
    check("no text read - it lives in Strings/", s.byKey.isEmpty(),
          QString::number(s.byKey.size()));
}

static void testMorrowindPluginRejected()
{
    std::cout << "\n[a Morrowind plugin is not a TES4-family plugin]\n";
    QTemporaryDir d;
    const QByteArray rec = makeRecord("WEAP", 0x01000001, {{"FULL", "Iron Sword"}});
    const auto s = plugin_strings::extract(
        writePlugin(d, "m.esp", makeStringPlugin(rec, false, "TES3")));
    check("rejected on the magic", !s.valid);
    check("nothing collected", s.byKey.isEmpty());
}

static void testComparisonSeparatesTranslatedFromNot()
{
    std::cout << "\n[comparing a translation against its original]\n";
    QTemporaryDir d;
    auto build = [](const QStringList &names) {
        QByteArray recs;
        quint32 id = 0x01000001;
        for (const QString &n : names)
            recs.append(makeRecord("WEAP", id++, {{"FULL", n.toUtf8()}}));
        return makeStringPlugin(recs);
    };
    const QStringList en = {"Iron Sword", "Iron Shield", "Steel Mace", "Riften"};
    const QStringList es = {"Espada de hierro", "Escudo de hierro",
                            "Maza de acero", "Riften"};

    const auto a = plugin_strings::extract(writePlugin(d, "en.esp", build(en)));
    const auto b = plugin_strings::extract(writePlugin(d, "es.esp", build(es)));
    const auto cmp = plugin_strings::compare(a, b);
    check("every record is compared", cmp.common == 4,
          QString::number(cmp.common));
    // The place name is the same word in Spanish - exactly the residue the
    // thresholds exist to tolerate.
    check("only the untranslated one counts as identical", cmp.identical == 1,
          QString::number(cmp.identical));
    check("the identical string is reported as a sample",
          cmp.samples == QStringList{"Riften"}, cmp.samples.join(", "));

    // Why BOTH thresholds exist, in one case: one proper noun out of four
    // strings is 25%, far past the ratio floor. On a four-string plugin that
    // means nothing, and the count requirement is what stops it being called
    // out. On a real plugin the same ratio would be hundreds of strings.
    check("the ratio alone would condemn it", cmp.ratio() >= plugin_strings::kPartialRatio);
    check("the count requirement spares it", cmp.identical < plugin_strings::kPartialCount);

    // The same plugin against itself: nothing translated at all.
    const auto self = plugin_strings::compare(a, a);
    check("an untranslated copy is 100% identical",
          self.common == 4 && self.identical == 4);

    // No shared records at all: not a translation pair, whatever it says.
    const auto other = plugin_strings::extract(
        writePlugin(d, "x.esp", [&]{
            QByteArray recs;
            quint32 id = 0x05000001;
            for (const QString &n : {"Glass Dagger", "Ebony Bow"})
                recs.append(makeRecord("WEAP", id++, {{"FULL", n.toUtf8()}}));
            return makeStringPlugin(recs);
        }()));
    check("unrelated plugins share nothing",
          plugin_strings::compare(a, other).common == 0);
}

// A texture or mesh replacer often ships a small plugin. It must read as
// "nothing to translate" rather than "untranslated", or the whole feature
// cries wolf on half a modlist.
static void testReplacerPluginHasNothingToTranslate()
{
    std::cout << "\n[a replacer plugin carries no player-facing text]\n";
    QTemporaryDir d;
    QByteArray recs;
    recs.append(makeRecord("WEAP", 0x01000001, {{"MODL", "weapons\\new.nif"}}));
    recs.append(makeRecord("STAT", 0x01000002, {{"MODL", "clutter\\bridge.nif"}}));
    const auto s = plugin_strings::extract(
        writePlugin(d, "r.esp", makeStringPlugin(recs)));
    check("valid, but with nothing to say", s.valid && s.byKey.isEmpty(),
          QString::number(s.byKey.size()));
}

} // namespace ps_test

static void run_plugin_records()
{
    std::cout << "=== plugin_records ===\n";
    pr_test::testMastersAndOverrides();
    pr_test::testMorrowindPluginRejected();
    pr_test::testTranslationClashIsFound();
    pr_test::testSmallOrPartialOverlapIgnored();
    std::cout << "\n";
}

static void run_plugin_strings()
{
    std::cout << "=== plugin_strings ===\n";
    ps_test::testOnlyPlayerFacingTypesAreRead();
    ps_test::testCompressedRecordIsRead();
    ps_test::testLocalizedPluginReportsNoStrings();
    ps_test::testMorrowindPluginRejected();
    ps_test::testComparisonSeparatesTranslatedFromNot();
    ps_test::testReplacerPluginHasNothingToTranslate();
    std::cout << "\n";
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    run_load_order_merge();
    run_scan_coordinator();
    run_conflict_direction();
    run_plugin_records();
    run_plugin_strings();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
