#include "conflict_direction.h"
#include "plugin_records.h"
#include "plugin_strings.h"
#include "plugin_writer.h"
#include "translation_mod.h"
#include "translation_store.h"
#include "target_language.h"
#include "google_translate.h"
#include "language_guess.h"
#include "lore_overrides.h"
#include "mod_package.h"
#include "term_protect.h"
#include "translation_rules.h"
#include "plugin_text.h"

#include <QUrlQuery>
#include <QRegularExpression>
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

    // Kept OUT of the ratio, but not thrown away: they are what tells us the
    // mod has something to translate at all.
    check("NPC_ and QUST land in the secondary tier", s.auxByKey.size() == 2,
          QString::number(s.auxByKey.size()));
    check("the NPC_ name is retained there",
          s.auxByKey.values().contains(QStringLiteral("Addvar")));
    check("the tiers do not overlap",
          s.byKey.size() == 3 && s.auxByKey.size() == 2);
}

// The reported bug. Varuun DLC items in base game (Starfield 11860) carries
// exactly one string - an NPC_ FULL - and a French translation exists for it.
// With NPC_ merely excluded, the string set came back empty, the scan dropped
// the plugin as having nothing to say, and the silence read as "already
// translated". The record shape here is that plugin's, minus the stringless
// LVLI/OTFT records.
static void testSecondaryOnlyPluginIsNotDropped()
{
    std::cout << "\n[a plugin whose only text is an NPC_ name still counts]\n";
    QTemporaryDir d;
    QByteArray recs;
    recs.append(makeRecord("NPC_", 0x01000001, {{"FULL", "Va'ruun Zealot"}}));
    const auto s = plugin_strings::extract(
        writePlugin(d, "varuun.esm", makeStringPlugin(recs)));

    check("plugin is recognised", s.valid);
    check("no core strings", s.byKey.isEmpty());
    check("but it is not empty", !s.empty());
    check("the string is there to be translated", s.auxByKey.size() == 1);
    // empty() is exactly the collection filter's test: this is what stopped
    // the plugin from being dropped before it could ever get a verdict.
    check("secondary-only is the tier the worker switches on",
          s.byKey.isEmpty() && !s.empty());

    // A genuine mesh replacer still says nothing - the silence that must stay.
    QByteArray meshOnly;
    meshOnly.append(makeRecord("WEAP", 0x01000002,
                               {{"MODL", "weapons\\sword.nif"}}));
    const auto m = plugin_strings::extract(
        writePlugin(d, "meshes.esp", makeStringPlugin(meshOnly)));
    check("a plugin with no text in either tier is still empty", m.empty());
}

// Geography stays put across languages. Traverse the Ulvenwald's only text is
// seven worldspace names and two tree names; flagging it would be crying wolf,
// so WRLD and TREE are in neither tier.
static void testGeographyStaysSilent()
{
    std::cout << "\n[worldspace and tree names are not translatable text]\n";
    QTemporaryDir d;
    QByteArray recs;
    recs.append(makeRecord("WRLD", 0x01000001, {{"FULL", "Ulvenwald"}}));
    recs.append(makeRecord("TREE", 0x01000002, {{"FULL", "Pine"}}));
    const auto s = plugin_strings::extract(
        writePlugin(d, "trees.esp", makeStringPlugin(recs)));
    check("plugin is recognised", s.valid);
    check("WRLD/TREE reach neither tier", s.empty(),
          QString::number(s.byKey.size() + s.auxByKey.size()));
}

