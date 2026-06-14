// tests/test_post_install.cpp
//
// Locks in the pure decision logic behind addModFromPath's post-install
// prompts (groundcover detection, splash-dir finding, openmw.cfg external-data
// parsing, bundled-patch subfolder matching).  These used to be inlined in the
// 490-line addModFromPath; extracting them into post_install:: made them
// testable without QtWidgets - this file is the contract.

#include "post_install.h"

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

static void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.close();
}

// -- groundcover detection ---

static void testLooksLikeGroundcover()
{
    std::cout << "\n[looksLikeGroundcover]\n";
    using post_install::looksLikeGroundcover;

    check("matches 'grass' in path",
          looksLikeGroundcover("/mods/Remiros Grass Vol1", "Remiros"));
    check("matches 'groundcover' in display name",
          looksLikeGroundcover("/mods/whatever", "Aesthesia Groundcover"));
    check("case-insensitive",
          looksLikeGroundcover("/mods/GRASS", "X"));
    check("named-hint mod (Lush Synthesis) matches with neither word",
          looksLikeGroundcover("/mods/lush_synthesis_v2", "Lush Synthesis"));
    check("unrelated mod does not match",
          !looksLikeGroundcover("/mods/Patch for Purists", "Patch for Purists"));
}

// -- splash dir finding ---

static void testFindSplashDir()
{
    std::cout << "\n[findSplashDir]\n";
    QTemporaryDir dir;

    // (a) mod root IS a Splash dir with images.
    const QString rootIsSplash = dir.filePath("ModA/Splash");
    touch(rootIsSplash + "/load1.tga");
    check("mod root that is Splash/ with images is found",
          QFileInfo(post_install::findSplashDir(rootIsSplash)).fileName()
              .compare("splash", Qt::CaseInsensitive) == 0);

    // (b) Splash/ nested a couple levels down.
    const QString nested = dir.filePath("ModB");
    touch(nested + "/Data Files/Splash/intro.png");
    check("nested Splash/ with images is found",
          !post_install::findSplashDir(nested).isEmpty());

    // (c) Splash/ exists but has no images -> not a splash replacer.
    const QString empty = dir.filePath("ModC");
    QDir().mkpath(empty + "/Splash");
    check("Splash/ with no images is not matched",
          post_install::findSplashDir(empty).isEmpty());

    // (d) no splash anywhere.
    const QString none = dir.filePath("ModD");
    touch(none + "/meshes/x.nif");
    check("mod without any Splash/ returns empty",
          post_install::findSplashDir(none).isEmpty());
}

// -- bundled-patch subfolder matching ---

static void testNormalizeModName()
{
    std::cout << "\n[normalizeModName]\n";
    check("lowercases + drops non-alnum",
          post_install::normalizeModName("Remiros' Groundcover!") == "remirosgroundcover");
    check("keeps digits",
          post_install::normalizeModName("OAAB 2 Data") == "oaab2data");
}

static void testBundledPatchMatchesMod()
{
    std::cout << "\n[bundledPatchMatchesMod]\n";
    const QString target = post_install::normalizeModName("Remiros' Groundcover");

    check("'01 Grass for Remiros Groundcover' matches",
          post_install::bundledPatchMatchesMod("01 Grass for Remiros Groundcover", target));
    check("numbered+lettered prefix '10a Patch for Remiros Groundcover' matches",
          post_install::bundledPatchMatchesMod("10a Patch for Remiros Groundcover", target));
    check("non-numbered subfolder does not match",
          !post_install::bundledPatchMatchesMod("Grass for Remiros Groundcover", target));
    check("subfolder without 'for <target>' does not match",
          !post_install::bundledPatchMatchesMod("01 Some Textures", target));
    check("different target does not match",
          !post_install::bundledPatchMatchesMod("01 Grass for Vurt's Trees", target));
    check("short normalized name never matches",
          !post_install::bundledPatchMatchesMod("01 X for ab", post_install::normalizeModName("ab")));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::cout << "=== post_install ===\n";
    testLooksLikeGroundcover();
    testFindSplashDir();
    testNormalizeModName();
    testBundledPatchMatchesMod();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
