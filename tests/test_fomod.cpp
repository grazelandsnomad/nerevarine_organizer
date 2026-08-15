// FOMOD/BAIN path resolution, copy, script rescue, install promote, bain, wizard UI.
// Wizard tests need a QApplication (offscreen QPA, see main).

#include "fomod_path.h"
#include "fomod_copy.h"
#include "fomod_hint.h"
#include "fomod_scripts.h"
#include "fomod_install.h"
#include "bain.h"
#include "fomodwizard.h"

#include <QApplication>
#include <QCoreApplication>
#include <QAbstractButton>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRadioButton>
#include <QSet>
#include <QString>
#include <QTemporaryDir>
#include <QWidget>

#include <iostream>
#include <type_traits>

#include "test_harness.h"

static void writeFile(const QString &path, const QByteArray &bytes = {})
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(bytes);
    f.close();
}

// OAAB-shaped archive tree.
static void fomodpath_seedArchive(const QString &root)
{
    QDir().mkpath(root + "/00 Core/bookart");
    QDir().mkpath(root + "/00 Core/meshes");
    QDir().mkpath(root + "/00 Core/Textures");
    QDir().mkpath(root + "/01 Epic Plants Patch/Meshes");
    QDir().mkpath(root + "/fomod");
    auto touch = [](const QString &p) {
        QFile f(p);
        (void)f.open(QIODevice::WriteOnly);
        f.close();
    };
    touch(root + "/00 Core/OAAB_Data.esm");
    touch(root + "/00 Core/bookart/tome.dds");
    touch(root + "/fomod/ModuleConfig.xml");
}

// The gate itself, checked at compile time: a raw QString cannot become a
// ResolvedPath (so only resolveDest can mint one), and a ResolvedPath cannot be
// copied (so it can't be stashed and reused stale). Loosen the ctor or the
// move-only-ness and the build breaks here.
static_assert(!std::is_constructible_v<fomod::ResolvedPath, QString>,
              "ResolvedPath must not be constructible from a raw QString");
static_assert(!std::is_copy_constructible_v<fomod::ResolvedPath>,
              "ResolvedPath must stay move-only");
static_assert(std::is_move_constructible_v<fomod::ResolvedPath>,
              "resolveDest returns ResolvedPath by value, so it must be movable");

static void run_fomod_path()
{
    std::cout << "=== fomod_path tests ===\n";

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::cout << "  \033[31m✗\033[0m could not create temp dir\n";
        ++s_failed;
        return;
    }
    const QString root = tmp.path();
    fomodpath_seedArchive(root);

    auto expectResolves = [&](const char *name, const QString &rel, const QString &expectedSuffix) {
        QString got = fomod::resolvePath(root, rel);
        bool ok = !got.isEmpty()
               && QFileInfo::exists(got)
               && got.endsWith(expectedSuffix);
        check(name, ok, got);
    };
    auto expectEmpty = [&](const char *name, const QString &rel) {
        QString got = fomod::resolvePath(root, rel);
        check(name, got.isEmpty(), got);
    };

    // exact matches
    expectResolves("exact file",
                   "00 Core/OAAB_Data.esm",
                   "00 Core/OAAB_Data.esm");
    expectResolves("exact folder",
                   "00 Core/bookart",
                   "00 Core/bookart");

    // Windows backslashes must normalize (OAAB bug)
    expectResolves("backslash path (OAAB bug)",
                   "00 Core\\OAAB_Data.esm",
                   "00 Core/OAAB_Data.esm");
    expectResolves("mixed separators",
                   "00 Core\\bookart/tome.dds",
                   "00 Core/bookart/tome.dds");

    // each segment matched case-insensitively
    expectResolves("case-insensitive folder (Meshes ↔ meshes)",
                   "00 Core\\Meshes",
                   "00 Core/meshes");
    expectResolves("case-insensitive leaf (textures ↔ Textures)",
                   "00 Core/textures",
                   "00 Core/Textures");
    expectResolves("case-insensitive every segment",
                   "00 CORE\\BOOKART\\TOME.DDS",
                   "00 Core/bookart/tome.dds");

    {
        QString got = fomod::resolvePath(root, "");
        check("empty relative path returns root", got == root, got);
    }

    // a missing segment returns empty, not the parent dir
    expectEmpty("missing leaf",
                "00 Core/NOT_THERE.esm");
    expectEmpty("missing intermediate folder",
                "99 Missing Folder/OAAB_Data.esm");
    expectEmpty("backslashed missing path",
                "99 Missing\\thing.esp");

    // FOMOD paths are untrusted; refuse ../ traversal
    expectEmpty("resolvePath rejects ../ escape",
                "00 Core/../../../etc/passwd");
    expectEmpty("resolvePath rejects backslash ../ escape",
                "..\\..\\outside.esp");
    {
        QString got = fomod::resolveDest(root, "meshes/../../escape.nif");
        check("resolveDest rejects ../ escape", got.isEmpty(), got);
    }
    {
        QString got = fomod::resolveDest(root, "00 Core/./meshes");
        check("resolveDest skips harmless ./ segment",
              got == root + "/00 Core/meshes", got);
    }

    // resolveDest is the writable-path side
    {
        QString got = fomod::resolveDest(root, "00 Core\\Meshes");
        check("resolveDest reuses existing folder casing",
              got == root + "/00 Core/meshes", got);
    }
    {
        QString got = fomod::resolveDest(root, "00 Core/NewArt/Thing.dds");
        check("resolveDest keeps authored casing for new components",
              got == root + "/00 Core/NewArt/Thing.dds", got);
    }
    {
        QString got = fomod::resolveDest(root, "");
        check("resolveDest empty path returns root", got == root, got);
    }

    std::cout << "\n";
}

static void fomodcopy_testEmptySourceProducesNoDest()
{
    std::cout << "\n[empty source dir → dest is NOT created]\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("src/scripts");
    const QString dst = dir.filePath("install/scripts");
    QDir().mkpath(src);  // empty

    fomod_copy::copyContents(src, dst);

    check("empty-source copyContents leaves no dest dir",
          !QFileInfo::exists(dst));

    fomod_copy::copyDir(src, dst);
    check("empty-source copyDir leaves no dest dir",
          !QFileInfo::exists(dst));
}

static void fomodcopy_testNonexistentSourceIsNoop()
{
    std::cout << "\n[missing source dir → dest is NOT created]\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("nope");
    const QString dst = dir.filePath("install/should_not_appear");

    fomod_copy::copyContents(src, dst);
    fomod_copy::copyDir(src, dst);

    check("missing-source produces no dest", !QFileInfo::exists(dst));
}

static void fomodcopy_testPopulatedSourceCopiesEverything()
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

static void fomodcopy_testPatchHubMixedEmptyAndContent()
{
    std::cout << "\n[Patch-Hub-shaped install: empty placeholder + real content]\n";
    QTemporaryDir dir;
    const QString src = dir.filePath("src");
    const QString dst = dir.filePath("install");

    QDir().mkpath(src + "/scripts");
    writeFile(src + "/Completionist - AJ.omwscripts",
              "PLAYER: scripts/Completionist - AJ/quests_AJ.lua");

    fomod_copy::copyContents(src, dst);

    check("real .omwscripts file landed in install",
          QFileInfo::exists(dst + "/Completionist - AJ.omwscripts"));
    check("empty `scripts/` placeholder does NOT appear in install",
          !QFileInfo::exists(dst + "/scripts"));
}