// The tier a comparison runs on decides which numbers come out, and secondary
// text must never move a core ratio.
static void testComparisonIsPerTier()
{
    std::cout << "\n[comparison runs on one tier at a time]\n";
    QTemporaryDir d;

    // Both plugins: core text translated, secondary text (a personal name)
    // identical - the shape that made NPC_ look like 55.9% noise.
    QByteArray ra, rb;
    ra.append(makeRecord("WEAP", 0x01000001, {{"FULL", "Iron Sword"}}));
    ra.append(makeRecord("NPC_", 0x01000002, {{"FULL", "Addvar"}}));
    rb.append(makeRecord("WEAP", 0x01000001, {{"FULL", "Espada de hierro"}}));
    rb.append(makeRecord("NPC_", 0x01000002, {{"FULL", "Addvar"}}));

    const auto a = plugin_strings::extract(
        writePlugin(d, "en.esp", makeStringPlugin(ra)));
    const auto b = plugin_strings::extract(
        writePlugin(d, "es.esp", makeStringPlugin(rb)));

    const auto core = plugin_strings::compare(a, b);
    check("core pairs the weapon", core.common == 1);
    check("core sees it translated", core.identical == 0);
    check("an identical NPC_ name cannot inflate the core ratio",
          core.ratio() == 0.0);

    const auto sec = plugin_strings::compare(a, b, 8,
                                             plugin_strings::Tier::Secondary);
    check("secondary pairs the NPC_ name", sec.common == 1);
    check("and reports it identical", sec.identical == 1);
    // 100% identical here is not evidence of a missing translation - it is a
    // personal name. This is why the worker never derives a partial verdict
    // from secondary text.
    check("secondary ratio is exactly the figure not to be trusted",
          sec.ratio() == 1.0);
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
    // Historical name: this used to assert TES3 was rejected. TES3 is now a
    // supported family (see the t3_test suite); what this file writes here is
    // TES3 magic over TES4-shaped framing - i.e. a corrupt TES3 plugin - so
    // the assertion becomes robustness: recognised, walked, nothing invented.
    std::cout << "\n[TES3 magic over TES4 framing reads as an empty TES3 plugin]\n";
    QTemporaryDir d;
    const QByteArray rec = makeRecord("WEAP", 0x01000001, {{"FULL", "Iron Sword"}});
    const auto s = plugin_strings::extract(
        writePlugin(d, "m.esp", makeStringPlugin(rec, false, "TES3")));
    check("recognised as TES3 now", s.valid && s.tes3);
    check("garbage framing yields no strings", s.empty());
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


// ---- plugin_writer ----
//
// The scan says "Bandit Chief is still English"; the writer is what turns it
// into "Lider Bandido". Every test here exists because getting a size wrong
// corrupts a real mod: a subrecord's 2-byte size, the record's 4-byte
// dataSize and every enclosing GRUP's size all have to move together.
namespace pw_test {

using pr_test::writePlugin;
using ps_test::makeRecord;
using ps_test::makeStringPlugin;

// The property the whole design is held to. Verified separately against all 24
// plugins on the live Skyrim AE and Starfield lists, USSEP included.
static void testEmptyReplacementIsByteIdentical()
{
    std::cout << "\n[applying nothing reproduces the file exactly]\n";
    QTemporaryDir d;
    QByteArray recs;
    recs.append(makeRecord("WEAP", 0x01000001, {{"FULL", "Iron Sword"}}));
    recs.append(makeRecord("BOOK", 0x01000002, {{"FULL", "A Dance in Fire"},
                                                {"DESC", "Chapter one."}}));
    recs.append(makeRecord("NPC_", 0x01000003, {{"FULL", "Bandit Chief"}}));
    recs.append(makeRecord("BOOK", 0x01000004, {{"FULL", "Compressed"}},
                           /*compressed=*/true));
    const QString src = writePlugin(d, "src.esp", makeStringPlugin(recs));
    const QString dst = d.filePath("out.esp");

    const auto r = plugin_writer::apply(src, dst, {});
    check("round trip succeeds", r.ok, r.error);
    check("nothing was applied", r.applied == 0);

    QFile a(src), b(dst);
    check("both files open", a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
    check("output is byte-identical to input", a.readAll() == b.readAll());
}

static void testReplacementLandsAndSizesFollow()
{
    std::cout << "\n[a replaced string reads back, and the file still parses]\n";
    QTemporaryDir d;
    QByteArray recs;
    recs.append(makeRecord("NPC_", 0x01000001, {{"FULL", "Bandit Chief"}}));
    recs.append(makeRecord("WEAP", 0x01000002, {{"FULL", "Iron Sword"}}));
    const QString src = writePlugin(d, "src.esp", makeStringPlugin(recs));
    const QString dst = d.filePath("out.esp");

    plugin_writer::Replacements repl;
    // Longer than the original AND non-ASCII: the two things that break a
    // naive in-place patch.
    repl.insert("NPC_:1000001:FULL:0", QString::fromUtf8("Líder Bandido"));

    const auto r = plugin_writer::apply(src, dst, repl);
    check("apply succeeds", r.ok, r.error);
    check("one replacement landed", r.applied == 1);
    check("nothing was missed", r.missed.isEmpty());

    const auto out = plugin_strings::extract(dst);
    check("patched plugin still parses", out.valid);
    check("the new text is there",
          out.auxByKey.value("NPC_:1000001:FULL:0")
              == QString::fromUtf8("Líder Bandido"),
          out.auxByKey.value("NPC_:1000001:FULL:0"));
    check("the untouched record is unharmed",
          out.byKey.value("WEAP:1000002:FULL:0") == QStringLiteral("Iron Sword"));
    check("accented text survives as UTF-8",
          out.auxByKey.value("NPC_:1000001:FULL:0").contains(QChar(0x00ed)));
}

// Skyrim compresses most sizeable records. A replacement inside one has to be
// decompressed, patched and recompressed, and the record's dataSize then
// describes the NEW compressed length - not the old one, and not the raw one.
static void testReplacementInsideCompressedRecord()
{
    std::cout << "\n[a string inside a compressed record can be replaced]\n";
    QTemporaryDir d;
    const QByteArray rec = makeRecord("BOOK", 0x01000009,
        {{"FULL", "The Lusty Argonian Maid"}, {"DESC", "Chapter one."}},
        /*compressed=*/true);
    const QString src = writePlugin(d, "src.esp", makeStringPlugin(rec));
    const QString dst = d.filePath("out.esp");

    plugin_writer::Replacements repl;
    repl.insert("BOOK:1000009:FULL:0",
                QString::fromUtf8("La doncella argoniana lasciva"));

    const auto r = plugin_writer::apply(src, dst, repl);
    check("apply succeeds", r.ok, r.error);
    check("the compressed record was patched", r.applied == 1);

    const auto out = plugin_strings::extract(dst);
    check("it recompressed to something readable", out.valid);
    check("the new text is there",
          out.byKey.value("BOOK:1000009:FULL:0")
              == QString::fromUtf8("La doncella argoniana lasciva"));
    check("the sibling subrecord survived recompression",
          out.byKey.value("BOOK:1000009:DESC:0") == QStringLiteral("Chapter one."));
}

// Writing text over a 4-byte string ID would corrupt the Strings/ lookup, so
// the whole file is refused rather than half-written.
static void testLocalizedPluginIsRefused()
{
    std::cout << "\n[a localized plugin is refused, not half-written]\n";
    QTemporaryDir d;
    const QByteArray rec = makeRecord("BOOK", 0x01000001, {{"FULL", "\1\0\0\0"}});
    const QString src = writePlugin(d, "loc.esp",
                                    makeStringPlugin(rec, /*localized=*/true));
    const QString dst = d.filePath("out.esp");

    plugin_writer::Replacements repl;
    repl.insert("BOOK:1000001:FULL:0", QStringLiteral("Libro"));
    const auto r = plugin_writer::apply(src, dst, repl);
    check("refused", !r.ok);
    check("and says why", r.error.contains(QStringLiteral("localized")), r.error);
    check("no output file was left behind", !QFile::exists(dst));
}

static void testUnknownKeyIsReportedNotSilentlyDropped()
{
    std::cout << "\n[a key that matches nothing is reported]\n";
    QTemporaryDir d;
    const QByteArray rec = makeRecord("WEAP", 0x01000001, {{"FULL", "Iron Sword"}});
    const QString src = writePlugin(d, "src.esp", makeStringPlugin(rec));
    const QString dst = d.filePath("out.esp");

    plugin_writer::Replacements repl;
    repl.insert("WEAP:1000001:FULL:0", QStringLiteral("Espada de hierro"));
    repl.insert("BOOK:9999999:FULL:0", QStringLiteral("Nunca"));

    const auto r = plugin_writer::apply(src, dst, repl);
    check("the real one still lands", r.ok && r.applied == 1, r.error);
    check("the stale one is reported back",
          r.missed == QStringList{QStringLiteral("BOOK:9999999:FULL:0")},
          r.missed.join(QLatin1Char(',')));
}

static void testNonPluginIsRefused()
{
    std::cout << "\n[a non-plugin is refused]\n";
    QTemporaryDir d;
    // TES3 is a supported family now, so the refusal case needs a magic that
    // belongs to neither family.
    const QString src = writePlugin(d, "x.esm",
        makeStringPlugin(makeRecord("BOOK", 1, {{"FULL", "x"}}), false, "XXXX"));
    const auto r = plugin_writer::apply(src, d.filePath("out.esp"), {});
    check("foreign magic is rejected", !r.ok);
}

} // namespace pw_test

static void run_plugin_strings()
{
    std::cout << "=== plugin_strings ===\n";
    ps_test::testOnlyPlayerFacingTypesAreRead();
    ps_test::testCompressedRecordIsRead();
    ps_test::testLocalizedPluginReportsNoStrings();
    ps_test::testMorrowindPluginRejected();
    ps_test::testComparisonSeparatesTranslatedFromNot();
    ps_test::testReplacerPluginHasNothingToTranslate();
    ps_test::testSecondaryOnlyPluginIsNotDropped();
    ps_test::testGeographyStaysSilent();
    ps_test::testComparisonIsPerTier();
    std::cout << "\n";
}


// ---- TES3 (Morrowind) ----
//
// The other family: flat records, 16-byte headers, 4-byte subrecord sizes,
// CP1252 text, editor-id identity. Ported from what Nerevarine Scribe learned
// and verified against real plugins (Cyr_Main.esm: INFO's text is NAME with
// INAM identity; NPC_ display names are FNAM, their CNAM/RNAM are references).
namespace t3_test {

using pr_test::writePlugin;

static void putU32(QByteArray &b, quint32 v)
{
    b.append(char(v & 0xff));
    b.append(char((v >> 8) & 0xff));
    b.append(char((v >> 16) & 0xff));
    b.append(char((v >> 24) & 0xff));
}

static QByteArray sub(const QByteArray &type, const QByteArray &data)
{
    QByteArray out;
    out.append(type);
    putU32(out, quint32(data.size()));
    out.append(data);
    return out;
}

static QByteArray rec(const QByteArray &type, const QByteArray &subs)
{
    QByteArray out;
    out.append(type);
    putU32(out, quint32(subs.size()));
    putU32(out, 0);   // header1
    putU32(out, 0);   // flags
    out.append(subs);
    return out;
}

static QByteArray tes3Plugin(const QByteArray &records)
{
    QByteArray hedr;
    hedr.append(QByteArray(300, '\0'));   // version/author/desc blob, unread
    QByteArray out = rec("TES3", sub("HEDR", hedr));
    out.append(records);
    return out;
}

static void testExtractionTiers()
{
    std::cout << "\n[TES3: the right subrecords, in the right tiers]\n";
    QTemporaryDir d;

    QByteArray rs;
    // Core: an item name, a book with name + text, dialogue, a faction with
    // description and two rank names.
    rs.append(rec("WEAP", sub("NAME", "iron_sword\0") + sub("FNAM", "Iron Sword\0")
                        + sub("MODL", "w\\sword.nif\0")));
    rs.append(rec("BOOK", sub("NAME", "bk_maid\0") + sub("FNAM", "The Lusty Argonian Maid\0")
                        + sub("TEXT", "Chapter one.\0")));
    rs.append(rec("INFO", sub("INAM", "1234567890\0") + sub("ONAM", "fargoth\0")
                        + sub("NAME", "Greetings, outlander.")
                        + sub("BNAM", "Journal \"x\" 10\0")));
    rs.append(rec("FACT", sub("NAME", "fighters_guild\0") + sub("FNAM", "Fighters Guild\0")
                        + sub("RNAM", "Apprentice\0") + sub("RNAM", "Journeyman\0")
                        + sub("DESC", "Hired blades.\0")));
    rs.append(rec("GMST", sub("NAME", "sYes\0") + sub("STRV", "Yes\0")));
    // Secondary: NPC and creature display names. CNAM/RNAM here are class and
    // race REFERENCES and must never surface.
    rs.append(rec("NPC_", sub("NAME", "guard_hlaalu\0") + sub("FNAM", "Guard\0")
                        + sub("RNAM", "Dark Elf\0") + sub("CNAM", "Guard\0")));
    // Neither: DIAL topic and CELL name are identities; a script's text is a
    // desync hazard.
    rs.append(rec("DIAL", sub("NAME", "latest rumors\0")));
    rs.append(rec("CELL", sub("NAME", "Balmora, Guild of Mages\0")));
    rs.append(rec("SCPT", sub("SCHD", QByteArray(52, '\0')) + sub("SCTX", "Begin foo\0")));

    const auto s = plugin_strings::extract(
        writePlugin(d, "a.esp", tes3Plugin(rs)));
    check("recognised as TES3", s.valid && s.tes3);
    // WEAP FNAM, BOOK FNAM+TEXT, INFO NAME, FACT FNAM+RNAMx2+DESC, GMST STRV.
    check("core count", s.byKey.size() == 9, QString::number(s.byKey.size()));
    check("item name in core",
          s.byKey.value("WEAP:iron_sword:FNAM:0") == "Iron Sword");
    check("book text in core",
          s.byKey.value("BOOK:bk_maid:TEXT:0") == "Chapter one.");
    check("dialogue text keyed by INAM, not by its own content",
          s.byKey.value("INFO:1234567890:NAME:0") == "Greetings, outlander.");
    check("faction ranks get indices",
          s.byKey.value("FACT:fighters_guild:RNAM:0") == "Apprentice"
              && s.byKey.value("FACT:fighters_guild:RNAM:1") == "Journeyman");
    check("GMST string in core", s.byKey.value("GMST:sYes:STRV:0") == "Yes");
    check("NPC display name in secondary",
          s.auxByKey.value("NPC_:guard_hlaalu:FNAM:0") == "Guard");
    check("NPC race/class references never surface",
          !s.byKey.values().contains("Dark Elf")
              && !s.auxByKey.values().contains("Dark Elf"));
    check("DIAL topic is untouchable",
          !s.byKey.values().contains("latest rumors")
              && !s.auxByKey.values().contains("latest rumors"));
    check("CELL name is untouchable",
          !s.byKey.values().contains("Balmora, Guild of Mages")
              && !s.auxByKey.values().contains("Balmora, Guild of Mages"));
    check("script text is untouchable",
          !s.byKey.values().contains("Begin foo")
              && !s.auxByKey.values().contains("Begin foo"));
    check("INFO actor reference (ONAM) never surfaces",
          !s.byKey.values().contains("fargoth"));
}

static void testCp1252ReadsAccents()
{
    std::cout << "\n[TES3 text is CP1252, not UTF-8]\n";
    QTemporaryDir d;
    // "Cantina de Fenicio" with an accented o: byte 0xF3, invalid as UTF-8.
    QByteArray name("Cantina de Fenicio\0", 19);
    QByteArray accented;
    accented.append("Poci");
    accented.append(char(0xF3));
    accented.append("n de salud");
    accented.append('\0');
    const auto s = plugin_strings::extract(writePlugin(d, "es.esp",
        tes3Plugin(rec("ALCH", sub("NAME", "p_health\0") + sub("FNAM", accented)))));
    check("plugin read", s.valid && s.tes3);
    check("0xF3 decodes as an accented o",
          s.byKey.value("ALCH:p_health:FNAM:0") == QString::fromUtf8("Poción de salud"),
          s.byKey.value("ALCH:p_health:FNAM:0"));
}

static void testWriterRoundTripAndPatch()
{
    std::cout << "\n[TES3 writer: byte-identical round trip, CP1252 patch]\n";
    QTemporaryDir d;
    QByteArray rs;
    rs.append(rec("NPC_", sub("NAME", "bandit1\0") + sub("FNAM", "Bandit Chief\0")));
    rs.append(rec("WEAP", sub("NAME", "iron_sword\0") + sub("FNAM", "Iron Sword\0")));
    const QString src = writePlugin(d, "src.esp", tes3Plugin(rs));
    const QString dst = d.filePath("out.esp");

    // The property everything is held to, on this family too.
    const auto rt = plugin_writer::apply(src, dst, {});
    check("empty apply succeeds", rt.ok, rt.error);
    QFile a(src), b(dst);
    check("files open", a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
    check("round trip is byte-identical", a.readAll() == b.readAll());

    // The user's own example, accents included.
    plugin_writer::Replacements repl;
    repl.insert("NPC_:bandit1:FNAM:0", QString::fromUtf8("Líder Bandido"));
    const auto w = plugin_writer::apply(src, dst, repl);
    check("patch succeeds", w.ok && w.applied == 1, w.error);

    const auto out = plugin_strings::extract(dst);
    check("patched plugin still parses", out.valid && out.tes3);
    check("the translation reads back",
          out.auxByKey.value("NPC_:bandit1:FNAM:0")
              == QString::fromUtf8("Líder Bandido"),
          out.auxByKey.value("NPC_:bandit1:FNAM:0"));
    check("the untouched record is unharmed",
          out.byKey.value("WEAP:iron_sword:FNAM:0") == "Iron Sword");

    // On disk the accent must be ONE CP1252 byte (0xED), not two UTF-8 bytes.
    QFile f(dst);
    check("output opens", f.open(QIODevice::ReadOnly));
    const QByteArray bytes = f.readAll();
    QByteArray cp1252("L");
    cp1252.append(char(0xED));
    cp1252.append("der Bandido");
    check("accent written as a single CP1252 byte", bytes.contains(cp1252));
    check("no UTF-8 sequence leaked in", !bytes.contains(QByteArray("L\xC3\xAD")));
}

static void testWriterCannotTouchIdentity()
{
    std::cout << "\n[TES3 identities are unwritable by construction]\n";
    QTemporaryDir d;
    const QString src = writePlugin(d, "src.esp", tes3Plugin(
        rec("DIAL", sub("NAME", "latest rumors\0"))));
    const QString dst = d.filePath("out.esp");

    // Even a hand-forged key aimed at the topic name must miss: the shared
    // admission table says DIAL NAME is not text, so no key ever matches it.
    plugin_writer::Replacements repl;
    repl.insert("DIAL:latest rumors:NAME:0", QStringLiteral("ultimos rumores"));
    const auto w = plugin_writer::apply(src, dst, repl);
    check("apply succeeds", w.ok, w.error);
    check("nothing landed", w.applied == 0);
    check("the forged key is reported missed", w.missed.size() == 1);
    QFile a(src), b(dst);
    check("files open", a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
    check("file is untouched", a.readAll() == b.readAll());
}

} // namespace t3_test


// ---- target_language ----
//
// The language mods should be in, which for a while was silently the language
// the app's own menus were in. A user reading the app in English while
// translating to Spanish got "Create English translation", a memory file named
// translation_memory_english.json, and MyMemory asked to turn English into
// English.
namespace tl_test {

static void testLookups()
{
    std::cout << "\n[token -> name and ISO]\n";
    check("a language resolves to its name",
          target_language::displayName("spanish") == QStringLiteral("Spanish"));
    check("and to its ISO code",
          target_language::isoCode("spanish") == QStringLiteral("es"));
    check("lookup ignores case and whitespace",
          target_language::displayName("  SPANISH ") == QStringLiteral("Spanish"));
    check("isKnown agrees", target_language::isKnown("Spanish"));

    // The bug the old private table had: it spelled Chinese as the app's own
    // translation FILENAME, which would never match Bethesda's
    // Strings/<plugin>_chinese.* nor give MyMemory a usable code.
    check("chinese is the Bethesda token, not the app's filename",
          target_language::isKnown("chinese")
              && !target_language::isKnown("chinese_simplified"));
    check("and it still has an ISO code",
          target_language::isoCode("chinese") == QStringLiteral("zh"));

    // Languages Bethesda ships that the app has no interface translation for.
    check("polish is offerable",     target_language::isKnown("polish"));
    check("portuguese is offerable", target_language::isKnown("portuguese"));
    check("czech is offerable",      target_language::isKnown("czech"));
    check("korean is offerable",     target_language::isKnown("korean"));

    // An unknown token yields empty rather than echoing itself back: a caller
    // showing a name wants to know when it has nothing to show.
    check("unknown token has no name",
          target_language::displayName("klingon").isEmpty());
    check("unknown token has no ISO",
          target_language::isoCode("klingon").isEmpty());
    check("empty in, empty out",
          target_language::displayName(QString()).isEmpty()
              && !target_language::isKnown(QString()));
}

static void testTableIsWellFormed()
{
    std::cout << "\n[the table itself]\n";
    const QStringList toks = target_language::tokens();
    check("the table is not empty", !toks.isEmpty());

    QSet<QString> seen;
    bool dupes = false, badCase = false, missing = false;
    for (const QString &t : toks) {
        if (seen.contains(t)) dupes = true;
        seen.insert(t);
        if (t != t.toLower()) badCase = true;
        if (target_language::displayName(t).isEmpty()
            || target_language::isoCode(t).isEmpty()) missing = true;
    }
    check("no duplicate tokens", !dupes);
    check("every token is lowercase", !badCase);
    check("every token has a name and an ISO code", !missing);
    check("English is offerable too (translating INTO English is legitimate)",
          toks.contains(QStringLiteral("english")));
}

static void testResolution()
{
    std::cout << "\n[profile override beats the shared default]\n";
    check("the override wins",
          target_language::resolve("french", "spanish") == QStringLiteral("french"));
    check("an empty override falls back",
          target_language::resolve("", "spanish") == QStringLiteral("spanish"));
    check("whitespace counts as empty",
          target_language::resolve("   ", "spanish") == QStringLiteral("spanish"));
    // Both empty is the "never been asked" state. It must stay empty - the
    // caller has to be able to tell it apart from a real answer, and defaulting
    // to English here would put the original bug straight back.
    check("both empty stays empty",
          target_language::resolve(QString(), QString()).isEmpty());
    check("resolution normalises case",
          target_language::resolve("SPANISH", "") == QStringLiteral("spanish"));
}

static void testLocaleGuess()
{
    std::cout << "\n[the locale is only a first guess]\n";
    check("a full locale name maps",
          target_language::fromLocale("es_ES.UTF-8") == QStringLiteral("spanish"));
    check("a bare code maps",
          target_language::fromLocale("fr") == QStringLiteral("french"));
    // This machine's own locale, which is not the language its owner
    // translates into - the reason the guess is only ever a pre-selection.
    check("catalan is reachable from a locale",
          target_language::fromLocale("ca_ES") == QStringLiteral("catalan"));
    check("an unmapped locale guesses nothing",
          target_language::fromLocale("xx_XX").isEmpty());
    check("an empty locale guesses nothing",
          target_language::fromLocale(QString()).isEmpty());
}

} // namespace tl_test


// ---- google_translate ----
//
// The detail ported from Nerevarine Scribe that fails silently when got wrong:
// the response arrives SPLIT into segments. Measured live, a book-length
// description came back in five, the first holding 44 of 541 characters - so
// sampling instead of joining drops most of a book while still returning
// something that reads like a translation.
namespace gt_test {

static void testParsesSegments()
{
    std::cout << "\n[the response is joined, not sampled]\n";

    // One segment, the ordinary short-string case.
    check("a single segment comes through",
          google_translate::parseResponse(
              R"([[["Espada de hierro","Iron Sword",null,null,1]],null,"en"])")
              == QString::fromUtf8("Espada de hierro"));

    // Several segments: a long description Google chose to split. Every one
    // has to be concatenated in order - the bug this test exists to catch is
    // returning only "Chapter one. " and dropping the rest of the book.
    check("multiple segments are joined in order",
          google_translate::parseResponse(
              R"([[["Capitulo uno. ","Chapter one. ",null,null,1],)"
              R"(["El heroe partio.","The hero left.",null,null,1]],null,"en"])")
              == QString::fromUtf8("Capitulo uno. El heroe partio."));

    check("accented text survives",
          google_translate::parseResponse(
              "[[[\"Poci\xc3\xb3n de salud\",\"Health Potion\",null,null,1]],null,\"en\"]")
              == QString::fromUtf8("Poción de salud"));
}

static void testFailsSoft()
{
    std::cout << "\n[an unusable response yields nothing, never a guess]\n";
    // Every one of these must come back empty so the caller leaves the row for
    // the user instead of writing rubbish into a plugin.
    check("empty input",        google_translate::parseResponse({}).isEmpty());
    check("not JSON",           google_translate::parseResponse("<html>403</html>").isEmpty());
    check("JSON but an object", google_translate::parseResponse(R"({"error":"quota"})").isEmpty());
    check("empty array",        google_translate::parseResponse("[]").isEmpty());
    check("no segments",        google_translate::parseResponse("[[],null,\"en\"]").isEmpty());
    check("truncated payload",  google_translate::parseResponse(R"([[["only)").isEmpty());
}

static void testRequestShape()
{
    std::cout << "\n[request shape]\n";
    const QUrl url = google_translate::requestUrl(QStringLiteral("Bandit Chief"),
                                                  QStringLiteral("es"));
    const QString s = url.toString(QUrl::FullyDecoded);
    check("hits the free endpoint",
          url.host() == QLatin1String("translate.googleapis.com"), url.host());
    check("carries the target language", s.contains(QStringLiteral("tl=es")), s);
    check("lets Google detect the source",
          s.contains(QStringLiteral("sl=auto")), s);
    check("carries the text", s.contains(QStringLiteral("Bandit Chief")), s);

    // A string with URL metacharacters must survive as a query VALUE, not
    // break the query apart.
    const QUrl tricky = google_translate::requestUrl(
        QStringLiteral("Ring of Fire & Ice?"), QStringLiteral("fr"));
    check("special characters are encoded",
          !tricky.toString(QUrl::FullyEncoded).contains(QStringLiteral(" ")));
    check("and decode back to the original text",
          QUrlQuery(tricky).queryItemValue(QStringLiteral("q"),
                                           QUrl::FullyDecoded)
              == QStringLiteral("Ring of Fire & Ice?"));

    // Insurance rather than a proven requirement - see google_translate.h.
    check("a browser user agent is sent",
          google_translate::userAgent().contains("Mozilla/5.0"));
    // A whole mod goes through a queue, not a fan-out.
    check("concurrency is bounded",
          google_translate::kMaxInFlight > 0
              && google_translate::kMaxInFlight <= 8);
}

} // namespace gt_test


// ---- plugin_text ----
namespace pt_test {

static void testEncodingDetection()
{
    std::cout << "\n[UTF-8 or CP1252, decided per file]\n";
    QByteArray cp("T"); cp.append(char(0xE9)); cp.append("cnico");   // CP1252 e-acute
    const QByteArray u8  = QByteArray("Poci") + "\xc3\xb3" + "n";     // UTF-8 o-acute
    const QByteArray ascii("Iron Sword");

    // The bug this exists to prevent: QStringDecoder::decode() returns a LAZY
    // proxy, so discarding the result left hasError() false for every input.
    // Detection then said "UTF-8" for everything and put a replacement glyph
    // through "Técnico" in the editor.
    check("a CP1252 byte is not valid UTF-8",
          !plugin_text::isValidUtf8(cp.constData(), cp.size()));
    check("real UTF-8 is",
          plugin_text::isValidUtf8(u8.constData(), u8.size()));
    check("ASCII is (it is valid as both)",
          plugin_text::isValidUtf8(ascii.constData(), ascii.size()));

    // observe() is one-way: evidence accumulates and cannot be argued back.
    auto e = plugin_text::Encoding::Utf8;
    plugin_text::observe(e, ascii.constData(), ascii.size());
    check("ASCII leaves the verdict alone", e == plugin_text::Encoding::Utf8);
    plugin_text::observe(e, cp.constData(), cp.size());
    check("one CP1252 string settles the file", e == plugin_text::Encoding::Cp1252);
    plugin_text::observe(e, u8.constData(), u8.size());
    check("later UTF-8 cannot argue it back", e == plugin_text::Encoding::Cp1252);
}

static void testRoundTrip()
{
    std::cout << "\n[decode and encode agree]\n";
    QByteArray cp("T"); cp.append(char(0xE9)); cp.append("cnico");
    check("CP1252 decodes",
          plugin_text::decode(cp.constData(), cp.size(),
                              plugin_text::Encoding::Cp1252)
              == QString::fromUtf8("Técnico"));
    check("and re-encodes to the same single byte",
          plugin_text::encode(QString::fromUtf8("Técnico"),
                              plugin_text::Encoding::Cp1252) == cp);
    check("UTF-8 round-trips too",
          plugin_text::encode(QString::fromUtf8("Técnico"),
                              plugin_text::Encoding::Utf8)
              == QByteArray("T\xc3\xa9" "cnico"));
    // Reading CP1252 as UTF-8 is what produced the broken glyph on screen.
    check("reading CP1252 as UTF-8 mangles it",
          plugin_text::decode(cp.constData(), cp.size(),
                              plugin_text::Encoding::Utf8)
              != QString::fromUtf8("Técnico"));
    check("a character CP1252 cannot hold becomes '?'",
          plugin_text::encode(QString::fromUtf8("日本"),
                              plugin_text::Encoding::Cp1252) == QByteArray("??"));
}

} // namespace pt_test

// ---- language_guess ----
//
// A mod can have no translation partner because it IS the translation. Better
// Crowd Citizens Spanish ships only the Spanish version - its page says
// "ENGLISH MOD IS NOT NEEDED" - so nothing paired with it and it was flagged
// red, offering to translate "Ciudadana" into Spanish.
namespace lg_test {

// Verbatim from that mod.
static QStringList spanishMod()
{
    return {"Ciudadana", "Ciudadano", "Cliente", "Empleada de Ryujin",
            "Empleado de Ryujin", "Estudiante", "Trabajador de Generdyne",
            "Trabajador de Xenofresh", "Trabajadora de Generdyne",
            "Trabajadora de Xenofresh", QString::fromUtf8("Técnico")};
}

static void testSpotsAlreadyTranslated()
{
    std::cout << "\n[a mod that is already the translation]\n";
    check("Spanish text reads as Spanish",
          language_guess::textLooksLike(spanishMod(), "spanish"));
    check("and the whole test agrees",
          language_guess::alreadyInLanguage(
              "Better Crowd Citizens Spanish (ENGLISH MOD IS NOT NEEDED)",
              spanishMod(), "spanish"));
    check("the name alone is recognised",
          language_guess::nameSuggestsLanguage(
              "Better Crowd Citizens Spanish", "spanish"));
}

static void testEnglishIsNeverSilenced()
{
    std::cout << "\n[English mods keep their warning]\n";
    const QStringList english{"Iron Sword", "Bandit Chief", "The Lusty Argonian Maid",
                              "Guard", "A Dance in Fire", "Health Potion",
                              "Ring of Fire", "Steel Helmet", "Ancient Nord Axe",
                              "Chapter one of the tale"};
    check("English text does not read as Spanish",
          !language_guess::textLooksLike(english, "spanish"));
    check("nor is it silenced", !language_guess::alreadyInLanguage(
              "Weapons of the Third Era", english, "spanish"));

    // One Spanish-looking proper noun must not flip an English mod.
    QStringList sprinkled = english;
    sprinkled << "Casa de Vivec";
    check("a lone Spanish place name is not enough",
          !language_guess::textLooksLike(sprinkled, "spanish"));
}

// The regression this rule was rewritten for. Bonemold weapons is a RUSSIAN
// Morrowind mod storing Cyrillic in CP1251; read as CP1252 it becomes
// "Ñóíäóê" - full of characters that are also Spanish. Judging on accented
// letters scored it 74% Spanish while the real Spanish mod scored 3%, so the
// rule went to function words only.
static void testMojibakedCyrillicIsNotSpanish()
{
    std::cout << "\n[CP1251 Cyrillic read as CP1252 is not Spanish]\n";
    const QStringList russian{
        QString::fromUtf8("Ñóíäóê"), QString::fromUtf8("Ñòðàæíèê Òåëâàííè"),
        QString::fromUtf8("Ñêåëåò"), QString::fromUtf8("Õåéôíèð"),
        QString::fromUtf8("Âûñîõøèé òðóï"), QString::fromUtf8("Àðíñêàð"),
        QString::fromUtf8("Ñòðàæíèê Ðåäîðàíà"), QString::fromUtf8("Òåðèñ Ðàëåäðàí"),
        QString::fromUtf8("Àäìèðàë Ðîëñòîí"), QString::fromUtf8("Êàìàñ")};
    check("accent-heavy mojibake does not read as Spanish",
          !language_guess::textLooksLike(russian, "spanish"));
    check("and the mod keeps its warning",
          !language_guess::alreadyInLanguage("Bonemold Weapons", russian, "spanish"));
}

static void testSilenceWhenUnsure()
{
    std::cout << "\n[no marker data means no verdict]\n";
    check("a language with no word list cannot decide",
          !language_guess::textLooksLike(spanishMod(), "korean"));
    check("English is never 'already translated'",
          !language_guess::textLooksLike({"the sword of the north"}, "english"));
    check("an empty sample decides nothing",
          !language_guess::alreadyInLanguage("Whatever Spanish", {}, "spanish"));
    // A name naming the language still needs the text to back it up, or a mod
    // named for what it translates FROM would silence itself.
    check("the name alone does not silence a mod",
          !language_guess::alreadyInLanguage(
              "Spanish Voices Restored",
              {"Iron Sword", "Bandit Chief", "Guard"}, "spanish"));
}

} // namespace lg_test

// ---- lore_overrides ----
namespace lo_test {

static void testCanonicalNames()
{
    std::cout << "\n[a published name beats a machine guess]\n";
    // The case Scribe added the table for: Google renders this literally as
    // "escamas de sombra", translating the words and losing the name.
    check("Shadowscales has a canonical name",
          lore_overrides::lookup("Shadowscales", "spanish")
              == QString::fromUtf8("Escamas Sombrías"));
    check("lookup ignores case and whitespace",
          lore_overrides::lookup("  DARK BROTHERHOOD ", "spanish")
              == QString::fromUtf8("Hermandad Oscura"));

    // Identity entries protect a proper noun from being translated at all.
    check("a proper noun maps to itself",
          lore_overrides::lookup("Skooma", "spanish") == QStringLiteral("Skooma"));

    check("an unknown term has no entry",
          lore_overrides::lookup("Iron Sword", "spanish").isEmpty());
    check("a language with no table has no entry",
          lore_overrides::lookup("Shadowscales", "korean").isEmpty());
    check("empty in, empty out",
          lore_overrides::lookup(QString(), "spanish").isEmpty());

    // Whole-cell only: inside a sentence the machine translator is needed for
    // the surrounding grammar.
    check("a term inside a sentence is not matched",
          lore_overrides::lookup("Join the Dark Brotherhood today", "spanish")
              .isEmpty());

    check("the table has Spanish entries",
          !lore_overrides::termsFor("spanish").isEmpty());
}

} // namespace lo_test


// ---- mod_package ----
//
// The layout is the whole point: a mod archive holds the mod's files at its
// ROOT, not inside a folder named after the mod. A wrapper folder is what
// forces every installer downstream - this one included - to guess whether the
// top-level directory is part of the mod or packaging noise.
namespace mp_test {

static void testNaming()
{
    std::cout << "\n[archive naming]\n";
    check("keeps the mod name and adds a suffix",
          mod_package::suggestedFileName("Varuun DLC items - Spanish (Nerevarine)",
                                         mod_package::Format::Zip)
              == QStringLiteral("Varuun DLC items - Spanish (Nerevarine).zip"));
    check("7z gets its own suffix",
          mod_package::suggestedFileName("Foo", mod_package::Format::SevenZip)
              == QStringLiteral("Foo.7z"));
    // Mod names really do carry these, and the file has to survive being
    // downloaded onto Windows.
    check("path separators and Windows-hostile characters are replaced",
          !mod_package::suggestedFileName("JK's Whiterun/Outskirts: v2?",
                                          mod_package::Format::Zip)
               .contains(QLatin1Char('/')));
    check("spaces are kept - they are ordinary in a mod filename",
          mod_package::suggestedFileName("Better Crowd Citizens",
                                         mod_package::Format::Zip)
              == QStringLiteral("Better Crowd Citizens.zip"));
    check("an empty name still yields a usable filename",
          mod_package::suggestedFileName("   ", mod_package::Format::Zip)
              == QStringLiteral("mod.zip"));

    check("format follows the suffix",
          mod_package::formatForPath("/tmp/a.7z") == mod_package::Format::SevenZip
              && mod_package::formatForPath("/tmp/a.zip") == mod_package::Format::Zip);
    check("an unknown suffix defaults to zip, the safer one",
          mod_package::formatForPath("/tmp/a.tar") == mod_package::Format::Zip
              && mod_package::formatForPath("/tmp/a") == mod_package::Format::Zip);
}

static void testRefusesBadInput()
{
    std::cout << "\n[nothing is packaged from nothing]\n";
    QTemporaryDir d;
    check("a missing folder is refused",
          !mod_package::create(d.filePath("nope"), d.filePath("out.zip")).ok);
    check("an empty folder is refused",
          QDir().mkpath(d.filePath("empty"))
              && !mod_package::create(d.filePath("empty"), d.filePath("out.zip")).ok);
    check("no destination is refused",
          !mod_package::create(d.path(), QString()).ok);
    check("and no archive is left behind",
          !QFile::exists(d.filePath("out.zip")));
}

} // namespace mp_test


// ---- term_protect ----
//
// Forfeoranna Heim SSE is a dungeon mod. Handed its nine strings, Google
// translated the dungeon's name in two rows and kept it in three, so the map,
// the key and the door disagreed about what the place was called.
namespace tp_test {

// Verbatim from that mod.
static QStringList dungeonMod()
{
    return {"Ancient Nord Helmet of Health", "Chest", "Forfeoranna Heim",
            "Forfeoranna Heim Catacombs", "Forfeoranna Heim Depths",
            "Forfeoranna Heim Key", "Forfeoranna Heim Lair",
            "Greatsword of the Succubi", "Wardrobe"};
}

static void testFindsTheName()
{
    std::cout << "\n[the repeated name is the one to protect]\n";
    const QStringList names = term_protect::findNames(dungeonMod());
    check("exactly one name found", names.size() == 1,
          names.join(QLatin1Char('|')));
    check("and it is the whole phrase, not a word of it",
          !names.isEmpty() && names.first() == QStringLiteral("Forfeoranna Heim"),
          names.value(0));

    // Everything that appears once is left translatable - those are
    // descriptions, and Google renders them fine.
    check("a one-off phrase is not protected",
          !names.contains(QStringLiteral("Ancient Nord Helmet")));
    check("nor is a lone word", !names.contains(QStringLiteral("Chest")));
}

static void testOrdinaryWordsStayTranslatable()
{
    std::cout << "\n[a repeated description is not a name]\n";
    // "Key" repeats three times, but a phrase of ordinary English words is
    // describing a thing. Protecting it would leave "Llave" untranslated.
    check("repeated ordinary words are not protected",
          term_protect::findNames({"Iron Key", "Gold Key", "Steel Key"}).isEmpty());
    // One unusual word in the phrase makes it a name again.
    const auto dwemer = term_protect::findNames({"Dwemer Key", "Dwemer Chest"});
    check("an unusual word makes it a name",
          dwemer == QStringList{QStringLiteral("Dwemer")}, dwemer.join('|'));
    check("nothing repeats, nothing protected",
          term_protect::findNames({"Iron Sword", "Health Potion"}).isEmpty());
}

static void testMaskRoundTrip()
{
    std::cout << "\n[mask and unmask are inverses]\n";
    const QStringList names{QStringLiteral("Forfeoranna Heim")};
    const QString masked = term_protect::mask("Forfeoranna Heim Catacombs", names);
    check("the name is replaced by a token",
          masked == QStringLiteral("Nrvaa Catacombs"), masked);
    check("and comes back",
          term_protect::unmask(masked, names)
              == QStringLiteral("Forfeoranna Heim Catacombs"));

    // The token has to survive the translator MOVING it, which is the whole
    // reason it is word-shaped rather than bracketed.
    check("a reordered token still unmasks",
          term_protect::unmask("Catacumbas de Nrvaa", names)
              == QString::fromUtf8("Catacumbas de Forfeoranna Heim"));

    // Handed "Nrvaa" alone, Google returned "Nrvaá" - it gave a bare unknown
    // word Spanish orthography. Matching the token literally missed that and
    // left it in the finished translation.
    check("an accented token still unmasks",
          term_protect::unmask(QString::fromUtf8("Nrvaá"), names)
              == QStringLiteral("Forfeoranna Heim"));
    check("a case-changed token still unmasks",
          term_protect::unmask("NRVAA Catacumbas", names)
              == QStringLiteral("Forfeoranna Heim Catacumbas"));

    // Never a digit: measured live, a token carrying one is read as a code and
    // both the word order and the sense of "Key" suffer.
    check("tokens carry no digits",
          !term_protect::tokenFor(0).contains(QRegularExpression("\\d"))
              && !term_protect::tokenFor(31).contains(QRegularExpression("\\d")));
    check("tokens are distinct",
          term_protect::tokenFor(0) != term_protect::tokenFor(1)
              && term_protect::tokenFor(0) != term_protect::tokenFor(26));
}

static void testPureNameIsNeverSent()
{
    std::cout << "\n[a string that is only a name has nothing to translate]\n";
    const QStringList names{QStringLiteral("Forfeoranna Heim")};
    check("a bare name masks to nothing but a token",
          term_protect::isOnlyNames(
              term_protect::mask("Forfeoranna Heim", names), 1));
    check("punctuation around it does not change that",
          term_protect::isOnlyNames(
              term_protect::mask("Forfeoranna Heim!", names), 1));
    check("but a name plus a word does",
          !term_protect::isOnlyNames(
              term_protect::mask("Forfeoranna Heim Depths", names), 1));
    check("and an unprotected string does not",
          !term_protect::isOnlyNames(term_protect::mask("Chest", names), 1));
}

// Two names in one mod must not be confused with each other.
static void testSeveralNames()
{
    std::cout << "\n[several names at once]\n";
    const QStringList src{"Forfeoranna Heim Key", "Forfeoranna Heim Depths",
                          "Zanthar Blade", "Zanthar Shield"};
    const QStringList names = term_protect::findNames(src);
    check("both names found", names.size() == 2, names.join('|'));

    const QString masked = term_protect::mask("Forfeoranna Heim and Zanthar", names);
    check("each gets its own token",
          masked.count(QStringLiteral("Nrv")) == 2, masked);
    check("and each comes back to the right name",
          term_protect::unmask(masked, names)
              == QStringLiteral("Forfeoranna Heim and Zanthar"),
          term_protect::unmask(masked, names));
}

// The mechanic behind "edit the name once and every row follows": unmask is
// handed the CHOSEN RENDERING, not the original name. The masked answer is
// kept per row so it can be re-rendered whenever that choice changes.
static void testRenderingIsSubstituted()
{
    std::cout << "\n[a name's chosen translation is carried into every row]\n";
    const QStringList names{QStringLiteral("Forfeoranna Heim")};

    // What Google returned for each row, with the name masked out.
    const QStringList templates{
        QStringLiteral("Catacumbas de Nrvaa"),
        QStringLiteral("Profundidades de Nrvaa"),
        QStringLiteral("Llave Nrvaa"),
        QStringLiteral("Guarida de Nrvaa")};

    // The name's own translation, which Google gave for it alone.
    const QStringList chosen{QString::fromUtf8("Hogar de los precursores")};
    check("the chosen rendering replaces the token, not the English name",
          term_protect::unmask(templates[0], chosen)
              == QString::fromUtf8("Catacumbas de Hogar de los precursores"));
    check("every row uses the same rendering",
          term_protect::unmask(templates[2], chosen)
              == QString::fromUtf8("Llave Hogar de los precursores"));

    // The user shortens it; the same templates re-render.
    const QStringList edited{QStringLiteral("Hogar Precursor")};
    check("editing the name re-renders the other rows",
          term_protect::unmask(templates[1], edited)
              == QStringLiteral("Profundidades de Hogar Precursor")
          && term_protect::unmask(templates[3], edited)
              == QStringLiteral("Guarida de Hogar Precursor"));

    // Undecided falls back to the original rather than leaving a token on
    // screen.
    check("an undecided name falls back to itself",
          term_protect::unmask(templates[0], names)
              == QStringLiteral("Catacumbas de Forfeoranna Heim"));
}

} // namespace tp_test


// ---- translation_rules ----
//
// The knobs the user turns without a rebuild. "Chest" came back from the
// machine translator as "Pecho", the body part, where a Skyrim container is a
// "Cofre" - a rule that needs a recompile to fix is a rule that never gets
// fixed.
namespace tr_test {

static void testRoundTrip()
{
    std::cout << "\n[rules are read from a file the user can edit]\n";
    QTemporaryDir d;
    const QString path = d.filePath("rules.txt");
    {
        QFile f(path);
        (void)f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("# a comment\n"
                "\n"
                "[terms]\n"
                "Chest=Cofre\n"
                "Greatsword of the Succubi=Mandoble de las Succubi\n"
                "\n"
                "[protect]\n"
                "Forfeoranna Heim\n"
                "\n"
                "[ordinary]\n"
                "Blade\n"
                "\n"
                "[after]\n"
                "Gran espada=>Mandoble\n");
    }

    const auto r = translation_rules::load(path);
    check("terms load", r.terms.value("chest") == QStringLiteral("Cofre"));
    check("a multi-word term loads",
          r.terms.value("greatsword of the succubi")
              == QStringLiteral("Mandoble de las Succubi"));
    check("protect loads", r.protect == QStringList{QStringLiteral("Forfeoranna Heim")});
    check("ordinary is lowercased", r.ordinary.contains(QStringLiteral("blade")));
    check("after loads", r.after.size() == 1);
    check("after is applied",
          translation_rules::applyAfter("Gran espada de los succubi", r)
              == QStringLiteral("Mandoble de los succubi"));

    check("a missing file is an empty rule set, not an error",
          translation_rules::load(d.filePath("nope.txt")).isEmpty());
}

static void testTemplateAndProtection()
{
    std::cout << "\n[the template explains itself, and rules reach protection]\n";
    QTemporaryDir d;
    const QString path = d.filePath("new.txt");
    check("template written", translation_rules::ensureTemplate(path, "spanish"));
    check("and it exists", QFile::exists(path));
    const auto blank = translation_rules::load(path);
    check("a fresh template is all comments, so it changes nothing",
          blank.isEmpty());
    // Never clobber what the user wrote.
    {
        QFile f(path);
        (void)f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("[terms]\nChest=Cofre\n");
    }
    check("an existing file is left alone",
          translation_rules::ensureTemplate(path, "spanish")
              && translation_rules::load(path).terms.value("chest")
                     == QStringLiteral("Cofre"));

    // A name that appears only ONCE cannot be found by repetition; this is
    // the whole reason [protect] exists.
    const QStringList src{"Forfeoranna Heim Depths", "Iron Sword"};
    check("repetition alone misses a one-off name",
          !term_protect::findNames(src).contains(QStringLiteral("Forfeoranna Heim")));
    check("a user rule protects it anyway",
          term_protect::findNames(src, {QStringLiteral("Forfeoranna Heim")}, {})
              .contains(QStringLiteral("Forfeoranna Heim")));

    // And the opposite escape hatch: stop protection freezing something.
    const QStringList blades{"Dwemer Blade", "Dwemer Shield"};
    check("a repeated unusual word is protected by default",
          term_protect::findNames(blades).contains(QStringLiteral("Dwemer")));
    check("declaring it ordinary releases it",
          !term_protect::findNames(blades, {}, {QStringLiteral("dwemer")})
               .contains(QStringLiteral("Dwemer")));
}

} // namespace tr_test


// ---- translation_store + translation_mod ----
namespace tm_test {

using pr_test::writePlugin;
using ps_test::makeRecord;
using ps_test::makeStringPlugin;

// "Bandit Chief" is in dozens of mods. The memory is what stops the user
// retyping it in every one of them.
static void testMemoryRemembersAcrossMods()
{
    std::cout << "\n[a translation typed once is offered everywhere]\n";
    QTemporaryDir d;
    const QString path = d.filePath("tm.json");

    translation_store::Memory m;
    check("a missing file is an empty memory, not an error", m.load(path));
    check("and it is empty", m.empty());

    m.remember(QStringLiteral("Bandit Chief"), QString::fromUtf8("Líder Bandido"));
    check("saved", m.save(path));

    translation_store::Memory again;
    check("reloaded", again.load(path));
    check("the translation survived the round trip",
          again.lookup(QStringLiteral("Bandit Chief"))
              == QString::fromUtf8("Líder Bandido"));

    // The same words in another mod arrive spelled differently.
    check("lookup ignores case",
          again.lookup(QStringLiteral("BANDIT CHIEF"))
              == QString::fromUtf8("Líder Bandido"));
    check("lookup ignores surrounding whitespace",
          again.lookup(QStringLiteral("  Bandit Chief "))
              == QString::fromUtf8("Líder Bandido"));

    // Deliberately exact otherwise: a near-miss must not answer, because a
    // wrong translation written into a plugin is worse than an empty field.
    check("a longer string is NOT answered by a prefix match",
          again.lookup(QStringLiteral("Bandit Chief's Key")).isEmpty());
    check("an unknown string has no answer",
          again.lookup(QStringLiteral("Iron Sword")).isEmpty());
    check("empty in, empty out", again.lookup(QString()).isEmpty());
}

static void testClearingAnEntryForgetsIt()
{
    std::cout << "\n[clearing a field un-remembers it]\n";
    translation_store::Memory m;
    m.remember(QStringLiteral("Guard"), QStringLiteral("Guardia"));
    check("stored", m.size() == 1);
    m.remember(QStringLiteral("Guard"), QStringLiteral("   "));
    check("a blank translation removes the entry rather than storing a blank",
          m.empty());
}

// The output has to be a mod in its own right: same plugin filename as the
// original so it wins the file conflict, in its own folder so unticking it
// reverts and the download is never touched.
static void testTranslationModShape()
{
    std::cout << "\n[the translation is built as a separate mod]\n";
    QTemporaryDir d;
    const QString srcMod = d.filePath("Varuun");
    const QString modsDir = d.filePath("mods");
    check("dirs created", QDir().mkpath(srcMod) && QDir().mkpath(modsDir));

    QByteArray recs;
    recs.append(makeRecord("NPC_", 0x01000001, {{"FULL", "Bandit Chief"}}));
    const QString plugin = srcMod + "/Varuun.esm";
    QFile f(plugin);
    check("source plugin written", f.open(QIODevice::WriteOnly));
    f.write(makeStringPlugin(recs));
    f.close();

    translation_mod::ByPlugin repl;
    repl[QStringLiteral("Varuun.esm")].insert(
        QStringLiteral("NPC_:1000001:FULL:0"), QString::fromUtf8("Líder Bandido"));

    const auto r = translation_mod::build(srcMod, QStringLiteral("Varuun"),
                                          modsDir, QStringLiteral("spanish"), repl);
    check("build succeeds", r.ok, r.error);
    check("named for the source mod and language",
          r.modName == QStringLiteral("Varuun - Spanish (Nerevarine)"), r.modName);
    check("one plugin, one string", r.plugins == 1 && r.strings == 1);

    // Same filename is the entire mechanism: later in the load order wins the
    // file, exactly as a Nexus translation does.
    const QString out = r.modPath + "/Varuun.esm";
    check("the plugin keeps the original's filename", QFile::exists(out));
    check("the original was not modified",
          plugin_strings::extract(plugin).auxByKey.value(
              QStringLiteral("NPC_:1000001:FULL:0")) == QStringLiteral("Bandit Chief"));
    check("the copy carries the translation",
          plugin_strings::extract(out).auxByKey.value(
              QStringLiteral("NPC_:1000001:FULL:0")) == QString::fromUtf8("Líder Bandido"));

    // Re-running the editor must update in place, not pile up "(2)" folders.
    translation_mod::ByPlugin again;
    again[QStringLiteral("Varuun.esm")].insert(
        QStringLiteral("NPC_:1000001:FULL:0"), QString::fromUtf8("Jefe Bandido"));
    const auto r2 = translation_mod::build(srcMod, QStringLiteral("Varuun"),
                                           modsDir, QStringLiteral("spanish"), again);
    check("rebuilding reuses the same folder", r2.ok && r2.modPath == r.modPath);
    check("and overwrites the translation",
          plugin_strings::extract(out).auxByKey.value(
              QStringLiteral("NPC_:1000001:FULL:0")) == QString::fromUtf8("Jefe Bandido"));
    check("only one translation folder exists",
          QDir(modsDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size() == 1);
}

// A mod update can renumber a FormID. Writing the file anyway but saying
// nothing would leave the string silently English.
static void testStaleKeyIsReported()
{
    std::cout << "\n[a key the plugin no longer has is reported]\n";
    QTemporaryDir d;
    const QString srcMod = d.filePath("M"), modsDir = d.filePath("mods");
    QDir().mkpath(srcMod); QDir().mkpath(modsDir);
    QFile f(srcMod + "/M.esp");
    (void)f.open(QIODevice::WriteOnly);
    f.write(makeStringPlugin(makeRecord("WEAP", 0x01000001, {{"FULL", "Iron Sword"}})));
    f.close();

    translation_mod::ByPlugin repl;
    repl[QStringLiteral("M.esp")].insert(QStringLiteral("WEAP:1000001:FULL:0"),
                                         QStringLiteral("Espada"));
    repl[QStringLiteral("M.esp")].insert(QStringLiteral("WEAP:9999999:FULL:0"),
                                         QStringLiteral("Fantasma"));
    const auto r = translation_mod::build(srcMod, QStringLiteral("M"), modsDir,
                                          QStringLiteral("spanish"), repl);
    check("the real string still lands", r.ok && r.strings == 1, r.error);
    check("the stale one is warned about, not swallowed",
          r.warnings.size() == 1
              && r.warnings.first().contains(QStringLiteral("no longer exists")),
          r.warnings.join(QLatin1Char('|')));
}

static void testNothingToTranslateLeavesNoFolder()
{
    std::cout << "\n[a failed build leaves no empty folder behind]\n";
    QTemporaryDir d;
    const QString srcMod = d.filePath("M"), modsDir = d.filePath("mods");
    QDir().mkpath(srcMod); QDir().mkpath(modsDir);

    translation_mod::ByPlugin repl;
    repl[QStringLiteral("gone.esp")].insert(QStringLiteral("WEAP:1:FULL:0"),
                                            QStringLiteral("x"));
    const auto r = translation_mod::build(srcMod, QStringLiteral("M"), modsDir,
                                          QStringLiteral("spanish"), repl);
    check("build fails when no plugin could be written", !r.ok);
    check("and the mods dir is left clean",
          QDir(modsDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
}

// Scribe's database format, reused so years of its translations carry over.
static void testScribeDbImport()
{
    std::cout << "\n[a Nerevarine Scribe database imports into the memory]\n";
    QTemporaryDir d;
    const QString db = d.filePath("scribe.ini");
    {
        QFile f(db);
        (void)f.open(QIODevice::WriteOnly);
        f.write("# Nerevarine Scribe Translation Database\n"
                "# Format: source=text\n\n"
                "Bandit Chief=L\xc3\xad" "der Bandido\n"
                "Guard=Guardia\n"
                "A = B=Un = Dos\n"          // '=' in the value survives
                "Empty One=\n"              // blank translation skipped
                "=orphan\n");               // no source skipped
    }

    translation_store::Memory m;
    m.remember(QStringLiteral("Guard"), QStringLiteral("Vigilante"));  // refined here

    const auto r = m.importScribeDb(db);
    check("three usable entries read", r.read == 3, QString::number(r.read));
    check("two added (the refined one kept)", r.added == 2, QString::number(r.added));
    check("imported accents survive",
          m.lookup(QStringLiteral("Bandit Chief")) == QString::fromUtf8("Líder Bandido"));
    check("the memory's own entry wins",
          m.lookup(QStringLiteral("Guard")) == QStringLiteral("Vigilante"));
    check("'=' in the translation is kept",
          m.lookup(QStringLiteral("A")) == QStringLiteral("B=Un = Dos"));
    check("a missing file reports failure",
          m.importScribeDb(d.filePath("nope.ini")).read == -1);
}

} // namespace tm_test

static void run_plugin_writer()
{
    std::cout << "=== plugin_writer ===\n";
    pw_test::testEmptyReplacementIsByteIdentical();
    pw_test::testReplacementLandsAndSizesFollow();
    pw_test::testReplacementInsideCompressedRecord();
    pw_test::testLocalizedPluginIsRefused();
    pw_test::testUnknownKeyIsReportedNotSilentlyDropped();
    pw_test::testNonPluginIsRefused();
    std::cout << "\n";
}

static void run_tes3()
{
    std::cout << "=== TES3 (Morrowind) ===\n";
    t3_test::testExtractionTiers();
    t3_test::testCp1252ReadsAccents();
    t3_test::testWriterRoundTripAndPatch();
    t3_test::testWriterCannotTouchIdentity();
    std::cout << "\n";
}

static void run_target_language()
{
    std::cout << "=== target_language ===\n";
    tl_test::testLookups();
    tl_test::testTableIsWellFormed();
    tl_test::testResolution();
    tl_test::testLocaleGuess();
    std::cout << "\n";
}

static void run_google_translate()
{
    std::cout << "=== google_translate ===\n";
    gt_test::testParsesSegments();
    gt_test::testFailsSoft();
    gt_test::testRequestShape();
    std::cout << "\n";
}

static void run_text_and_language()
{
    std::cout << "=== plugin_text / language_guess / lore_overrides ===\n";
    pt_test::testEncodingDetection();
    pt_test::testRoundTrip();
    mp_test::testNaming();
    mp_test::testRefusesBadInput();
    tp_test::testFindsTheName();
    tp_test::testOrdinaryWordsStayTranslatable();
    tp_test::testMaskRoundTrip();
    tp_test::testPureNameIsNeverSent();
    tp_test::testSeveralNames();
    tp_test::testRenderingIsSubstituted();
    tr_test::testRoundTrip();
    tr_test::testTemplateAndProtection();
    lg_test::testSpotsAlreadyTranslated();
    lg_test::testEnglishIsNeverSilenced();
    lg_test::testMojibakedCyrillicIsNotSpanish();
    lg_test::testSilenceWhenUnsure();
    lo_test::testCanonicalNames();
    std::cout << "\n";
}

static void run_translation()
{
    std::cout << "=== translation store + mod ===\n";
    tm_test::testMemoryRemembersAcrossMods();
    tm_test::testClearingAnEntryForgetsIt();
    tm_test::testTranslationModShape();
    tm_test::testStaleKeyIsReported();
    tm_test::testNothingToTranslateLeavesNoFolder();
    tm_test::testScribeDbImport();
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
    run_target_language();
    run_google_translate();
    run_text_and_language();
    run_plugin_writer();
    run_tes3();
    run_translation();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
