// tests/test_asset_collisions.cpp
//
// Pure tests for openmw::findAssetCaseCollisions.
//
// Pins the "Player.lua / player.lua" regression: on Linux both files coexist
// on disk; OpenMW's VFS loads both and one silently shadows the other.  The
// Inspector now surfaces that shape.

#include "asset_collisions.h"

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

using openmw::AssetCaseInput;
using openmw::findAssetCaseCollisions;

// Empty input → empty report.
static void testEmpty()
{
    std::cout << "testEmpty\n";
    auto r = findAssetCaseCollisions({});
    check("no hits on empty input",
          r.mods.isEmpty() && r.totalFilesChecked == 0);
}

// All paths distinct even case-insensitively → no hits.
static void testNoCollision()
{
    std::cout << "testNoCollision\n";
    AssetCaseInput in;
    in.modLabel = "Clean";
    in.dataRoot = "/mods/Clean";
    in.relPaths = {"Scripts/Player.lua", "Meshes/Actor.nif", "Textures/Actor.dds"};
    auto r = findAssetCaseCollisions({in});
    check("disjoint paths produce no hits", r.mods.isEmpty());
    check("total files counted", r.totalFilesChecked == 3,
          QString::number(r.totalFilesChecked), "3");
}

// The motivating case: Player.lua + player.lua in the same data root.
static void testLuaCaseCollision()
{
    std::cout << "testLuaCaseCollision\n";
    AssetCaseInput in;
    in.modLabel = "SomeMod";
    in.dataRoot = "/mods/SomeMod/Data Files";
    in.relPaths = {"Scripts/Player.lua", "Scripts/player.lua", "Meshes/actor.nif"};
    auto r = findAssetCaseCollisions({in});
    check("one mod affected", r.mods.size() == 1,
          QString::number(r.mods.size()), "1");
    check("total files counted", r.totalFilesChecked == 3,
          QString::number(r.totalFilesChecked), "3");
    if (r.mods.isEmpty()) return;
    const auto &m = r.mods.first();
    check("modLabel preserved", m.modLabel == "SomeMod");
    check("dataRoot preserved", m.dataRoot == "/mods/SomeMod/Data Files");
    check("one hit", m.hits.size() == 1,
          QString::number(m.hits.size()), "1");
    if (m.hits.isEmpty()) return;
    const auto &h = m.hits.first();
    check("lowercased key correct", h.lowercasedRel == "scripts/player.lua",
          h.lowercasedRel, "scripts/player.lua");
    check("both spellings present", h.spellings.size() == 2,
          QString::number(h.spellings.size()), "2");
    check("spellings contain Player.lua", h.spellings.contains("Scripts/Player.lua"));
    check("spellings contain player.lua", h.spellings.contains("Scripts/player.lua"));
}

// Exact duplicate paths (same spelling twice) must NOT self-collide.
static void testExactDuplicateNoSelfCollision()
{
    std::cout << "testExactDuplicateNoSelfCollision\n";
    AssetCaseInput in;
    in.modLabel = "Dup";
    in.dataRoot = "/mods/Dup";
    in.relPaths = {"Scripts/Foo.lua", "Scripts/Foo.lua"}; // same exact path twice
    auto r = findAssetCaseCollisions({in});
    check("exact duplicate does not self-collide", r.mods.isEmpty());
}

// Two separate data roots for the same mod - each root is an independent
// scan; cross-root comparison is out of scope for this detector.
static void testTwoInputsIndependent()
{
    std::cout << "testTwoInputsIndependent\n";
    AssetCaseInput a;
    a.modLabel = "ModA";
    a.dataRoot = "/mods/A/root1";
    a.relPaths = {"Scripts/Player.lua"};

    AssetCaseInput b;
    b.modLabel = "ModA";
    b.dataRoot = "/mods/A/root2";
    b.relPaths = {"Scripts/player.lua"};  // different root - not a within-root collision

    auto r = findAssetCaseCollisions({a, b});
    check("cross-root is not flagged", r.mods.isEmpty());
    check("both files counted", r.totalFilesChecked == 2,
          QString::number(r.totalFilesChecked), "2");
}

// Multiple hits in the same data root.
static void testMultipleHitsInOneRoot()
{
    std::cout << "testMultipleHitsInOneRoot\n";
    AssetCaseInput in;
    in.modLabel = "Multi";
    in.dataRoot = "/mods/Multi";
    in.relPaths = {
        "Scripts/Player.lua", "Scripts/player.lua",       // collision 1
        "Textures/Icon.dds",  "Textures/icon.dds",        // collision 2
        "Meshes/Actor.nif",                                // no collision
    };
    auto r = findAssetCaseCollisions({in});
    check("one mod in report", r.mods.size() == 1);
    check("total files counted", r.totalFilesChecked == 5,
          QString::number(r.totalFilesChecked), "5");
    if (r.mods.isEmpty()) return;
    check("two hits found", r.mods.first().hits.size() == 2,
          QString::number(r.mods.first().hits.size()), "2");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testEmpty();
    testNoCollision();
    testLuaCaseCollision();
    testExactDuplicateNoSelfCollision();
    testTwoInputsIndependent();
    testMultipleHitsInOneRoot();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
