// tests/test_plugin_collisions.cpp
//
// Golden-file tests for openmw::findPluginBasenameCollisions.
//
// Pins the Rocky_WG_Base_1.1.esp regression: the base mod, Caldera Priory's
// "01 Rocky West Gash Patch" subfolder, and "03 Rocky WG Aggressively
// Compatible Version Patch" all shipped an ESP of the same name.  VFS
// last-wins picked one silently and the user had no way to tell the other
// two were being shadowed.  The Inspector dialog now surfaces that shape.

#include "plugin_collisions.h"

#include <QCoreApplication>

#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok,
                  const QString &got = {}, const QString &want = {})
{
    if (ok) {
        std::cout << "  \033[32m\xE2\x9C\x93\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m\xE2\x9C\x97\033[0m " << name << "\n";
        if (!want.isEmpty() || !got.isEmpty()) {
            std::cout << "    --- want ---\n" << want.toStdString() << "\n";
            std::cout << "    ---  got ---\n" << got.toStdString()  << "\n";
        }
        ++s_failed;
    }
}

using openmw::CollisionInput;
using openmw::findPluginBasenameCollisions;

static CollisionInput mod(const QString &label,
                          const QString &dataRoot,
                          const QStringList &plugins)
{
    CollisionInput ci;
    ci.modLabel = label;
    ci.pluginDirs = {{dataRoot, plugins}};
    return ci;
}

// Empty input → empty report.
static void testEmpty()
{
    std::cout << "testEmpty\n";
    auto r = findPluginBasenameCollisions({});
    check("no collisions on empty input",
          r.collisions.isEmpty() && r.totalPluginsChecked == 0);
}

// Disjoint plugin sets → no collisions.
static void testDisjoint()
{
    std::cout << "testDisjoint\n";
    QList<CollisionInput> mods = {
        mod("A", "/mods/A", {"A.esp"}),
        mod("B", "/mods/B", {"B.esp"}),
    };
    auto r = findPluginBasenameCollisions(mods);
    check("two mods, two plugins, no overlap",
          r.collisions.isEmpty() && r.totalPluginsChecked == 2);
}

// The motivating case: Rocky_WG_Base_1.1.esp in three mods.
static void testRockyWGCollision()
{
    std::cout << "testRockyWGCollision\n";
    QList<CollisionInput> mods = {
        mod("Rocky West Gash",
            "/mods/Rocky_WG_Base",
            {"Rocky_WG_Base_1.1.esp"}),
        mod("Caldera Priory",
            "/mods/CalderaPriory/01 Rocky West Gash Patch",
            {"Rocky_WG_Base_1.1.esp"}),
        mod("Caldera Priory",
            "/mods/CalderaPriory/03 Rocky WG Aggressively Compatible",
            {"Rocky_WG_Base_1.1.esp"}),
    };
    auto r = findPluginBasenameCollisions(mods);
    check("one collision surfaced",
          r.collisions.size() == 1,
          QString::number(r.collisions.size()));
    if (r.collisions.isEmpty()) return;

    const auto &c = r.collisions.first();
    check("collision basename captured case-preserved",
          c.basename == "Rocky_WG_Base_1.1.esp");
    check("three providers recorded", c.providers.size() == 3,
          QString::number(c.providers.size()));

    // The triple case is specifically the shape the Caldera Priory FOMOD
    // produced - keep the assertion specific so regressions regress loudly.
    bool baseSeen = false, p01Seen = false, p03Seen = false;
    for (const auto &p : c.providers) {
        if (p.dataRoot.endsWith("Rocky_WG_Base"))                               baseSeen = true;
        if (p.dataRoot.endsWith("01 Rocky West Gash Patch"))                    p01Seen = true;
        if (p.dataRoot.endsWith("03 Rocky WG Aggressively Compatible"))         p03Seen = true;
    }
    check("base mod provider present",     baseSeen);
    check("01 patch provider present",     p01Seen);
    check("03 patch provider present",     p03Seen);
}

