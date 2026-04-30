// tests/test_load_order_merge.cpp
//
// Regression coverage for loadorder::mergeLoadOrder - the pure helper
// MainWindow::absorbExternalLoadOrder routes through when it detects that
// the OpenMW Launcher has rewritten openmw.cfg since we last saved our
// load order.
//
// Why these tests exist:
//   The exact symptom we're guarding against is the OAAB_Data.esm case:
//   user pulls OAAB_Data.esm (or Tamriel_Data.esm / Sky_Main.esm) to the
//   top of the load order inside the OpenMW Launcher, closes it, and
//   expects that order to stick.  Without the absorb step (or with a
//   broken merge), saveModList rewrites openmw.cfg from the stale
//   m_loadOrder and the reorder is silently lost.
//
//   The first test case in this file is exactly that scenario.  If that
//   test ever goes red, production users will start losing their
//   launcher reorders - do not relax or delete it without re-reading
//   feedback_absorb_external_order.md first.
//
// Build + run:
//   cmake --build build -j$(nproc) && ./build/tests/test_load_order_merge

#include "load_order_merge.h"

#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QStringList>

#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

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
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name << "\n";
        std::cout << "     expected " << fmt(expected).toStdString() << "\n";
        std::cout << "     got      " << fmt(got).toStdString() << "\n";
        ++s_failed;
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "=== load_order_merge tests ===\n";

    // -- The OAAB_Data.esm regression (the reason this module exists) -----
    //   User pulled OAAB_Data.esm to the top inside OpenMW Launcher;
    //   m_loadOrder still has it at the bottom.  After absorb, the
    //   merged order must honour the launcher's position.
    expect("OAAB_Data.esm pulled to top (core regression)",
           {"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm",
            "SomeMod.esp", "OAAB_Data.esm"},
           {"OAAB_Data.esm", "Morrowind.esm", "Tribunal.esm",
            "Bloodmoon.esm", "SomeMod.esp"},
           {"OAAB_Data.esm", "Morrowind.esm", "Tribunal.esm",
            "Bloodmoon.esm", "SomeMod.esp"});

    // -- Identity: cfg already matches prev (absorb should be a no-op) ----
    expect("identity - no reorder",
           {"A.esm", "B.esp", "C.esp"},
           {"A.esm", "B.esp", "C.esp"},
           {"A.esm", "B.esp", "C.esp"});

    // -- Straight swap of two managed plugins ---
    expect("two-plugin swap",
           {"A.esm", "B.esp"},
           {"B.esp", "A.esm"},
           {"B.esp", "A.esm"});

    // -- Disabled mod keeps its slot while managed plugins reorder ---
    //   X.esp is disabled so it's absent from openmw.cfg.  Its position
    //   among the managed entries must survive the merge.
    expect("disabled plugin preserved around reorder",
           {"A.esm", "X.esp", "B.esp", "C.esp"},
           {"C.esp", "A.esm", "B.esp"},
           {"C.esp", "X.esp", "A.esm", "B.esp"});

    // -- New plugin appears in cfg (launcher saw a file we didn't) ---
    //   Should append to the tail, preserving cfg's relative order.
    expect("new plugin from launcher appended",
           {"A.esm", "B.esp"},
           {"A.esm", "B.esp", "D.esp"},
           {"A.esm", "B.esp", "D.esp"});

    // -- Plugin disappears from cfg (launcher filtered it out) ---
    //   prev had X.esp but cfg doesn't.  Since X.esp isn't in cfgSet,
    //   it's treated as disabled-mod territory and kept at its slot.
    //   This matches how the real absorb is invoked: disabled mods in the
    //   modlist show up here.
    expect("cfg omits plugin from prev (treated as disabled)",
           {"A.esm", "X.esp", "B.esp"},
           {"A.esm", "B.esp"},
           {"A.esm", "X.esp", "B.esp"});

    // -- Both sides empty ---
    expect("empty prev, empty cfg",
           {}, {}, {});

    // -- Fresh install: prev is empty, cfg introduces all plugins ---
    expect("empty prev → everything from cfg",
           {},
           {"A.esm", "B.esp"},
           {"A.esm", "B.esp"});

    // -- prev has managed plugins, cfg is empty (openmw.cfg wiped) ---
    //   Returning prev unchanged is the right move: we've got nothing
    //   to merge in.  Crucially, we must NOT emit an empty list.
    expect("empty cfg → prev preserved",
           {"A.esm", "B.esp"},
           {},
           {"A.esm", "B.esp"});

    // -- Heavy reorder with new plugin AND disabled plugin ---
    expect("reorder + new plugin + disabled plugin",
           {"A.esm", "X.esp", "B.esp", "C.esp"},
           {"C.esp", "B.esp", "A.esm", "D.esp"},
           {"C.esp", "X.esp", "B.esp", "A.esm", "D.esp"});

    // -- Duplicate-entry sanity: merge must not emit duplicates ---
    //   If prev happens to contain the same plugin twice (shouldn't, but
    //   defensive) the output must still be unique.
    {
        QStringList got = loadorder::mergeLoadOrder(
            {"A.esm", "B.esp", "A.esm"},
            {"A.esm", "B.esp"});
        // We don't pin the exact order here - the contract is "no duplicate
        // in output".  Count occurrences of A.esm.
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

    // -- topologicallySortByMasters regression coverage ---
    //   This helper is what stops the Stargazer - Telescopes Cyrodiil crash
    //   from recurring: the reconcile pass sorts filenames alphabetically
    //   within a mod folder, which places child.omwaddon above
    //   parent.omwaddon when the child's stem has an extra " Suffix".
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
            std::cout << "  \033[32m✓\033[0m " << name << "\n";
            ++s_passed;
        } else {
            std::cout << "  \033[31m✗\033[0m " << name << "\n";
            std::cout << "     expected " << fmt(expected).toStdString() << "\n";
            std::cout << "     got      " << fmt(got).toStdString() << "\n";
            ++s_failed;
        }
    };

    std::cout << "\ntopologicallySortByMasters:\n";

    // The canonical Stargazer case.  No master relationship → identity.
    topoExpect("no masters declared → input order preserved",
               {"Stargazer.omwaddon",
                "Stargazer - Telescopes.omwaddon",
                "Stargazer - Telescopes Cyrodiil.omwaddon"},
               {},
               {"Stargazer.omwaddon",
                "Stargazer - Telescopes.omwaddon",
                "Stargazer - Telescopes Cyrodiil.omwaddon"});

    // The crash case: child lands above parent.  With the master edge
    // declared (Cyrodiil → Telescopes) the helper lifts Telescopes above
    // Cyrodiil.
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

    // Cross-mod: OAAB_Data.esm must land above anything declaring it as a
    // master, even when the dependent comes far earlier in the input.
    topoExpect("cross-mod master - dependent moved below",
               {"DependentA.esp", "DependentB.esp", "OAAB_Data.esm"},
               {
                 {"dependenta.esp", {"OAAB_Data.esm"}},
                 {"dependentb.esp", {"OAAB_Data.esm"}},
               },
               {"OAAB_Data.esm", "DependentA.esp", "DependentB.esp"});

    // Case-insensitive master matching - Nexus authors mix case freely.
    topoExpect("case-insensitive master match",
               {"dependent.esp", "BASE.ESM"},
               {
                 {"dependent.esp", {"base.esm"}},  // different case
               },
               {"BASE.ESM", "dependent.esp"});

    // Unknown master (parent not installed): silently ignored by this
    // helper - missing-master detection lives elsewhere.  The known
    // dependency chain still resolves.
    topoExpect("master absent from input → skipped, no crash",
               {"Child.esp", "RealParent.esm"},
               {
                 {"child.esp", {"MissingExternal.esm", "RealParent.esm"}},
               },
               {"RealParent.esm", "Child.esp"});

    // Two-plugin cycle (malformed, shouldn't happen in practice) must not
    // spin forever; both entries appear exactly once.
    topoExpect("cycle → terminates, all entries emitted once",
               {"A.esp", "B.esp"},
               {
                 {"a.esp", {"B.esp"}},
                 {"b.esp", {"A.esp"}},
               },
               {"B.esp", "A.esp"});  // DFS happens to emit this order -
                                     // the important invariant is no loop
                                     // and no duplicates.

    // Input order preserved when masters happen to be above deps already
    // (the common case after LOOT sort).  This is why the algorithm has
    // to be stable.
    topoExpect("already-sorted input → identity",
               {"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm", "MyMod.esp"},
               {
                 {"tribunal.esm",  {"Morrowind.esm"}},
                 {"bloodmoon.esm", {"Morrowind.esm"}},
                 {"mymod.esp",     {"Morrowind.esm", "Bloodmoon.esm"}},
               },
               {"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm", "MyMod.esp"});

    // Empty input → empty output (trivial but worth pinning).
    topoExpect("empty input → empty output", {}, {}, {});

    std::cout << "\n";
    std::cout << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
