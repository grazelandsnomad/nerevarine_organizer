// tests/test_fomod_scripts.cpp
//
// Regression tests for fomod_scripts::installDeclaredScripts.
//
// Why these exist:
//   Completionist Patch Hub (Nexus mod 58523) re-broke after the 0.3.1 fix.
//   The wizard now ticks every patch plugin by default (Pass D) and skips
//   materializing the empty scripts/ placeholder, but the user still ended
//   up with the .omwscripts manifests at the mod root and an empty
//   scripts/ directory next to them - because the FOMOD plugins only list
//   the manifest as a <file>, not the lua bodies.  The lua lives in the
//   per-plugin folder ("00 AJ/scripts/...") that the FOMOD never names.
//
//   These tests lock in: when an .omwscripts manifest is installed, every
//   path it declares is resolved against the archive AND copied into the
//   install dir at that path.  Without the rescue, OpenMW reads each
//   manifest, tries to load the declared lua, and bails when the file is
//   missing.

#include "fomod_scripts.h"

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

// The Completionist Patch Hub shape: per-plugin folder "00 AJ" containing
// both the manifest and the lua sibling.  The manifest has already been
// copied into installDir at the root by the wizard's main loop; this test
// covers the rescue step that pulls the lua across from the manifest's
// parent dir.
static void testRescueFromManifestParent()
{
    std::cout << "\n[lua lives next to manifest in per-plugin folder → rescued]\n";
    QTemporaryDir dir;
    const QString archive    = dir.filePath("archive");
    const QString installDir = dir.filePath("install");
    QDir().mkpath(installDir);

    const QString manifestSrc = archive + "/00 AJ/Completionist - AJ.omwscripts";
    writeFile(manifestSrc,
              "# header line\n"
              "PLAYER: scripts/Completionist - AJ/quests_AJ.lua\n");
    writeFile(archive + "/00 AJ/scripts/Completionist - AJ/quests_AJ.lua",
              "return {}");

    fomod_scripts::installDeclaredScripts(manifestSrc, archive, installDir);

    check("declared lua copied to install at the declared path",
          QFileInfo::exists(installDir + "/scripts/Completionist - AJ/quests_AJ.lua"));
}

static void testRescueFromArchiveRoot()
{
    std::cout << "\n[lua lives at archive root → also rescued]\n";
    QTemporaryDir dir;
    const QString archive    = dir.filePath("archive");
    const QString installDir = dir.filePath("install");
    QDir().mkpath(installDir);

    // Manifest is at archive root; the archive also has the lua at the
    // same relative path the manifest declares.  Mirrors the simpler shape
    // where everything sits at the archive root.
    const QString manifestSrc = archive + "/Completionist - AJ.omwscripts";
    writeFile(manifestSrc,
              "PLAYER: scripts/Completionist - AJ/quests_AJ.lua\n");
    writeFile(archive + "/scripts/Completionist - AJ/quests_AJ.lua",
              "return {}");

    fomod_scripts::installDeclaredScripts(manifestSrc, archive, installDir);

    check("declared lua copied via archive-root resolution",
          QFileInfo::exists(installDir + "/scripts/Completionist - AJ/quests_AJ.lua"));
}

static void testManyDeclarationsInOneManifest()
{
    std::cout << "\n[manifest with multiple declarations → all rescued]\n";
    QTemporaryDir dir;
    const QString archive    = dir.filePath("archive");
    const QString installDir = dir.filePath("install");
    QDir().mkpath(installDir);

    const QString manifestSrc = archive + "/00 BFM/Completionist - BFM.omwscripts";
    writeFile(manifestSrc,
              "PLAYER: scripts/Completionist - BFM/quests_BFM.lua\n"
              "GLOBAL: scripts/Completionist - BFM/global_BFM.lua\n");
    writeFile(archive + "/00 BFM/scripts/Completionist - BFM/quests_BFM.lua",
              "return {}");
    writeFile(archive + "/00 BFM/scripts/Completionist - BFM/global_BFM.lua",
              "return {}");

    fomod_scripts::installDeclaredScripts(manifestSrc, archive, installDir);

    check("first declaration installed",
          QFileInfo::exists(installDir + "/scripts/Completionist - BFM/quests_BFM.lua"));
    check("second declaration installed",
          QFileInfo::exists(installDir + "/scripts/Completionist - BFM/global_BFM.lua"));
}