// Case-insensitive collapse: FOO.ESP and foo.esp are the same VFS entry on
// all platforms that matter, so the detector must collapse them.
static void testCaseInsensitive()
{
    std::cout << "testCaseInsensitive\n";
    QList<CollisionInput> mods = {
        mod("Upper", "/mods/U", {"FOO.ESP"}),
        mod("Lower", "/mods/L", {"foo.esp"}),
    };
    auto r = findPluginBasenameCollisions(mods);
    check("case-different names reported as a single collision",
          r.collisions.size() == 1);
    if (r.collisions.isEmpty()) return;
    // First-seen spelling wins for display - in this fixture that's "FOO.ESP".
    check("display basename is first-seen spelling",
          r.collisions.first().basename == "FOO.ESP",
          r.collisions.first().basename);
}

// One mod listing the same (mod, root) pair with the same basename twice
// (pathological input) must NOT self-collide - only >1 distinct provider
// counts.  Guards against the caller double-building the pluginDirs list.
static void testSameModSameRootNoSelfCollision()
{
    std::cout << "testSameModSameRootNoSelfCollision\n";
    CollisionInput ci;
    ci.modLabel = "Dup";
    ci.pluginDirs = {
        {"/mods/D", {"Dup.esp"}},
        {"/mods/D", {"Dup.esp"}},  // identical to above
    };
    auto r = findPluginBasenameCollisions({ci});
    check("identical (modLabel, dataRoot) pairs don't self-collide",
          r.collisions.isEmpty());
}

// Same mod, two DIFFERENT data roots, same basename - this IS a real
// within-mod FOMOD-extract bug and must be reported.
static void testSameModDifferentRootsIsReported()
{
    std::cout << "testSameModDifferentRootsIsReported\n";
    CollisionInput ci;
    ci.modLabel = "FomodMess";
    ci.pluginDirs = {
        {"/mods/F/01 Core",   {"Plugin.esp"}},
        {"/mods/F/02 Altern", {"Plugin.esp"}},
    };
    auto r = findPluginBasenameCollisions({ci});
    check("same mod, two roots, same basename → reported",
          r.collisions.size() == 1);
    if (!r.collisions.isEmpty()) {
        check("both data roots listed",
              r.collisions.first().providers.size() == 2);
    }
}

// Output sorted by basename (case-insensitive) so the Inspector shows
// collisions in a predictable order regardless of modlist order.
static void testDeterministicOrdering()
{
    std::cout << "testDeterministicOrdering\n";
    QList<CollisionInput> mods = {
        mod("M1", "/m1", {"Zebra.esp", "Alpha.esp"}),
        mod("M2", "/m2", {"Zebra.esp", "Alpha.esp"}),
    };
    auto r = findPluginBasenameCollisions(mods);
    check("two collisions", r.collisions.size() == 2);
    if (r.collisions.size() >= 2) {
        check("Alpha sorted before Zebra",
              r.collisions[0].basename.toLower() <
              r.collisions[1].basename.toLower(),
              r.collisions[0].basename + " / " + r.collisions[1].basename);
    }
}

// totalPluginsChecked reflects every basename seen - not filtered to
// collisions.  Lets the UI show "checked N plugins, found M collisions".
static void testTotalCountsNonCollidingToo()
{
    std::cout << "testTotalCountsNonCollidingToo\n";
    QList<CollisionInput> mods = {
        mod("A", "/a", {"Shared.esp", "A_only.esp"}),
        mod("B", "/b", {"Shared.esp", "B_only.esp"}),
    };
    auto r = findPluginBasenameCollisions(mods);
    check("total counts 4 plugin rows even though only 1 collides",
          r.totalPluginsChecked == 4,
          QString::number(r.totalPluginsChecked));
    check("exactly one collision", r.collisions.size() == 1);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testEmpty();
    testDisjoint();
    testRockyWGCollision();
    testCaseInsensitive();
    testSameModSameRootNoSelfCollision();
    testSameModDifferentRootsIsReported();
    testDeterministicOrdering();
    testTotalCountsNonCollidingToo();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