static void fomodcopy_testCaseVariantFoldersMerge()
{
    std::cout << "\n[case-variant folders merge into one]\n";
    QTemporaryDir dir;
    const QString dst = dir.filePath("install");

    const QString opt1 = dir.filePath("opt1");
    writeFile(opt1 + "/Meshes/x.nif", "base");
    writeFile(opt1 + "/Meshes/keep.nif", "keep");
    fomod_copy::copyContents(opt1, dst);

    const QString opt2 = dir.filePath("opt2");
    writeFile(opt2 + "/meshes/x.nif", "patch");
    writeFile(opt2 + "/meshes/y.nif", "new");
    fomod_copy::copyContents(opt2, dst);

    int meshDirs = 0;
    for (const QString &e : QDir(dst).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (e.compare("meshes", Qt::CaseInsensitive) == 0) ++meshDirs;
    check("no duplicate case-variant folder (one meshes dir)", meshDirs == 1,
          QString("found %1").arg(meshDirs));

    check("merged folder keeps first-seen casing",
          QFileInfo::exists(dst + "/Meshes"));

    check("base-only file survives the merge",
          QFileInfo::exists(dst + "/Meshes/keep.nif"));
    check("patch's new file joins the merged folder",
          QFileInfo::exists(dst + "/Meshes/y.nif"));

    QByteArray got;
    QFile f(dst + "/Meshes/x.nif");
    if (f.open(QIODevice::ReadOnly)) { got = f.readAll(); f.close(); }
    check("later option overwrites earlier file (last writer wins)",
          got == "patch", QString::fromUtf8(got));
}

static void fomodcopy_testMergeOverlayOverridesMainDownload()
{
    std::cout << "\n[merge overlay: optional download overrides the main download]\n";
    QTemporaryDir dir;
    const QString existing = dir.filePath("mods/OAAB_Data");

    writeFile(existing + "/OAAB_Data.esm", "main-plugin");
    writeFile(existing + "/Textures/shared.dds", "main-texture");
    writeFile(existing + "/Meshes/keep.nif", "main-only-mesh");

    const QString optional = dir.filePath("extract/OAAB_optional");
    writeFile(optional + "/Textures/shared.dds", "optional-texture");  // collides
    writeFile(optional + "/Textures/extra.dds",  "optional-extra");    // new

    fomod_copy::copyContents(optional, existing);

    auto readAll = [](const QString &p) {
        QByteArray b; QFile f(p);
        if (f.open(QIODevice::ReadOnly)) { b = f.readAll(); f.close(); }
        return b;
    };

    check("main download's untouched plugin survives the merge",
          readAll(existing + "/OAAB_Data.esm") == "main-plugin");
    check("main-only mesh survives the merge",
          readAll(existing + "/Meshes/keep.nif") == "main-only-mesh");
    check("optional overrides the colliding main file (last writer wins)",
          readAll(existing + "/Textures/shared.dds") == "optional-texture",
          QString::fromUtf8(readAll(existing + "/Textures/shared.dds")));
    check("optional's new file joins the existing folder",
          readAll(existing + "/Textures/extra.dds") == "optional-extra");
}

static void fomodcopy_testCopyFileLastWriterWinsAndReports()
{
    std::cout << "\n[copyFile: mkpath + last-writer-wins + bool result]\n";
    QTemporaryDir dir;
    const QString root = dir.path();
    writeFile(root + "/src_a.txt", "AAA");
    writeFile(root + "/src_b.txt", "BBB");

    // First write into a not-yet-existing nested dest: the parent is created.
    const bool ok1 = fomod_copy::copyFile(
        root + "/src_a.txt", fomod::resolveDest(root + "/out", "sub/f.txt"));
    check("copyFile creates the parent and returns true", ok1);
    check("copyFile placed the first file",
          QFileInfo::exists(root + "/out/sub/f.txt"));

    // Second write to the same dest overwrites (plain QFile::copy would refuse).
    const bool ok2 = fomod_copy::copyFile(
        root + "/src_b.txt", fomod::resolveDest(root + "/out", "sub/f.txt"));
    QFile out(root + "/out/sub/f.txt");
    (void)out.open(QIODevice::ReadOnly);
    const QByteArray got = out.readAll();
    out.close();
    check("copyFile overwrote (last writer wins)", ok2 && got == "BBB",
          "got: " + QString::fromUtf8(got));

    // Missing source -> QFile::copy fails -> false.
    const bool ok3 = fomod_copy::copyFile(
        root + "/nope.txt", fomod::resolveDest(root + "/out", "g.txt"));
    check("copyFile returns false when the source is missing", !ok3);
}

static void run_fomod_copy()
{
    std::cout << "=== fomod_copy ===\n";
    fomodcopy_testEmptySourceProducesNoDest();
    fomodcopy_testNonexistentSourceIsNoop();
    fomodcopy_testPopulatedSourceCopiesEverything();
    fomodcopy_testPatchHubMixedEmptyAndContent();
    fomodcopy_testCaseVariantFoldersMerge();
    fomodcopy_testMergeOverlayOverridesMainDownload();
    fomodcopy_testCopyFileLastWriterWinsAndReports();
    std::cout << "\n";
}

static void fomodscripts_testRescueFromManifestParent()
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

static void fomodscripts_testRescueFromArchiveRoot()
{
    std::cout << "\n[lua lives at archive root → also rescued]\n";
    QTemporaryDir dir;
    const QString archive    = dir.filePath("archive");
    const QString installDir = dir.filePath("install");
    QDir().mkpath(installDir);

    const QString manifestSrc = archive + "/Completionist - AJ.omwscripts";
    writeFile(manifestSrc,
              "PLAYER: scripts/Completionist - AJ/quests_AJ.lua\n");
    writeFile(archive + "/scripts/Completionist - AJ/quests_AJ.lua",
              "return {}");

    fomod_scripts::installDeclaredScripts(manifestSrc, archive, installDir);

    check("declared lua copied via archive-root resolution",
          QFileInfo::exists(installDir + "/scripts/Completionist - AJ/quests_AJ.lua"));
}

static void fomodscripts_testManyDeclarationsInOneManifest()
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

static void fomodscripts_testCommentsAndBlanksIgnored()
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

static void fomodscripts_testMissingDeclaredScriptIsSkipped()
{
    std::cout << "\n[manifest declares missing lua → skipped, no crash]\n";
    QTemporaryDir dir;
    const QString archive    = dir.filePath("archive");
    const QString installDir = dir.filePath("install");
    QDir().mkpath(installDir);

    const QString manifestSrc = archive + "/00 Z/Z.omwscripts";
    writeFile(manifestSrc, "PLAYER: scripts/Z/missing.lua\n");

    fomod_scripts::installDeclaredScripts(manifestSrc, archive, installDir);

    check("nothing materialized for missing lua",
          !QFileInfo::exists(installDir + "/scripts"));
}

static void fomodscripts_testAlreadyInstalledFileIsLeftAlone()
{
    std::cout << "\n[lua already at destination → kept as-is]\n";
    QTemporaryDir dir;
    const QString archive    = dir.filePath("archive");
    const QString installDir = dir.filePath("install");
    QDir().mkpath(installDir);

    const QString manifestSrc = archive + "/00 AJ/M.omwscripts";
    writeFile(manifestSrc, "PLAYER: scripts/M/m.lua\n");
    writeFile(archive + "/00 AJ/scripts/M/m.lua", "from-archive");
    writeFile(installDir + "/scripts/M/m.lua", "from-folder-entry");

    fomod_scripts::installDeclaredScripts(manifestSrc, archive, installDir);

    QFile f(installDir + "/scripts/M/m.lua");
    QByteArray after;
    if (f.open(QIODevice::ReadOnly)) after = f.readAll();
    check("rescue did NOT overwrite the existing destination",
          after == "from-folder-entry");
}

static void fomodscripts_testWindowsBackslashPathsResolve()
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

static void fomodscripts_testDeclaredScriptMergesIntoStagedCase()
{
    std::cout << "\n[declared script routes through resolveDest, merges case]\n";
    // A folder= entry already staged a lowercase "scripts/"; the manifest then
    // declares the same dir miscased ("Scripts\foo.lua"). resolveDest must merge
    // into the existing casing - the old installDir + "/" + scriptPath concat
    // this replaced would fork a case-variant "Scripts/" on a case-sensitive FS.
    QTemporaryDir dir;
    const QString archive    = dir.filePath("archive");
    const QString installDir = dir.filePath("install");
    QDir().mkpath(installDir + "/scripts");        // already staged, lowercase

    const QString manifestSrc = archive + "/00 W/W.omwscripts";
    writeFile(manifestSrc, "PLAYER: Scripts\\foo.lua\n");
    writeFile(archive + "/00 W/Scripts/foo.lua", "return {}");

    fomod_scripts::installDeclaredScripts(manifestSrc, archive, installDir);

    check("declared script merges into the staged lowercase scripts/",
          QFileInfo::exists(installDir + "/scripts/foo.lua"));
    check("no forked case-variant Scripts/ dir",
          !QDir(installDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)
               .contains(QStringLiteral("Scripts")));
}

static void run_fomod_scripts()
{
    std::cout << "=== fomod_scripts::installDeclaredScripts ===\n";
    fomodscripts_testRescueFromManifestParent();
    fomodscripts_testRescueFromArchiveRoot();
    fomodscripts_testManyDeclarationsInOneManifest();
    fomodscripts_testCommentsAndBlanksIgnored();
    fomodscripts_testMissingDeclaredScriptIsSkipped();
    fomodscripts_testAlreadyInstalledFileIsLeftAlone();
    fomodscripts_testWindowsBackslashPathsResolve();
    fomodscripts_testDeclaredScriptMergesIntoStagedCase();
    std::cout << "\n";
}

static void fomodinstall_testEmptyFomodOutputFallsBack()
{
    std::cout << "\n[empty FOMOD output → EmptyFallback]\n";
    QTemporaryDir dir;
    const QString modsDir    = dir.filePath("mods");
    const QString extractDir = dir.filePath("mods/archive-stuff");
    const QString fomodPath  = extractDir + "/fomod_install";
    const QString rawModPath = extractDir;
    QDir().mkpath(fomodPath);       // empty FOMOD output
    writeFile(extractDir + "/plugin.esp", "raw plugin data");

    const auto r = fomod_install::promote(extractDir, rawModPath, fomodPath,
                                          /*titleHint=*/QString(), modsDir);

    check("outcome is EmptyFallback",
          r.outcome == fomod_install::PromoteOutcome::EmptyFallback);
    check("finalModPath is the raw extract",
          r.finalModPath == rawModPath);
    check("empty fomod_install folder was removed",
          !QFileInfo::exists(fomodPath));
    check("raw extract survives",
          QFileInfo::exists(extractDir + "/plugin.esp"));
    check("extractDirRemoved is false", !r.extractDirRemoved);
}

static void fomodinstall_testPromoteWithoutTitleReusesExtractDirName()
{
    std::cout << "\n[non-empty FOMOD + no title → lands at extractDir basename, wrapper removed]\n";
    QTemporaryDir dir;
    const QString modsDir    = dir.filePath("mods");
    const QString extractDir = dir.filePath("mods/archive-stuff");
    const QString fomodPath  = extractDir + "/fomod_install";
    QDir().mkpath(modsDir);
    writeFile(fomodPath + "/picked.esp", "content");
    writeFile(extractDir + "/02 NotPicked Patch/skip.esp", "unpicked variant");

    const auto r = fomod_install::promote(extractDir, extractDir, fomodPath,
                                          /*titleHint=*/QString(), modsDir);

    check("outcome is Promoted",
          r.outcome == fomod_install::PromoteOutcome::Promoted);
    check("finalModPath inherits the extractDir basename",
          r.finalModPath == QDir(modsDir).filePath("archive-stuff"),
          "got: " + r.finalModPath);
    check("picked file is at the relocated install",
          QFileInfo::exists(r.finalModPath + "/picked.esp"));
    check("unpicked sibling variant is GONE",
          !QFileInfo::exists(r.finalModPath + "/02 NotPicked Patch/skip.esp"));
    check("extractDirRemoved is true",
          r.extractDirRemoved);
    check("raw extractDir path no longer a directory outside the relocated install",
          !QFileInfo::exists(extractDir + "/fomod_install"));
}

static void fomodinstall_testPromoteWithTitleRenamesAndCleansWrapper()
{
    std::cout << "\n[non-empty FOMOD + title → rename + wrapper tidied]\n";
    QTemporaryDir dir;
    const QString modsDir    = dir.filePath("mods");
    const QString extractDir = dir.filePath("mods/archive-stuff");
    const QString fomodPath  = extractDir + "/fomod_install";
    QDir().mkpath(modsDir);
    writeFile(fomodPath + "/picked.esp", "content");

    const auto r = fomod_install::promote(extractDir, extractDir, fomodPath,
                                          "Distant Ebon Tower", modsDir);

    check("outcome is Promoted",
          r.outcome == fomod_install::PromoteOutcome::Promoted);
    check("finalModPath lands under modsDir with the title",
          r.finalModPath == QDir(modsDir).filePath("Distant Ebon Tower"),
          "got: " + r.finalModPath);
    check("renamed directory has the FOMOD file",
          QFileInfo::exists(r.finalModPath + "/picked.esp"));
    check("wrapper extractDir was removed", r.extractDirRemoved);
    check("extractDir gone on disk", !QFileInfo::exists(extractDir));
}

static void fomodinstall_testPromoteWithTitleCollisionAddsSuffix()
{
    std::cout << "\n[title collision → numeric suffix]\n";
    QTemporaryDir dir;
    const QString modsDir    = dir.filePath("mods");
    const QString extractDir = dir.filePath("mods/archive-stuff");
    const QString fomodPath  = extractDir + "/fomod_install";
    QDir().mkpath(modsDir);
    writeFile(modsDir + "/DET/preexisting.esp", "already here");
    writeFile(fomodPath + "/new.esp", "new content");

    const auto r = fomod_install::promote(extractDir, extractDir, fomodPath,
                                          "DET", modsDir);

    check("outcome is Promoted",
          r.outcome == fomod_install::PromoteOutcome::Promoted);
    check("finalModPath got a _2 suffix",
          r.finalModPath == QDir(modsDir).filePath("DET_2"),
          "got: " + r.finalModPath);
    check("preexisting DET untouched",
          QFileInfo::exists(modsDir + "/DET/preexisting.esp"));
    check("new install at DET_2",
          QFileInfo::exists(r.finalModPath + "/new.esp"));
}

static void fomodinstall_testPromoteNonexistentFomodPathFallsBack()
{
    std::cout << "\n[missing fomod_install path → EmptyFallback]\n";
    QTemporaryDir dir;
    const QString modsDir    = dir.filePath("mods");
    const QString extractDir = dir.filePath("mods/archive-stuff");
    const QString rawModPath = extractDir;
    QDir().mkpath(extractDir);
    writeFile(extractDir + "/plugin.esp", "raw");

    const auto r = fomod_install::promote(
        extractDir, rawModPath,
        extractDir + "/fomod_install_never_created",
        /*titleHint=*/QString(), modsDir);

    check("nonexistent fomodPath → EmptyFallback",
          r.outcome == fomod_install::PromoteOutcome::EmptyFallback);
    check("finalModPath is the raw extract",
          r.finalModPath == rawModPath);
    check("raw extract survives", QFileInfo::exists(extractDir + "/plugin.esp"));
    check("extractDirRemoved is false", !r.extractDirRemoved);
}

static void fomodinstall_testPromoteScrubsSiblingVariants()
{
    std::cout << "\n[Caldera-style sibling variants scrubbed on promote]\n";
    QTemporaryDir dir;
    const QString modsDir    = dir.filePath("mods");
    const QString extractDir = dir.filePath("mods/Caldera Priory-52898-2-2");
    const QString fomodPath  = extractDir + "/fomod_install";
    QDir().mkpath(modsDir);

    writeFile(extractDir + "/00 Core/Caldera Priory.ESP",             "core");
    writeFile(extractDir + "/01 Rocky West Gash Patch/rocky.esp",     "rocky");
    writeFile(extractDir + "/02 BCOM Rocky West Gash Patch/bcom.esp", "bcom-not-picked");
    writeFile(extractDir + "/03 Rocky WG Aggressively Compatible/agg.esp", "agg-not-picked");
    writeFile(extractDir + "/04 Remiros' Groundcover Patch/rem.esp",  "rem");
    writeFile(extractDir + "/05 Aesthesia Groundcover Patch/aes.esp", "aes");

    writeFile(fomodPath + "/00 Core/Caldera Priory.ESP",             "core");
    writeFile(fomodPath + "/01 Rocky West Gash Patch/rocky.esp",     "rocky");
    writeFile(fomodPath + "/04 Remiros' Groundcover Patch/rem.esp",  "rem");
    writeFile(fomodPath + "/05 Aesthesia Groundcover Patch/aes.esp", "aes");

    const auto r = fomod_install::promote(
        extractDir, extractDir, fomodPath,
        "Caldera Priory and the Depths of Blood and Bone", modsDir);

    check("outcome is Promoted",
          r.outcome == fomod_install::PromoteOutcome::Promoted);
    check("extractDirRemoved is true", r.extractDirRemoved);
    check("raw extractDir path no longer on disk",
          !QFileInfo::exists(extractDir));
    check("picked Core survives in final dir",
          QFileInfo::exists(r.finalModPath + "/00 Core/Caldera Priory.ESP"));
    check("picked Rocky survives",
          QFileInfo::exists(r.finalModPath + "/01 Rocky West Gash Patch/rocky.esp"));
    check("picked Remiros survives",
          QFileInfo::exists(r.finalModPath + "/04 Remiros' Groundcover Patch/rem.esp"));
    check("picked Aesthesia survives",
          QFileInfo::exists(r.finalModPath + "/05 Aesthesia Groundcover Patch/aes.esp"));
    check("UNPICKED BCOM variant is gone (the point of the scrub)",
          !QFileInfo::exists(r.finalModPath + "/02 BCOM Rocky West Gash Patch/bcom.esp"));
    check("UNPICKED Aggressively-Compatible variant is gone",
          !QFileInfo::exists(r.finalModPath + "/03 Rocky WG Aggressively Compatible/agg.esp"));
}

static void run_fomod_install()
{
    std::cout << "=== fomod_install::promote ===\n";
    fomodinstall_testEmptyFomodOutputFallsBack();
    fomodinstall_testPromoteWithoutTitleReusesExtractDirName();
    fomodinstall_testPromoteWithTitleRenamesAndCleansWrapper();
    fomodinstall_testPromoteWithTitleCollisionAddsSuffix();
    fomodinstall_testPromoteNonexistentFomodPathFallsBack();
    fomodinstall_testPromoteScrubsSiblingVariants();
    std::cout << "\n";
}

static void bain_mkdirs(const QString &p) { QDir().mkpath(p); }
static void bain_touch(const QString &p, const QByteArray &b = "x")
{
    QDir().mkpath(QFileInfo(p).absolutePath());
    QFile f(p);
    if (f.open(QIODevice::WriteOnly)) { f.write(b); f.close(); }
}
static QByteArray bain_readAll(const QString &p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static void bain_testDetection()
{
    std::cout << "\n[looksLikeBain detection boundary]\n";

    {   // two numbered packages, each with data
        QTemporaryDir d;
        bain_touch(d.filePath("00 Core/meshes/a.nif"));
        bain_touch(d.filePath("01 Optional Textures/textures/b.dds"));
        check("two numbered packages -> BAIN", bain::looksLikeBain(d.path()));
        check("packages() returns 2", bain::packages(d.path()).size() == 2);
    }
    {   // fomod/ wins even when the layout is numbered
        QTemporaryDir d;
        bain_touch(d.filePath("00 Core/meshes/a.nif"));
        bain_touch(d.filePath("01 Optional/textures/b.dds"));
        bain_touch(d.filePath("fomod/ModuleConfig.xml"));
        check("fomod/ present -> NOT BAIN", !bain::looksLikeBain(d.path()));
    }
    {   // a top-level asset root is plain mod data, not packages
        QTemporaryDir d;
        bain_touch(d.filePath("00 Core/x.esp"));
        bain_mkdirs(d.filePath("meshes"));
        check("an asset-root sibling -> NOT BAIN", !bain::looksLikeBain(d.path()));
    }
    {   // numbered + ordinary folder isn't a package set
        QTemporaryDir d;
        bain_touch(d.filePath("00 Core/x.esp"));
        bain_touch(d.filePath("Docs/readme.txt"));
        check("numbered + non-numbered mix -> NOT BAIN",
              !bain::looksLikeBain(d.path()));
    }
    {   // one numbered folder = nothing to choose
        QTemporaryDir d;
        bain_touch(d.filePath("00 Core/x.esp"));
        check("single package -> NOT BAIN", !bain::looksLikeBain(d.path()));
    }
    {
        QTemporaryDir d;
        bain_touch(d.filePath("meshes/a.nif"));
        bain_touch(d.filePath("textures/b.dds"));
        check("plain meshes+textures -> NOT BAIN", !bain::looksLikeBain(d.path()));
    }
    {   // accepted false positive: all-numbered install-all mod (harmless, pre-checked)
        QTemporaryDir d;
        bain_touch(d.filePath("00 Core/Core.esm"));
        bain_touch(d.filePath("01 Faction Integration/FI.esp"));
        check("all-numbered install-all mod matches (accepted, all pre-checked)",
              bain::looksLikeBain(d.path()));
    }
    {
        check("nonexistent path -> NOT BAIN",
              !bain::looksLikeBain("/nonexistent/path/nrv_bain_test"));
    }
}

static void bain_testOrdering()
{
    std::cout << "\n[packages() numeric order]\n";
    QTemporaryDir d;
    bain_touch(d.filePath("10 Late/textures/z.dds"));
    bain_touch(d.filePath("02 Mid/textures/y.dds"));
    bain_touch(d.filePath("00 Core/textures/x.dds"));
    const auto pkgs = bain::packages(d.path());
    bool order = pkgs.size() == 3
              && pkgs[0].name.startsWith("00")
              && pkgs[1].name.startsWith("02")
              && pkgs[2].name.startsWith("10");
    check("00 < 02 < 10 (numeric, not lexical)", order,
          pkgs.size() == 3 ? (pkgs[0].name + "," + pkgs[1].name + "," + pkgs[2].name)
                           : QString("size=%1").arg(pkgs.size()));
}

static void bain_testStageMerge()
{
    std::cout << "\n[stage() merges chosen packages, last writer wins]\n";
    QTemporaryDir d;
    bain_touch(d.filePath("00 Core/meshes/x.nif"), "base");
    bain_touch(d.filePath("00 Core/meshes/keep.nif"), "keep");
    bain_touch(d.filePath("01 Patch/meshes/x.nif"), "patch");
    bain_touch(d.filePath("01 Patch/meshes/y.nif"), "new");
    bain_touch(d.filePath("02 Unwanted/meshes/z.nif"), "nope");

    const QString modPath = d.filePath("mod");  // subdir so bain_install lands as a sibling
    QDir().mkpath(modPath);
    QDir(d.path()).rename("00 Core", "mod/00 Core");
    QDir(d.path()).rename("01 Patch", "mod/01 Patch");
    QDir(d.path()).rename("02 Unwanted", "mod/02 Unwanted");

    const QString staged = bain::stage(modPath, {"00 Core", "01 Patch"});
    check("stage returns a non-empty path", !staged.isEmpty(), staged);
    check("base-only file kept",
          QFileInfo::exists(staged + "/meshes/keep.nif"));
    check("patch's new file present",
          QFileInfo::exists(staged + "/meshes/y.nif"));
    check("unselected package NOT staged",
          !QFileInfo::exists(staged + "/meshes/z.nif"));
    check("later package overwrote earlier (last writer wins)",
          bain_readAll(staged + "/meshes/x.nif") == "patch",
          QString::fromUtf8(bain_readAll(staged + "/meshes/x.nif")));

    check("empty selection -> empty staged path",
          bain::stage(modPath, {}).isEmpty());
}

static void run_bain()
{
    std::cout << "=== bain tests ===\n";
    bain_testDetection();
    bain_testOrdering();
    bain_testStageMerge();
    std::cout << "\n";
}

// A Yes/No step only earns a modlist verdict when it is about another mod.
// Absence of a match is not evidence of absence, so the wording has to carry it.
static void run_fomod_hint()
{
    std::cout << "=== fomod_hint (asksAboutAnotherMod) tests ===\n";

    struct Case { const char *step; const char *group; bool wanted; };
    static const Case kCases[] = {
        // Asking after the user's existing setup.
        {"Tamriel Rebuilt", "Do you have Tamriel Rebuilt installed?",   true},
        {"Patches",         "Do you use OAAB_Data?",                    true},
        {"Compatibility",   "For users of Glow in the Dahrk",           true},
        {"",                "Are you using Correct UV Rocks?",          true},
        // The mod name splits the phrase, so only the clause tail gives it away.
        {"Graphic Herbalism", "Is Graphic Herbalism installed?",        true},
        {"Expansion Delay", "Expansion Delay already installed, or not?", true},
        {"MWSE",            "Do you run MWSE?",                         true},
        {"Ashfall",         "Pick Yes if you have Ashfall",             true},
        // A generic page title must no longer suppress the group's question.
        {"Options",         "If you use Ashfall, pick Yes",             true},
        // ModuleConfig.xml text keeps the author's line breaks.
        {"Ashfall",         "Do you\n    use Ashfall?",                 true},

        // Options belonging to the mod being installed. The reported bug is the
        // first one: a glow effect is not a mod, so it gets no verdict.
        {"Glow Effect", "Would you like the lanterns to use a glow effect?", false},
        {"Lantern Size",       "Do you want bigger lanterns?",          false},
        {"Texture Resolution", "2K or 1K?",                             false},
        {"Options",            "Choose your preferred variant",         false},
        {"Install Options",    "Select the parts you want",             false},
        {"Dust",               "Enable dust particles?",                false},
        // "installed" about this install, not about the user's setup.
        {"Meshes",             "Which meshes should be installed?",     false},
        {"Files",              "Select the files to be installed",      false},
        {"Optional",           "Nothing else gets installed.",          false},
    };

    std::cout << "\n[wording decides whether a Yes/No step is about another mod]\n";
    for (const Case &c : kCases) {
        const bool got = fomod::asksAboutAnotherMod(
            QString::fromUtf8(c.step), QString::fromUtf8(c.group));
        const QString label = QString::fromUtf8(c.group);
        check(c.wanted ? "about another mod" : "about this mod's own options",
              got == c.wanted, label);
    }
    std::cout << "\n";

    // -- classifyRuntimeVariant: which Skyrim runtime an option is built for --
    std::cout << "[runtime-pair options classify by version, words, then tokens]\n";
    using RT = fomod::SkyrimRuntime;
    struct RtCase { const char *name; RT wanted; };
    static const RtCase kRt[] = {
        // The Spell Perk Item Distributor pair this was built from.
        {"SSE v1.6.629+ (\"Anniversary Edition\")",  RT::AE},
        {"SSE v1.5.97 (\"Special Edition\")",        RT::SE},
        // Version number alone is enough, in either spelling.
        {"1.6.1170",                                 RT::AE},
        {"DLL for 1.6.xxx",                          RT::AE},
        {"1.5.97 build",                             RT::SE},
        {"1_6_640",                                  RT::AE},
        // Words without a version.
        {"Anniversary Edition DLL",                  RT::AE},
        {"Special Edition (pre-AE update)",          RT::SE},   // version words beat the AE aside
        // Bare tokens, word-bounded: SSE must never read as SE.
        {"AE",                                       RT::AE},
        {"SE",                                       RT::SE},
        {"SSE",                                      RT::None},
        {"Base install",                             RT::None},
        // Both signals at once is not a side of a pair.
        {"1.5.97 - 1.6.317 bridge",                  RT::None},
        // Ordinary option names stay silent.
        {"2K Textures",                              RT::None},
        {"Yes",                                      RT::None},
    };
    for (const RtCase &c : kRt) {
        const auto got = fomod::classifyRuntimeVariant(QString::fromUtf8(c.name));
        check(c.wanted == RT::AE ? "reads as AE"
            : c.wanted == RT::SE ? "reads as SE" : "stays unclassified",
              got == c.wanted, QString::fromUtf8(c.name));
    }

    std::cout << "\n[profile id decides the preferred runtime]\n";
    check("skyrimanniversaryedition prefers AE",
          fomod::runtimePreferenceForGame("skyrimanniversaryedition") == RT::AE);
    check("skyrimspecialedition prefers SE",
          fomod::runtimePreferenceForGame("skyrimspecialedition") == RT::SE);
    check("Enderal SE runs the 1.5.97 engine",
          fomod::runtimePreferenceForGame("enderalspecialedition") == RT::SE);
    check("other games disable the pass",
          fomod::runtimePreferenceForGame("morrowind") == RT::None
          && fomod::runtimePreferenceForGame("skyrim") == RT::None);

    std::cout << "\n[cited mods: the evidence for a missing-mod warning]\n";
    {
        // Verbatim from An Addendum to Tamrielic Lore Data's ModuleConfig.xml -
        // the install that prompted this, where neither cited mod was present.
        const auto ash = fomod::citedMods(
            "Installs Ashfall (https://www.nexusmods.com/morrowind/mods/49057) "
            "compatible meshes. Use the HD version for the HD meshes.");
        check("finds the cited mod page", ash.size() == 1);
        check("closing paren is not swallowed into the id",
              !ash.isEmpty() && ash[0].modId == 49057);
        check("carries the game slug",
              !ash.isEmpty() && ash[0].game == QLatin1String("morrowind"));

        // The option in the same group that cites a different mod.
        const auto glow = fomod::citedMods(
            "Installs Glass Glowset (https://www.nexusmods.com/morrowind/mods/42762) "
            "Compatible Meshes (courtesy of 3deadgods).");
        check("a second option cites its own mod",
              glow.size() == 1 && glow[0].modId == 42762);

        // The quiet case, and the whole reason this is safe: an ordinary
        // option cites nothing, so it can never draw a warning. "Normal Maps"
        // matches no mod in the modlist either, and must stay silent.
        check("ordinary option cites nothing", fomod::citedMods(
            "1K Normals for new textures. For OpenMW support "
            "(still requires Materials set for OpenMW)").isEmpty());
        check("empty description cites nothing",
              fomod::citedMods(QString()).isEmpty());
        check("prose naming a mod without linking it cites nothing",
              fomod::citedMods("Patch for Ashfall users.").isEmpty());

        // URL shapes seen in the wild.
        check("bare host with no scheme still parses",
              fomod::citedMods("see www.nexusmods.com/morrowind/mods/49057")
                  .size() == 1);
        check("trailing sentence period is stripped",
              fomod::citedMods("https://www.nexusmods.com/morrowind/mods/123.")
                  .size() == 1);
        check("a file link is still the mod page",
              fomod::citedMods("https://www.nexusmods.com/skyrim/mods/3863?tab=files")
                  .size() == 1);
        // A page that is not a mod page carries no mod id.
        check("non-mod nexus link is not a citation",
              fomod::citedMods("https://www.nexusmods.com/about/terms").isEmpty());

        // Two mods cited by one option: both are reported, deduplicated.
        const auto two = fomod::citedMods(
            "Needs https://www.nexusmods.com/morrowind/mods/49057 and "
            "https://www.nexusmods.com/morrowind/mods/42762 "
            "(again https://www.nexusmods.com/morrowind/mods/49057).");
        check("multiple citations are all reported", two.size() == 2);
    }

    std::cout << "\n[naming the missing mod]\n";
    {
        // Shape one: the options are named after the mod, the group is not.
        check("common prefix of option names names the mod",
              fomod::missingModLabel({"Ashfall", "Ashfall (HD)"},
                                     "Compatibility Options")
              == QLatin1String("Ashfall"));
        check("a lone option name is used as-is",
              fomod::missingModLabel({"Glass Glowset"}, "Compatibility Options")
              == QLatin1String("Glass Glowset"));

        // Shape two: the options are bare variants, so the group carries the
        // name. This is the shape in the screenshot that prompted the change.
        check("variant options fall back to the group name",
              fomod::missingModLabel({"Core", "HD"}, "Ashfall")
              == QLatin1String("Ashfall"));
        check("a two-letter shared prefix is not a mod name",
              fomod::missingModLabel({"HD", "HQ"}, "Ashfall")
              == QLatin1String("Ashfall"));

        // Separator debris must not survive the truncation.
        check("trailing separators are trimmed off the prefix",
              fomod::missingModLabel({"OAAB_Data - Core", "OAAB_Data - Extra"},
                                     "Group")
              == QLatin1String("OAAB_Data"));
        check("no usable name anywhere yields empty",
              fomod::missingModLabel({"HD", "HQ"}, QString()).isEmpty());
    }

    std::cout << "\n";
}

// Friend hook into buildUi() and the private button tree.
struct FomodWizardTestHook {
    static FomodWizard *build(const QList<FomodStep> &steps,
                              const QString &prior = {},
                              const QStringList &installed = {},
                              const QStringList &installedUrls = {})
    {
        auto *w = new FomodWizard(QStringLiteral("/tmp/nrv_fomod_ui_test"));
        w->m_steps            = steps;
        w->m_priorChoices     = prior;
        w->m_installedModNames = installed;
        for (const QString &u : installedUrls) {
            const auto ref = parseNexusModUrl(u);
            if (ref) w->m_installedNexusKeys.insert(
                ref->game.toLower() + u'/' + QString::number(ref->modId));
        }
        w->buildUi();
        return w;
    }

    static QAbstractButton *btn(FomodWizard *w, int si, int gi, int pi)
    { return w->m_buttons[si][gi][pi]; }

    static int pluginCount(FomodWizard *w, int si, int gi)
    { return w->m_buttons[si][gi].size(); }

    static QString collect(FomodWizard *w) { return w->collectChoices(); }

    static QString fomodRoot(const QString &p) { return FomodWizard::findFomodRoot(p); }
};

static FomodPlugin wizardui_mkPlugin(const QString &name, const QString &type = "Optional")
{
    FomodPlugin p;
    p.name = name;
    p.type = type;
    return p;
}

static FomodGroup wizardui_mkGroup(const QString &type, const QList<FomodPlugin> &plugins)
{
    FomodGroup g;
    g.name    = QStringLiteral("Group");
    g.type    = type;
    g.plugins = plugins;
    return g;
}

static QList<FomodStep> wizardui_oneGroup(const FomodGroup &g)
{
    FomodStep s;
    s.name = QStringLiteral("Step");
    s.groups.append(g);
    return { s };
}

// A named single-group Yes/No step, the shape Pass B reasons about.
static QList<FomodStep> wizardui_yesNoStep(const QString &stepName,
                                           const QString &groupName)
{
    FomodGroup g;
    g.name    = groupName;
    g.type    = QStringLiteral("SelectExactlyOne");
    g.plugins = { wizardui_mkPlugin("Yes"), wizardui_mkPlugin("No") };
    FomodStep s;
    s.name = stepName;
    s.groups.append(g);
    return { s };
}

// The synthetic "None" radio is the group-box QRadioButton that isn't a plugin
// button; nullptr for groups without one (SelectExactlyOne, checkboxes).
static QRadioButton *wizardui_findNoneRadio(FomodWizard *w, int si, int gi)
{
    const int n = FomodWizardTestHook::pluginCount(w, si, gi);
    if (n == 0) return nullptr;
    QWidget *box = FomodWizardTestHook::btn(w, si, gi, 0)->parentWidget();
    if (!box) return nullptr;
    QSet<QAbstractButton *> plugins;
    for (int pi = 0; pi < n; ++pi)
        plugins.insert(FomodWizardTestHook::btn(w, si, gi, pi));
    const auto radios = box->findChildren<QRadioButton *>();
    for (QRadioButton *r : radios)
        if (!plugins.contains(r)) return r;
    return nullptr;
}

static void wizardui_testFindFomodRoot()
{
    std::cout << "\n[findFomodRoot locates fomod/ under a wrapper]\n";
    {   // fomod/ at the root
        QTemporaryDir d;
        writeFile(d.filePath("fomod/ModuleConfig.xml"));
        check("direct fomod/ at root is found",
              FomodWizardTestHook::fomodRoot(d.path()) == d.path(),
              FomodWizardTestHook::fomodRoot(d.path()));
    }
    {   // Nexus wrapper + sibling file that suppresses the dive
        QTemporaryDir d;
        writeFile(d.filePath("Completionist Patch Hub-58523/fomod/ModuleConfig.xml"));
        writeFile(d.filePath("readme.txt"), "x");
        const QString want = d.filePath("Completionist Patch Hub-58523");
        check("fomod/ under a wrapper (with sibling file) is found",
              FomodWizardTestHook::fomodRoot(d.path()) == want,
              FomodWizardTestHook::fomodRoot(d.path()));
    }
    {
        QTemporaryDir d;
        writeFile(d.filePath("00 Core/meshes/x.nif"));
        check("no fomod/ anywhere returns empty",
              FomodWizardTestHook::fomodRoot(d.path()).isEmpty());
    }
    {   // fomod/ at two depths -> shallowest wins
        QTemporaryDir d;
        writeFile(d.filePath("fomod/ModuleConfig.xml"));
        writeFile(d.filePath("sub/fomod/ModuleConfig.xml"));
        check("shallowest fomod/ wins",
              FomodWizardTestHook::fomodRoot(d.path()) == d.path(),
              FomodWizardTestHook::fomodRoot(d.path()));
    }
}

// Pass B: the modlist verdict on Yes/No steps, and its absence.
static void wizardui_testModlistVerdict()
{
    const QString kPresent = QStringLiteral("currently present");
    const QString kAbsent  = QStringLiteral("not currently present");

    {   // The reported bug: an option of the mod itself, not a dependency.
        std::cout << "\n[a Yes/No step about this mod's own options gets no verdict]\n";
        auto *w = FomodWizardTestHook::build(
            wizardui_yesNoStep("Glow Effect",
                               "Would you like the lanterns to use a glow effect?"),
            {}, { "Glow in the Dahrk", "Better Bodies" });
        QAbstractButton *yes = FomodWizardTestHook::btn(w, 0, 0, 0);
        QAbstractButton *no  = FomodWizardTestHook::btn(w, 0, 0, 1);
        check("Yes carries no modlist claim",
              !yes->text().contains(kPresent), yes->text());
        check("No carries no modlist claim",
              !no->text().contains(kAbsent), no->text());
        check("the FOMOD's own default (Yes) survives", yes->isChecked());
        check("No is not pre-picked", !no->isChecked());
        delete w;
    }

    {   // Positive half: the match itself proves the step names a mod, so no
        // wording is needed. Underscore variant: step "OAAB_Data" -> "OAAB Data".
        std::cout << "\n[a named mod that IS in the modlist recommends Yes]\n";
        auto *w = FomodWizardTestHook::build(
            wizardui_yesNoStep("OAAB_Data", "OAAB_Data"),
            {}, { "OAAB Data", "Better Bodies" });
        QAbstractButton *yes = FomodWizardTestHook::btn(w, 0, 0, 0);
        QAbstractButton *no  = FomodWizardTestHook::btn(w, 0, 0, 1);
        check("Yes is recommended and picked",
              yes->isChecked() && yes->text().contains(kPresent), yes->text());
        check("No is untouched",
              !no->isChecked() && !no->text().contains(kAbsent), no->text());
        delete w;
    }

    {   // Negative half: absent from the modlist, but the question says it is
        // asking about the user's setup. A generic step title must not block it.
        std::cout << "\n[a mod the question asks after, and the modlist lacks, recommends No]\n";
        auto *w = FomodWizardTestHook::build(
            wizardui_yesNoStep("Options", "Do you use OAAB_Data?"),
            {}, { "Better Bodies" });
        QAbstractButton *yes = FomodWizardTestHook::btn(w, 0, 0, 0);
        QAbstractButton *no  = FomodWizardTestHook::btn(w, 0, 0, 1);
        check("No is recommended and picked",
              no->isChecked() && no->text().contains(kAbsent), no->text());
        check("Yes is untouched",
              !yes->isChecked() && !yes->text().contains(kPresent), yes->text());
        delete w;
    }
}

// The install that prompted this: An Addendum to Tamrielic Lore Data offers
// Ashfall-compatible meshes and pre-ticks a Glass Glowset option, with neither
// mod in the 412-mod list. Descriptions and mod ids are verbatim from its
// ModuleConfig.xml.
static void wizardui_testMissingCitedMod()
{
    const QString kAshfall =
        "Installs Ashfall (https://www.nexusmods.com/morrowind/mods/49057) "
        "compatible meshes. Use the HD version for the HD meshes.";
    const QString kGlowset =
        "Installs Glass Glowset (https://www.nexusmods.com/morrowind/mods/42762) "
        "Compatible Meshes (courtesy of 3deadgods).";
    const QString kNormals =
        "1K Normals for new textures. For OpenMW support "
        "(still requires Materials set for OpenMW)";

    auto withDesc = [](const QString &name, const QString &desc) {
        FomodPlugin p = wizardui_mkPlugin(name);
        p.description = desc;
        return p;
    };

    std::cout << "\n[compatibility option for a mod that is not installed]\n";
    {
        FomodGroup g = wizardui_mkGroup("SelectAny", {
            withDesc("Ashfall",       kAshfall),
            withDesc("Ashfall (HD)",  kAshfall),
            withDesc("Glass Glowset", kGlowset),
        });
        g.name = QStringLiteral("Compatibility Options");
        FomodStep st;
        st.name   = QStringLiteral("Compatibility Options");
        st.groups = { g };
        auto *w = FomodWizardTestHook::build({ st });

        const QString ash = FomodWizardTestHook::btn(w, 0, 0, 0)->text();
        check("warns on the option", ash.contains(QString::fromUtf8("⚠")), ash);
        check("names the missing mod", ash.contains("Ashfall is not installed"), ash);
        check("the HD variant warns too",
              FomodWizardTestHook::btn(w, 0, 0, 1)->text().contains("Ashfall is not installed"));
        check("each option names its own mod",
              FomodWizardTestHook::btn(w, 0, 0, 2)->text()
                  .contains("Glass Glowset is not installed"));
        // Warn, don't decide: the author's defaults are left alone.
        check("selection is not changed",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        delete w;
    }

    std::cout << "\n[the same options once the mod IS installed]\n";
    {
        FomodGroup g = wizardui_mkGroup("SelectAny", {
            withDesc("Ashfall",       kAshfall),
            withDesc("Glass Glowset", kGlowset),
        });
        g.name = QStringLiteral("Compatibility Options");
        FomodStep st;
        st.name   = QStringLiteral("Compatibility Options");
        st.groups = { g };
        auto *w = FomodWizardTestHook::build({ st }, {}, {},
            { "https://www.nexusmods.com/morrowind/mods/49057" });

        check("no warning once the cited mod is installed",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->text()
                  .contains(QString::fromUtf8("⚠")));
        check("the option for the other absent mod still warns",
              FomodWizardTestHook::btn(w, 0, 0, 1)->text()
                  .contains("Glass Glowset is not installed"));
        delete w;
    }

    std::cout << "\n[options that cite nothing stay silent]\n";
    {
        // "Normal Maps" matches no mod in the modlist either. Without a
        // citation there is no evidence, so it must draw nothing - this is
        // the guard against the warning becoming noise on ordinary options.
        FomodGroup g = wizardui_mkGroup("SelectAny", { withDesc("1K", kNormals) });
        g.name = QStringLiteral("Normal Maps");
        FomodStep st;
        st.name   = QStringLiteral("Compatibility Options");
        st.groups = { g };
        auto *w = FomodWizardTestHook::build({ st });
        check("no warning without a citation",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->text()
                  .contains(QString::fromUtf8("⚠")));
        delete w;
    }

    std::cout << "\n[variant options take the group's name]\n";
    {
        // The shape in the screenshot: a group called "Ashfall" whose options
        // are bare variants, so the mod name comes from the group.
        FomodGroup g = wizardui_mkGroup("SelectExactlyOne", {
            withDesc("Core", kAshfall),
            withDesc("HD",   kAshfall),
        });
        g.name = QStringLiteral("Ashfall");
        FomodStep st;
        st.name   = QStringLiteral("Compatibility Options");
        st.groups = { g };
        auto *w = FomodWizardTestHook::build({ st });
        check("group name names the mod",
              FomodWizardTestHook::btn(w, 0, 0, 0)->text()
                  .contains("Ashfall is not installed"),
              FomodWizardTestHook::btn(w, 0, 0, 0)->text());
        delete w;
    }
}

static void run_fomod_wizard_ui()
{
    std::cout << "=== fomod_wizard_ui (buildUi) tests ===\n";

    wizardui_testFindFomodRoot();
    wizardui_testModlistVerdict();
    wizardui_testMissingCitedMod();

    // SelectAtMostOne, nothing required: starts on None
    {
        std::cout << "\n[SelectAtMostOne defaults to none]\n";
        auto *w = FomodWizardTestHook::build(
            wizardui_oneGroup(wizardui_mkGroup("SelectAtMostOne",
                             { wizardui_mkPlugin("Alpha"), wizardui_mkPlugin("Beta") })));
        check("no plugin auto-checked (A)",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("no plugin auto-checked (B)",
              !FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        QRadioButton *none = wizardui_findNoneRadio(w, 0, 0);
        check("a None radio exists", none != nullptr);
        check("None is selected by default", none && none->isChecked());
        check("nothing serialized while None is active",
              FomodWizardTestHook::collect(w).isEmpty(),
              FomodWizardTestHook::collect(w));
        delete w;
    }

    // SelectAtMostOne: None and plugin mutually exclusive
    {
        std::cout << "\n[SelectAtMostOne None <-> plugin are mutually exclusive]\n";
        auto *w = FomodWizardTestHook::build(
            wizardui_oneGroup(wizardui_mkGroup("SelectAtMostOne",
                             { wizardui_mkPlugin("Alpha"), wizardui_mkPlugin("Beta") })));
        QRadioButton *none = wizardui_findNoneRadio(w, 0, 0);
        check("precondition: None starts on", none && none->isChecked());
        FomodWizardTestHook::btn(w, 0, 0, 0)->setChecked(true); // pick Alpha
        check("picking a plugin clears None", none && !none->isChecked());
        check("the picked plugin becomes the serialized choice",
              FomodWizardTestHook::collect(w) == QLatin1String("0:0:0"),
              FomodWizardTestHook::collect(w));
        delete w;
    }

    // SelectAtMostOne with a Recommended plugin
    {
        std::cout << "\n[SelectAtMostOne honours a Recommended default]\n";
        auto *w = FomodWizardTestHook::build(
            wizardui_oneGroup(wizardui_mkGroup("SelectAtMostOne",
                             { wizardui_mkPlugin("Alpha"),
                               wizardui_mkPlugin("Beta", "Recommended") })));
        check("recommended plugin checked",
              FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        QRadioButton *none = wizardui_findNoneRadio(w, 0, 0);
        check("None radio still present", none != nullptr);
        check("None NOT selected when a plugin is recommended",
              none && !none->isChecked());
        delete w;
    }

    // SelectExactlyOne: first selectable forced on, no None radio
    {
        std::cout << "\n[SelectExactlyOne forces a pick and offers no None]\n";
        auto *w = FomodWizardTestHook::build(
            wizardui_oneGroup(wizardui_mkGroup("SelectExactlyOne",
                             { wizardui_mkPlugin("Alpha"), wizardui_mkPlugin("Beta") })));
        check("first option forced on",
              FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("second option off",
              !FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        check("no None radio", wizardui_findNoneRadio(w, 0, 0) == nullptr);
        delete w;
    }

    // SelectExactlyOne, NotUsable first option
    {
        std::cout << "\n[SelectExactlyOne skips a NotUsable first option]\n";
        auto *w = FomodWizardTestHook::build(
            wizardui_oneGroup(wizardui_mkGroup("SelectExactlyOne",
                             { wizardui_mkPlugin("Alpha", "NotUsable"),
                               wizardui_mkPlugin("Beta") })));
        check("NotUsable option disabled",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isEnabled());
        check("NotUsable option not checked",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("forced selection lands on the next usable option",
              FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        delete w;
    }

    // SelectAll: every plugin checked, no None
    {
        std::cout << "\n[SelectAll checks everything]\n";
        auto *w = FomodWizardTestHook::build(
            wizardui_oneGroup(wizardui_mkGroup("SelectAll",
                             { wizardui_mkPlugin("Alpha"), wizardui_mkPlugin("Beta") })));
        check("all checked (A)", FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("all checked (B)", FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        check("no None radio", wizardui_findNoneRadio(w, 0, 0) == nullptr);
        delete w;
    }

    // SelectAny: lone plugin defaults on, multiple default off
    {
        std::cout << "\n[SelectAny default-on only for a lone plugin]\n";
        auto *w1 = FomodWizardTestHook::build(
            wizardui_oneGroup(wizardui_mkGroup("SelectAny", { wizardui_mkPlugin("Alpha") })));
        check("single SelectAny plugin defaults on",
              FomodWizardTestHook::btn(w1, 0, 0, 0)->isChecked());
        delete w1;

        auto *w2 = FomodWizardTestHook::build(
            wizardui_oneGroup(wizardui_mkGroup("SelectAny",
                             { wizardui_mkPlugin("Alpha"), wizardui_mkPlugin("Beta") })));
        check("multi SelectAny A defaults off",
              !FomodWizardTestHook::btn(w2, 0, 0, 0)->isChecked());
        check("multi SelectAny B defaults off",
              !FomodWizardTestHook::btn(w2, 0, 0, 1)->isChecked());
        delete w2;
    }

    // Required plugin: checked and disabled
    {
        std::cout << "\n[Required plugin is forced on and locked]\n";
        auto *w = FomodWizardTestHook::build(
            wizardui_oneGroup(wizardui_mkGroup("SelectAny", { wizardui_mkPlugin("Alpha", "Required") })));
        check("required plugin checked",
              FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("required plugin disabled",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isEnabled());
        delete w;
    }

    // smart default: OpenMW preferred over MGE XE
    {
        std::cout << "\n[smart default: OpenMW wins over MGE XE]\n";
        auto *w = FomodWizardTestHook::build(
            wizardui_oneGroup(wizardui_mkGroup("SelectExactlyOne",
                             { wizardui_mkPlugin("MGE XE version"),
                               wizardui_mkPlugin("OpenMW version") })));
        check("OpenMW option chosen",
              FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        check("MGE option not chosen",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("still no None radio (SelectExactlyOne)",
              wizardui_findNoneRadio(w, 0, 0) == nullptr);
        delete w;
    }

    std::cout << "\n";
}

int main(int argc, char **argv)
{
    // Wizard section needs Widgets; run headless via offscreen QPA.
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    run_fomod_path();
    run_fomod_copy();
    run_fomod_scripts();
    run_fomod_install();
    run_bain();
    run_fomod_hint();
    run_fomod_wizard_ui();

    std::cout << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
