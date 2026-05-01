// tests/test_fomod_copy.cpp
//
// Regression tests for fomod_copy::copyContents and fomod_copy::copyDir.
//
// Why these exist:
//   Completionist Patch Hub (Nexus mod 58523) shipped with all 17
//   `Completionist - <X>.omwscripts` files at the archive root and an
//   apparently-empty `scripts/` placeholder dir.  The wizard used to mkpath
//   the destination unconditionally, so a folder= entry pointing at an empty
//   source materialized as an empty `scripts/` next to the .omwscripts files
//   - indistinguishable from "the lua content failed to copy" and confusing
//   enough that we got user reports.  These tests lock in the
//   "empty source → no destination" rule that prevents the misleading state.

#include "fomod_copy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok, const QString &hint = {})
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

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(bytes);
    f.close();
}

// -- Scenarios ---

static void testEmptySourceProducesNoDest()
{
    std::cout << "\n[empty source dir → dest is NOT created]\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("src/scripts");
    const QString dst = dir.filePath("install/scripts");
    QDir().mkpath(src);  // exists, but empty

    fomod_copy::copyContents(src, dst);

    check("empty-source copyContents leaves no dest dir",
          !QFileInfo::exists(dst));

    fomod_copy::copyDir(src, dst);
    check("empty-source copyDir leaves no dest dir",
          !QFileInfo::exists(dst));
}

static void testNonexistentSourceIsNoop()
{
    std::cout << "\n[missing source dir → dest is NOT created]\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("nope");
    const QString dst = dir.filePath("install/should_not_appear");

    fomod_copy::copyContents(src, dst);
    fomod_copy::copyDir(src, dst);

    check("missing-source produces no dest", !QFileInfo::exists(dst));
}

static void testPopulatedSourceCopiesEverything()
{
    std::cout << "\n[populated source → recursive copy of all children]\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("src/00 AJ");
    const QString dst = dir.filePath("install");
    writeFile(src + "/Completionist - AJ.omwscripts",
              "PLAYER: scripts/Completionist - AJ/quests_AJ.lua");
    writeFile(src + "/scripts/Completionist - AJ/quests_AJ.lua",
              "return {}");

    fomod_copy::copyContents(src, dst);

    check("file at root of source is copied",
          QFileInfo::exists(dst + "/Completionist - AJ.omwscripts"));
    check("nested lua is copied through the subdir",
          QFileInfo::exists(dst + "/scripts/Completionist - AJ/quests_AJ.lua"));
}

// The Completionist Patch Hub failure mode in miniature: the FOMOD's
// requiredInstallFiles section had a folder= entry whose source was an empty
// placeholder dir at the archive root.  Pre-fix, copyContents called mkpath
// before checking children, so the mod ended up with a misleading empty
// `scripts/` next to the .omwscripts files - which then tried to load Lua
// scripts that were never actually installed.  Locking in the no-empty-dest
// rule alongside the "real" content keeps this from regressing.
static void testPatchHubMixedEmptyAndContent()
{
    std::cout << "\n[Patch-Hub-shaped install: empty placeholder + real content]\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("src");
    const QString dst = dir.filePath("install");

    // Empty placeholder dir - the Completionist Patch Hub shape.
    QDir().mkpath(src + "/scripts");
    // Real .omwscripts file at the archive root.
    writeFile(src + "/Completionist - AJ.omwscripts",
              "PLAYER: scripts/Completionist - AJ/quests_AJ.lua");

    fomod_copy::copyContents(src, dst);

    check("real .omwscripts file landed in install",
          QFileInfo::exists(dst + "/Completionist - AJ.omwscripts"));
    check("empty `scripts/` placeholder does NOT appear in install",
          !QFileInfo::exists(dst + "/scripts"));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::cout << "=== fomod_copy ===\n";
    testEmptySourceProducesNoDest();
    testNonexistentSourceIsNoop();
    testPopulatedSourceCopiesEverything();
    testPatchHubMixedEmptyAndContent();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