static void testCommentsAndBlanksIgnored()
{
    std::cout << "\n[comments and blank lines do not crash the parser]\n";
    QTemporaryDir dir;
    const QString archive    = dir.filePath("archive");
    const QString installDir = dir.filePath("install");
    QDir().mkpath(installDir);

    const QString manifestSrc = archive + "/00 X/X.omwscripts";
    writeFile(manifestSrc,
              "# this is a comment\n"
              "\n"
              "PLAYER: scripts/X/x.lua\n"
              "\n"
              "# another comment\n");
    writeFile(archive + "/00 X/scripts/X/x.lua", "return {}");

    fomod_scripts::installDeclaredScripts(manifestSrc, archive, installDir);

    check("script behind comment lines installed",
          QFileInfo::exists(installDir + "/scripts/X/x.lua"));
}

static void testMissingDeclaredScriptIsSkipped()
{
    std::cout << "\n[manifest declares missing lua → skipped, no crash]\n";
    QTemporaryDir dir;
    const QString archive    = dir.filePath("archive");
    const QString installDir = dir.filePath("install");
    QDir().mkpath(installDir);

    const QString manifestSrc = archive + "/00 Z/Z.omwscripts";
    writeFile(manifestSrc, "PLAYER: scripts/Z/missing.lua\n");
    // Don't create the lua - the FOMOD declared a path that doesn't exist
    // in the archive.  That's the modder's bug; we should just no-op.

    fomod_scripts::installDeclaredScripts(manifestSrc, archive, installDir);

    check("nothing materialized for missing lua",
          !QFileInfo::exists(installDir + "/scripts"));
}

// Some plugin entries DO list the script folder explicitly (a well-formed
// patch hub), so the lua is already on disk by the time the manifest is
// processed.  The rescue must not error or overwrite - it's strictly an
// additive backstop, not a re-copy.
static void testAlreadyInstalledFileIsLeftAlone()
{
    std::cout << "\n[lua already at destination → kept as-is]\n";
    QTemporaryDir dir;
    const QString archive    = dir.filePath("archive");
    const QString installDir = dir.filePath("install");
    QDir().mkpath(installDir);

    const QString manifestSrc = archive + "/00 AJ/M.omwscripts";
    writeFile(manifestSrc, "PLAYER: scripts/M/m.lua\n");
    writeFile(archive + "/00 AJ/scripts/M/m.lua", "from-archive");
    // Pre-existing copy at the install dest, e.g. from a folder= entry.
    writeFile(installDir + "/scripts/M/m.lua", "from-folder-entry");

    fomod_scripts::installDeclaredScripts(manifestSrc, archive, installDir);

    QFile f(installDir + "/scripts/M/m.lua");
    QByteArray after;
    if (f.open(QIODevice::ReadOnly)) after = f.readAll();
    check("rescue did NOT overwrite the existing destination",
          after == "from-folder-entry");
}

// Backslash paths slip in when a Windows-authored manifest is read on Linux;
// the parser must normalize them so resolvePath can split on '/'.
static void testWindowsBackslashPathsResolve()
{
    std::cout << "\n[Windows-authored manifest with backslashes → resolves]\n";
    QTemporaryDir dir;
    const QString archive    = dir.filePath("archive");
    const QString installDir = dir.filePath("install");
    QDir().mkpath(installDir);

    const QString manifestSrc = archive + "/00 W/W.omwscripts";
    writeFile(manifestSrc, "PLAYER: scripts\\W\\w.lua\n");
    writeFile(archive + "/00 W/scripts/W/w.lua", "return {}");

    fomod_scripts::installDeclaredScripts(manifestSrc, archive, installDir);

    check("backslash path normalized and resolved",
          QFileInfo::exists(installDir + "/scripts/W/w.lua"));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::cout << "=== fomod_scripts::installDeclaredScripts ===\n";
    testRescueFromManifestParent();
    testRescueFromArchiveRoot();
    testManyDeclarationsInOneManifest();
    testCommentsAndBlanksIgnored();
    testMissingDeclaredScriptIsSkipped();
    testAlreadyInstalledFileIsLeftAlone();
    testWindowsBackslashPathsResolve();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
