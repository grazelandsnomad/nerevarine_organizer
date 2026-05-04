// tests/test_game_adapters.cpp
//
// Sanity checks for the GameAdapter registry.  Most per-game logic
// downstream (Steam locator, GOG locator, LOOT slug, pinned-menu order)
// trusts the registry's contract: stable non-empty ids, no duplicates,
// hasLauncher() reflecting the layout data, the pinned subset being a
// non-empty prefix etc.  Catching a typo or copy-paste mistake here is
// much cheaper than tracking down a "Skyrim never appears in the menu"
// regression in the GUI.
//
// Build + run:
//   cmake --build build && ./build/tests/test_game_adapters

#include "game_adapter.h"

#include <QSet>
#include <QString>
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

// Every adapter MUST have a stable id and a human display name.  An
// empty id would make profile lookup ambiguous; an empty display name
// would render as a blank menu entry.
static void testAllAdaptersHaveBasicIdentity()
{
    std::cout << "\n-- adapters: every entry has id + displayName --\n";
    for (const GameAdapter *a : GameAdapterRegistry::all()) {
        check("non-empty id",
              !a->id().isEmpty(),
              QStringLiteral("displayName=%1").arg(a->displayName()));
        check("non-empty displayName",
              !a->displayName().isEmpty(),
              QStringLiteral("id=%1").arg(a->id()));
    }
}

// Two adapters with the same id silently shadow each other; the second
// one becomes unreachable via find(id).  Keep ids unique.
static void testIdsAreUnique()
{
    std::cout << "\n-- adapters: ids are unique --\n";
    QSet<QString> seen;
    bool clean = true;
    for (const GameAdapter *a : GameAdapterRegistry::all()) {
        if (seen.contains(a->id())) {
            clean = false;
            std::cout << "    duplicate id: " << a->id().toStdString() << "\n";
        }
        seen.insert(a->id());
    }
    check("no duplicate ids", clean);
}

// find() must return the same pointer all() does for a known id, and
// nullptr for unknown.  This is the contract the launch dispatch relies
// on: adapter==nullptr means "unknown game, fall back to file picker".
static void testFindLookup()
{
    std::cout << "\n-- adapters: find() round-trips known and unknown ids --\n";
    const GameAdapter *m = GameAdapterRegistry::find("morrowind");
    check("find(morrowind) is non-null", m != nullptr);
    if (m) check("find(morrowind) returns the OpenMW adapter",
                 m->isMorrowind());

    check("find(\"\") is null",     GameAdapterRegistry::find("") == nullptr);
    check("find(unknown) is null",  GameAdapterRegistry::find("nope") == nullptr);
}

// hasLauncher()'s default impl says "true if any layout declares a
// launcher".  Verify a couple of known cases so a refactor to that
// helper doesn't silently flip the toolbar visibility.
static void testHasLauncherDerivedFromLayouts()
{
    std::cout << "\n-- adapters: hasLauncher() reflects layout data --\n";
    const auto *fnv = GameAdapterRegistry::find("falloutnewvegas");
    check("Fallout NV declares a launcher",  fnv && fnv->hasLauncher());

    const auto *flondon = GameAdapterRegistry::find("falloutlondon");
    // Total conversion overrides hasLauncher() to false even though its
    // borrowed Steam folder lists Fallout4Launcher.exe.
    check("Fallout London hides the launcher",
          flondon && !flondon->hasLauncher());

    const auto *cyber = GameAdapterRegistry::find("cyberpunk2077");
    // Has Steam + GOG layouts but neither declares a separate launcher.
    check("Cyberpunk 2077 has no launcher", cyber && !cyber->hasLauncher());
}

// Pinned subset feeds the toolbar's "switch game" top section.  At
// minimum, OpenMW (Morrowind) must be pinned -- it's the canonical
// game the app was built around and the first-run wizard creates it.
static void testPinnedContainsOpenMW()
{
    std::cout << "\n-- adapters: pinned() includes OpenMW (Morrowind) --\n";
    bool foundOpenMW = false;
    for (const GameAdapter *a : GameAdapterRegistry::pinned()) {
        if (a->isMorrowind()) { foundOpenMW = true; break; }
    }
    check("Morrowind is pinned", foundOpenMW);
}

// Builtin subset (first-run wizard chooser) must be a strict subset of
// all() and must include the canonical OpenMW entry.
static void testBuiltinIsSubsetOfAll()
{
    std::cout << "\n-- adapters: builtin() ⊆ all() --\n";
    QSet<QString> allIds;
    for (const GameAdapter *a : GameAdapterRegistry::all()) allIds.insert(a->id());

    bool subset = true;
    bool foundOpenMW = false;
    for (const GameAdapter *a : GameAdapterRegistry::builtin()) {
        if (!allIds.contains(a->id())) {
            subset = false;
            std::cout << "    builtin id not in all(): "
                      << a->id().toStdString() << "\n";
        }
        if (a->isMorrowind()) foundOpenMW = true;
    }
    check("every builtin id appears in all()", subset);
    check("builtin() includes Morrowind",      foundOpenMW);
}

int main()
{
    std::cout << "=== GameAdapter registry tests ===\n";

    testAllAdaptersHaveBasicIdentity();
    testIdsAreUnique();
    testFindLookup();
    testHasLauncherDerivedFromLayouts();
    testPinnedContainsOpenMW();
    testBuiltinIsSubsetOfAll();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
