// FOMOD/BAIN path resolution, copy, script rescue, install promote, bain, wizard UI.
// Wizard tests need a QApplication (offscreen QPA, see main).

#include "fomod_path.h"
#include "fomod_copy.h"
#include "fomod_hint.h"
#include "mod_aliases.h"
#include "fomod_scripts.h"
#include "fomod_install.h"
#include "bain.h"
#include "bain_hint.h"
#include "bainwizard.h"

#include <QCheckBox>
#include <QScrollArea>
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

// ===== bain_hint: what a package name says it is for =====
//
// Names below are verbatim from a real mods folder. The point of this suite is
// not that the patches are found - it is that the things which are NOT patches
// are left alone.

using BV = bain::PackageVerdict;

static bain::PackageVerdict bh_judge(const QString &pkg,
                                     const QStringList &installed,
                                     const QString &own = {})
{
    return bain::judgeOne(pkg, {}, installed, {}, own);
}

static const char *bh_stateName(BV::State s)
{
    switch (s) {
        case BV::State::Installed: return "Installed";
        case BV::State::Missing:   return "Missing";
        default:                   return "Unknown";
    }
}

static void bh_check(const char *what, const QString &pkg,
                     const QStringList &installed, BV::State want,
                     const QString &own = {})
{
    const auto v = bh_judge(pkg, installed, own);
    check(what, v.state == want,
          QString("%1 -> %2 (target '%3', matched '%4')")
              .arg(pkg, QString::fromLatin1(bh_stateName(v.state)),
                   v.target, v.matched),
          QString::fromLatin1(bh_stateName(want)));
}

static void bainhint_testInstalled()
{
    std::cout << "\n[the mod a package patches is installed]\n";

    const QStringList modlist = {
        QStringLiteral("Tamriel Rebuilt 25.08.12"),
        QStringLiteral("Uncharted Artifacts"),
        QStringLiteral("Project Atlas"),
        QStringLiteral("Glow in the Dahrk"),
        QStringLiteral("Patch for Purists"),
    };

    bh_check("a double marker still finds the mod", "02 Patch - TR Patch",
             modlist, BV::State::Installed);
    bh_check("and the acronym bridges to the full name",
             "02 Patch - TR Patch", modlist, BV::State::Installed);
    bh_check("a plainly named patch", "01 Patch - Uncharted Artifacts",
             modlist, BV::State::Installed);

    // Measured false negative: "Atlas" is one word, so it only clears the bar
    // because the alias table vouches for it - and the alias is what reaches
    // "Project Atlas", which does not start with "Atlas".
    bh_check("a one-word target the table knows", "01 Atlas Patch",
             modlist, BV::State::Installed);

    // Measured false negative: the acronym found nothing on its own.
    bh_check("an acronym target", "01 GITD Patch", modlist,
             BV::State::Installed);
    bh_check("an acronym behind a qualifier", "01 - Vanilla GITD Patch",
             modlist, BV::State::Installed);

    // The residue is "Purists"; only the whole name answers.
    bh_check("a mod whose name contains the marker",
             "01 - Patch for Purists", modlist, BV::State::Installed);

    const auto v = bh_judge("02 Patch - TR Patch", modlist);
    check("and it reports which mod answered",
          v.matched == QStringLiteral("Tamriel Rebuilt 25.08.12"), v.matched);
}

static void bainhint_testMissing()
{
    std::cout << "\n[the mod a package patches is not installed]\n";

    // Ground truth from the archive that prompted this: OAAB Shipwrecks.
    const QStringList modlist = {
        QStringLiteral("Tamriel Rebuilt 25.08.12"),
        QStringLiteral("Uncharted Artifacts"),
        QStringLiteral("OAAB_Data"),
    };

    bh_check("four title-cased words read as a name",
             "03 Patch - Of Pillows and Peril", modlist, BV::State::Missing);
    // The case a master check gets wrong: that patch's plugin declares only
    // base game, Tamriel_Data and OAAB_Data, all of them present.
    bh_check("two title-cased words are enough",
             "04 Patch - Lush Synthesis", modlist, BV::State::Missing);
    bh_check("a known one-word target that is absent", "01 Atlas Patch",
             modlist, BV::State::Missing);

    const auto v = bh_judge("04 Patch - Lush Synthesis", modlist);
    check("and names the mod it wanted",
          v.target == QStringLiteral("Lush Synthesis"), v.target);
    check("from the name, not a master",
          v.source == BV::Source::Name);
}

static void bainhint_testSilence()
{
    std::cout << "\n[everything that is not a patch is left alone]\n";

    // The false positive: "OpenMW" is the engine. It lives in the game folder,
    // not the modlist, so the modlist cannot answer for it either way - and
    // without the stop-list it matches the first mod whose name starts OpenMW.
    bh_check("an engine name is not a mod", "03 OpenMW Patch",
             {QStringLiteral("OpenMW Skyrim Style Quest Notifications")},
             BV::State::Unknown);
    bh_check("nor is a script extender", "02 MWSE Patch",
             {QStringLiteral("MWSE")}, BV::State::Unknown);

    const QStringList some = {QStringLiteral("Tamriel Rebuilt")};
    bh_check("'Fix' is not a marker", "03 Chuzei Fix", some, BV::State::Unknown);
    bh_check("a lower-case residue is not a name",
             "05 - Patch for water removal", some, BV::State::Unknown);
    bh_check("one Title word the table cannot vouch for",
             "01 White suran patch", some, BV::State::Unknown);
    bh_check("an acronym nobody can vouch for", "03 TPOTAI patch", some,
             BV::State::Unknown);
    bh_check("no marker at all", "00 Core", some, BV::State::Unknown);
    bh_check("nor here", "01 Stratified Rocks", some, BV::State::Unknown);
    bh_check("nor a bare mod name", "02 - Atlas", some, BV::State::Unknown);

    // Confident enough to find, not confident enough to miss.
    bh_check("a one-word target stays silent when absent",
             "01 - Patch for Purists", some, BV::State::Unknown);

    std::cout << "\n[a package naming its own mod is not naming another]\n";
    bh_check("self-reference", "01 OAAB Patch", {QStringLiteral("Tamriel Rebuilt")},
             BV::State::Unknown, QStringLiteral("OAAB Grazelands"));

    std::cout << "\n[an empty modlist is evidence of nothing]\n";
    bh_check("nothing to ask", "04 Patch - Lush Synthesis", {},
             BV::State::Unknown);
}

static void bainhint_testAnchoring()
{
    std::cout << "\n[a short needle has to start the mod name]\n";
    // "OAABandoned Shack" is a real folder in the mods directory this was
    // measured against. Loosening the start-anchor to a bare word boundary
    // would report OAAB_Data as installed on the strength of it.
    bh_check("OAAB does not fire inside OAABandoned", "01 OAAB Patch",
             {QStringLiteral("OAABandoned Shack 1.1")}, BV::State::Missing);
    bh_check("and does fire when it opens the name", "01 OAAB Patch",
             {QStringLiteral("OAAB_Data")}, BV::State::Installed);
}

static void bainhint_testMasters()
{
    std::cout << "\n[a master the modlist cannot supply outranks the name]\n";

    const QSet<QString> have = {QStringLiteral("oaab_data.esm")};

    {   // Name says nothing; the plugin says everything.
        const auto v = bain::judgeOne("01 Optional Extras",
                                      {QStringLiteral("TR_Mainland.esm")},
                                      {QStringLiteral("Tamriel Rebuilt")},
                                      have);
        check("an unsatisfiable master is a miss on its own",
              v.state == BV::State::Missing);
        check("and the reason is the file, not a guess",
              v.master == QStringLiteral("TR_Mainland.esm"), v.master);
        check("reported as master evidence", v.source == BV::Source::Master);
    }
    {   // Satisfied masters must not vouch for anything - the lush3 shape.
        const auto v = bain::judgeOne("04 Patch - Lush Synthesis",
                                      {QStringLiteral("OAAB_Data.esm")},
                                      {QStringLiteral("OAAB_Data")}, have);
        check("satisfied masters do not veto the name verdict",
              v.state == BV::State::Missing && v.source == BV::Source::Name,
              QString::fromLatin1(bh_stateName(v.state)));
    }
    {   // Empty inventory = the pass is off, not "everything is missing".
        const auto v = bain::judgeOne("01 Optional Extras",
                                      {QStringLiteral("TR_Mainland.esm")},
                                      {QStringLiteral("Tamriel Rebuilt")}, {});
        check("an empty plugin inventory turns the master pass off",
              v.state == BV::State::Unknown);
    }
    {   // Hard evidence beats a name that says the mod is there.
        const auto v = bain::judgeOne("01 Patch - Uncharted Artifacts",
                                      {QStringLiteral("Nowhere.esm")},
                                      {QStringLiteral("Uncharted Artifacts")},
                                      have);
        check("a missing master outranks an installed-looking name",
              v.state == BV::State::Missing && v.source == BV::Source::Master);
    }
}

static void bainhint_testArchiveGuard()
{
    std::cout << "\n[the pass never leaves the archive with nothing ticked]\n";
    // stage() returns "" for an empty selection and the caller reads "" as a
    // cancel, so a pass confident about every package would abort the install.
    QTemporaryDir d;
    bain_touch(d.filePath("mod/01 Patch - Of Pillows and Peril/x.esp"));
    bain_touch(d.filePath("mod/02 Patch - Lush Synthesis/y.esp"));
    const auto pkgs = bain::packages(d.filePath("mod"));
    check("two packages found", pkgs.size() == 2);

    const auto v = bain::judgePackages(pkgs, {QStringLiteral("Tamriel Rebuilt")},
                                       {}, {});
    const bool allQuiet = std::all_of(v.cbegin(), v.cend(),
        [](const BV &x) { return x.state == BV::State::Unknown; });
    check("all-missing is downgraded to silence", allQuiet);
}

// A package name with no marker, but a bare "for"/"with" pointing at a mod.
//
// Every name here is verbatim from the real mods folder. The archive that
// prompted it is Death and Taxes, whose "01 Icons for OpenMW SSQN" carried no
// badge at all - neither "you have that" nor "you do not" - because the parser
// needed a "Patch"/"Addon" word before it would look.
//
// The gate being tested is the alias table. It is the only evidence a folder
// name offers, so a tail it cannot vouch for stays silent however name-shaped
// it looks; and a tail it CAN vouch for still loses when the vouching word is
// an engine, which lives in the game folder and can never be in a modlist.
static void bainhint_testBareJoin()
{
    std::cout << "\n[a bare \"for\" naming a mod the table knows]\n";

    const QStringList withSsqn = {
        QStringLiteral("OpenMW Skyrim Style Quest Notifications (SSQN)"),
        QStringLiteral("OAAB_Data-49042-2-6-2-1780155954"),
    };
    const QStringList withoutSsqn = {
        QStringLiteral("Tamriel Rebuilt 25.08.12"),
        QStringLiteral("Uncharted Artifacts"),
    };

    bh_check("the icons package finds the mod it is for",
             "01 Icons for OpenMW SSQN", withSsqn, BV::State::Installed);
    bh_check("and says so when the mod is not there",
             "01 Icons for OpenMW SSQN", withoutSsqn, BV::State::Missing);

    // The badge has to name the package's own target rather than the row that
    // answered, which here reached it through the SSQN alias.
    const auto v = bh_judge("01 Icons for OpenMW SSQN", withSsqn);
    check("the badge names the target",
          v.target == QStringLiteral("OpenMW SSQN"), v.target);
    check("and the tooltip can name the row that answered",
          v.matched == withSsqn[0], v.matched);

    bh_check("a join anywhere in the name is enough",
             "04 Riders - to use with OAAB Grazelands", withSsqn,
             BV::State::Installed);

    // -- and the silences, which are the point of the gate ---------------

    // The case the old comment named as the reason not to split at all. It
    // still says nothing, because "OpenMW" is a stop-word and the table does
    // not know it - a different mechanism, same answer.
    bh_check("an engine after the join is not a mod", "01 Icons for OpenMW",
             withSsqn, BV::State::Unknown);

    // The one false untick this gate prevents: MGE XE is in the alias table
    // AND in the stop-word list, so it cannot vouch. Without that, this grass
    // would start unticked against a tool no modlist can contain.
    bh_check("an engine cannot vouch for the tail either",
             "01 Grass for MGEXE and OpenMW", withoutSsqn, BV::State::Unknown);

    bh_check("a tail the table does not know stays silent",
             "01 Grass for Remiros' Groundcover", withoutSsqn,
             BV::State::Unknown);
    bh_check("however name-shaped it is",
             "01 GITD Telvanni Dormers Patch - for users of Sadrith Mora - SOP",
             withoutSsqn, BV::State::Unknown);

    // Whitespace is required on both sides of the join, so a name that merely
    // opens with "For" is not making a claim about a mod.
    bh_check("a leading For is not a join",
             "02 For WIP Detailed Correct UV Rocks", withoutSsqn,
             BV::State::Unknown);
    bh_check("nor is it when the rest looks like a name",
             "01 For Vanilla Rocks", withoutSsqn, BV::State::Unknown);

    // The marker path is untouched: these two answered before this change and
    // must answer identically now.
    bh_check("a marker still wins on its own", "01 SSQN Addon", withSsqn,
             BV::State::Installed);
    bh_check("and a stop-word target is still silent", "03 OpenMW Patch",
             withSsqn, BV::State::Unknown);
}

static void run_bain_hint()
{
    std::cout << "=== bain_hint tests ===\n";
    bainhint_testInstalled();
    bainhint_testMissing();
    bainhint_testSilence();
    bainhint_testAnchoring();
    bainhint_testMasters();
    bainhint_testArchiveGuard();
    bainhint_testBareJoin();
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


    std::cout << "\n[options that require another mod]\n";
    {
        // Grand Solitude's SMIM Rotor - ships ticked, and installs a mesh
        // nothing loads correctly when SMIM is absent.
        const auto smim = fomod::requiredMods(
            "SMIM Rotor for Solitude Windmill. "
            "Required Static Mesh Improvement Mod - SMIM by Brumbek.");
        check("the required mod is found",
              smim.contains(QStringLiteral("Static Mesh Improvement Mod")),
              smim.join(QLatin1Char('|')));
        check("and so is its acronym",
              smim.contains(QStringLiteral("SMIM")), smim.join(QLatin1Char('|')));

        // Every OTHER requirement sentence in the author's mod corpus is about
        // the mod being installed, not another one. A keyword rule fires on
        // all three; these are the regression guards.
        check("\"Required for the mod to function\" names no mod",
              fomod::requiredMods(
                  "The core files for Voices of Vvardenfell. "
                  "Required for the mod to function.").isEmpty());
        check("\"only required file in the installer\" names no mod",
              fomod::requiredMods(
                  "Core Files for AATL_Data. Full package included, "
                  "only required file in the installer.").isEmpty());
        check("\"requires Materials set for OpenMW\" names no mod",
              fomod::requiredMods(
                  "1K Normals for new textures. For OpenMW support "
                  "(still requires Materials set for OpenMW)").isEmpty());

        // Other real phrasings.
        check("an article is skipped",
              fomod::requiredMods("Requires the Unofficial Skyrim Special Edition Patch to work.")
                  .contains(QStringLiteral("Unofficial Skyrim Special Edition Patch")));
        check("\"needs\" counts too",
              fomod::requiredMods("Needs JK's Skyrim installed first.")
                  .contains(QStringLiteral("JK's Skyrim")));
        check("a description with no requirement says nothing",
              fomod::requiredMods("Installs Ashfall compatible meshes.").isEmpty());
        check("empty in, empty out", fomod::requiredMods(QString()).isEmpty());

        // Grand Solitude's own required entry: "Required Main Files." under a
        // group called "Required", on an option called "Main files". Without
        // the self-reference guard this parsed as a missing mod named "Main
        // Files" and warned on the one option the user cannot deselect.
        check("a requirement naming the option itself is not another mod",
              fomod::requiredMods("Required Main Files.",
                                  "Main files", "Required").isEmpty());
        check("nor is one naming its group",
              fomod::requiredMods("Requires the Core Files.",
                                  "Core Files", "Options").isEmpty());
        check("installer boilerplate is not a mod either",
              fomod::requiredMods("Required Main Package.",
                                  "Whatever", "Options").isEmpty());
        // The guard is an EXACT match on purpose: "SMIM" has to survive on an
        // option called "SMIM Rotor", so containment would be far too eager.
        check("an acronym inside the option name still counts",
              fomod::requiredMods(
                  "SMIM Rotor for Solitude Windmill. Required Static Mesh "
                  "Improvement Mod - SMIM by Brumbek.",
                  "SMIM Rotor", "SMIM Rotor")
                  .contains(QStringLiteral("SMIM")));

    }




    std::cout << "\n[an exclusive group offering alternative frameworks]\n";
    {
        // Producers of Skyrim, verbatim.
        const QStringList group{QStringLiteral("Don't Install"),
                                QStringLiteral("Container Distribution Framework"),
                                QStringLiteral("SkyPatcher")};
        using St = fomod::FrameworkChoice::State;

        // Neither installed: the pre-selected framework writes config nothing
        // reads, so the opt-out is the only honest answer.
        {
            const auto c = fomod::chooseFrameworkOption(
                group, {QStringLiteral("Base Object Swapper")});
            check("opt-out chosen when no framework is installed", c.index == 0);
            check("and neither framework is claimed present", !c.anyInstalled);
            check("each missing one is marked",
                  c.states[1] == St::Missing && c.states[2] == St::Missing);
        }
        // Exactly one installed: that fact decides, no preference involved.
        {
            const auto c = fomod::chooseFrameworkOption(
                group, {QStringLiteral("Container Distribution Framework")});
            check("the installed one is chosen", c.index == 1);
            check("without invoking the tiebreak", !c.brokeTie);
        }
        {
            const auto c = fomod::chooseFrameworkOption(
                group, {QStringLiteral("SkyPatcher")});
            check("and the other way round", c.index == 2 && !c.brokeTie);
        }
        // Both installed: either works, so a documented order breaks the tie
        // and the UI is told that is what happened.
        {
            const auto c = fomod::chooseFrameworkOption(
                group, {QStringLiteral("Container Distribution Framework"),
                        QStringLiteral("SkyPatcher")});
            check("a tie is broken toward the more depended-upon framework",
                  c.index == 2, QString::number(c.index));
            check("and reported as a tiebreak, not as a verdict", c.brokeTie);
        }
        // An acronym in the modlist still counts as the same framework.
        {
            const auto c = fomod::chooseFrameworkOption(
                group, {QStringLiteral("CDF")});
            check("installed under its acronym", c.index == 1);
        }

        // A group whose options name nothing identifiable is left entirely
        // alone - the rule that keeps ordinary option names quiet.
        {
            const auto c = fomod::chooseFrameworkOption(
                {QStringLiteral("Default LOD"),
                 QStringLiteral("Distant windows do not glow")},
                {QStringLiteral("SkyPatcher")});
            check("an ordinary exclusive group is not touched",
                  c.states.isEmpty() && c.index == -1);
        }
        // No opt-out offered and nothing installed: nothing safe to pick, so
        // the author's default is left alone rather than guessed at.
        {
            const auto c = fomod::chooseFrameworkOption(
                {QStringLiteral("Container Distribution Framework"),
                 QStringLiteral("SkyPatcher")}, {QStringLiteral("SkyUI")});
            check("no opt-out and nothing installed leaves the default alone",
                  c.index == -1);
            check("but both are still marked missing",
                  c.states[0] == St::Missing && c.states[1] == St::Missing);
        }
    }

    std::cout << "\n[downloading the build for the wrong game version]\n";
    {
        // The real Scrambled Bugs mod page.
        const QStringList page{
            QStringLiteral("Scrambled Bugs - Anniversary Edition (1.6.629.0 and later)"),
            QStringLiteral("Scrambled Bugs - Anniversary Edition (1.6.318.0 to 1.6.353.0)"),
            QStringLiteral("Scrambled Bugs - Special Edition (1.5.97.0 and earlier)"),
            QStringLiteral("Vendor Respawn Fix"),
            QStringLiteral("Script Effect Archetype Crash Fix")};

        // On an AE profile, grabbing the 1.5.97 build is the mistake to catch.
        check("the Special Edition build is flagged on an AE profile",
              fomod::betterRuntimeFile(page[2], page, RT::AE)
                  == page[0], fomod::betterRuntimeFile(page[2], page, RT::AE));
        check("and the newest AE build is the one offered",
              fomod::betterRuntimeFile(page[2], page, RT::AE)
                  .contains(QStringLiteral("1.6.629")));

        // The right build says nothing at all.
        check("the AE build on an AE profile is fine",
              fomod::betterRuntimeFile(page[0], page, RT::AE).isEmpty());
        check("the older AE build is also fine - still 1.6.x",
              fomod::betterRuntimeFile(page[1], page, RT::AE).isEmpty());

        // Mirrored for an SE profile.
        check("the AE build is flagged on an SE profile",
              fomod::betterRuntimeFile(page[0], page, RT::SE) == page[2]);

        // A file whose name carries no version marking is not a verdict.
        check("an unmarked optional file says nothing",
              fomod::betterRuntimeFile(page[3], page, RT::AE).isEmpty());
        // Neither is a page that offers only one kind.
        check("nothing to switch to means silence",
              fomod::betterRuntimeFile(
                  page[2], {page[2], page[3]}, RT::AE).isEmpty());
        // And a profile with no runtime (Morrowind, Oldrim) never fires.
        check("a game with no runtime split is unaffected",
              fomod::betterRuntimeFile(page[2], page, RT::None).isEmpty());
        check("empty inputs are safe",
              fomod::betterRuntimeFile(QString(), page, RT::AE).isEmpty()
                  && fomod::betterRuntimeFile(page[2], {}, RT::AE).isEmpty());
    }

    std::cout << "\n[an option that is a patch FOR another mod]\n";
    {
        // Verbatim from Lively Farms' "Patches" group, where every entry is
        // one of these and none uses the word "required".
        const auto caco = fomod::requiredMods(
            "A integration patch for Complete Alchemy and Cooking Overhaul.");
        check("the patched mod is found",
              caco.contains(QStringLiteral("Complete Alchemy and Cooking Overhaul")),
              caco.join(QLatin1Char('|')));

        check("a short name works",
              fomod::requiredMods("An integration patch for Last Seed.")
                  .contains(QStringLiteral("Last Seed")));
        check("an apostrophe survives",
              fomod::requiredMods("A patch for Ryn's Farms.")
                  .contains(QStringLiteral("Ryn's Farms")));
        check("\"compatibility with\" counts too",
              fomod::requiredMods("Adds compatibility with Patch for Purists.")
                  .contains(QStringLiteral("Patch for Purists")));

        // A name must not run past a full stop into the next sentence. This
        // is what produced "Favor Jobs Overhaul Use" and would have warned
        // about a mod of that name.
        const auto favor = fomod::requiredMods(
            "An integration patch for Favor Jobs Overhaul. Use it only if you "
            "have chosen the \"Standard version\" of Lively Farms.");
        check("the name stops at the full stop",
              favor.contains(QStringLiteral("Favor Jobs Overhaul")),
              favor.join(QLatin1Char('|')));
        check("and nothing from the next sentence leaks in",
              !favor.join(QLatin1Char('|')).contains(QStringLiteral("Use")),
              favor.join(QLatin1Char('|')));

        // Two mods joined by "and" are genuinely ambiguous, so both the long
        // reading and the part before the connector are offered and either may
        // match the modlist.
        const auto combo = fomod::requiredMods(
            "A integration patch for CACO and SunHelm Survival and Needs. "
            "Select only this patch if you have both CACO and SunHelm installed.");
        check("the connector form is kept whole",
              combo.contains(QStringLiteral("CACO and SunHelm Survival and Needs")),
              combo.join(QLatin1Char('|')));
        check("and the leading name alone is offered",
              combo.contains(QStringLiteral("CACO")), combo.join(QLatin1Char('|')));

        // Ordinary prose about this mod's own files must stay quiet, "for"
        // and all.
        check("a texture description is not a patch requirement",
              fomod::requiredMods(
                  "New textures for the corkbulb plant; replaces "
                  "tx_cork_bulb_01 and tx_cork_bulb_02").isEmpty());

        check("CACO resolves to its full name",
              mod_aliases::aliasesFor("CACO")
                  .contains(QStringLiteral("Complete Alchemy and Cooking Overhaul")));
    }

    std::cout << "\n[scene acronyms]\n";
    {
        check("an acronym finds the full name",
              mod_aliases::aliasesFor("SMIM")
                  .contains(QStringLiteral("Static Mesh Improvement Mod")));
        check("and the full name finds the acronym",
              mod_aliases::aliasesFor("Static Mesh Improvement Mod")
                  .contains(QStringLiteral("SMIM")));
        check("lookup ignores case", !mod_aliases::aliasesFor("smim").isEmpty());
        check("an unknown mod has no aliases",
              mod_aliases::aliasesFor("Forfeoranna Heim SSE").isEmpty());

        // Game acronyms are excluded on purpose: "SSE" is the game and appears
        // in half the mod names on that Nexus page.
        check("the game's own acronym is not a mod alias",
              mod_aliases::aliasesFor("SSE").isEmpty()
                  && mod_aliases::aliasesFor("AE").isEmpty());

        const auto ex = mod_aliases::expand({QStringLiteral("SMIM")});
        check("expand keeps the original first",
              !ex.isEmpty() && ex.first() == QStringLiteral("SMIM"));
        check("and adds the other spelling",
              ex.contains(QStringLiteral("Static Mesh Improvement Mod")));
        check("expand does not duplicate",
              mod_aliases::expand({QStringLiteral("SMIM"),
                                   QStringLiteral("Static Mesh Improvement Mod")})
                  .size() == 2);
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
//
// The screenshot that prompted the second round: the Glass Glowset box sat
// TICKED with "Glass Glowset is not installed in this modlist" written along
// its own label. Warning and then installing anyway is the manager arguing
// with itself, so the verdict now moves the tick - both ways.
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

    // `type` matters here in a way it did not before: the reported options
    // ship Recommended, which is what puts the tick on screen.
    auto withDesc = [](const QString &name, const QString &desc,
                       const QString &type = QStringLiteral("Optional")) {
        FomodPlugin p = wizardui_mkPlugin(name, type);
        p.description = desc;
        return p;
    };

    std::cout << "\n[compatibility option for a mod that is not installed]\n";
    {
        FomodGroup g = wizardui_mkGroup("SelectAny", {
            withDesc("Ashfall",       kAshfall, "Recommended"),
            withDesc("Ashfall (HD)",  kAshfall, "Recommended"),
            withDesc("Glass Glowset", kGlowset, "Recommended"),
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

        // The reported bug: all three shipped Recommended, so all three were
        // ticked underneath their own "not installed" warning.
        check("the Recommended tick is taken off Ashfall",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("and off the HD variant",
              !FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        check("and off Glass Glowset, the one in the screenshot",
              !FomodWizardTestHook::btn(w, 0, 0, 2)->isChecked());
        check("the tooltip says the tick was moved and how to put it back",
              FomodWizardTestHook::btn(w, 0, 0, 2)->toolTip()
                  .contains(QStringLiteral("Unticked because")),
              FomodWizardTestHook::btn(w, 0, 0, 2)->toolTip());
        // Unticked, not disabled: a mod installed outside the manager is not
        // in the modlist and the user is the only one who knows that.
        check("the option stays settable",
              FomodWizardTestHook::btn(w, 0, 0, 2)->isEnabled());
        delete w;
    }

    std::cout << "\n[the same options once the mod IS installed]\n";
    {
        // Both ship Optional, i.e. unticked, so a tick here can only have come
        // from the modlist verdict.
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
        check("the option for the installed mod is ticked",
              FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("and says which mod put it there",
              FomodWizardTestHook::btn(w, 0, 0, 0)->text().contains("Ashfall"),
              FomodWizardTestHook::btn(w, 0, 0, 0)->text());
        check("the option for the other absent mod still warns",
              FomodWizardTestHook::btn(w, 0, 0, 1)->text()
                  .contains("Glass Glowset is not installed"));
        check("and stays off",
              !FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        delete w;
    }

    std::cout << "\n[options that cite nothing stay silent]\n";
    {
        // "Normal Maps" matches no mod in the modlist either. Without a
        // citation there is no evidence, so it must draw nothing - this is
        // the guard against the warning becoming noise on ordinary options,
        // and now against the tick moving on ordinary options too.
        FomodGroup g = wizardui_mkGroup("SelectAny",
                                        { withDesc("1K", kNormals, "Recommended") });
        g.name = QStringLiteral("Normal Maps");
        FomodStep st;
        st.name   = QStringLiteral("Compatibility Options");
        st.groups = { g };
        auto *w = FomodWizardTestHook::build({ st });
        check("no warning without a citation",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->text()
                  .contains(QString::fromUtf8("⚠")));
        check("and the author's own default is left alone",
              FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
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
        // Exclusive: unticking one means picking another, which is the user's
        // call. SelectExactlyOne forces the first option on, and it stays on.
        check("the radio selection is not touched",
              FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        delete w;
    }

    std::cout << "\n[a stored choice does not put back a tick for a missing mod]\n";
    {
        // Re-installing the same FOMOD replays the previous run's choices. That
        // run may have been made against a modlist that HAD Glass Glowset; this
        // one does not, and the label says so, so the tick cannot come back.
        FomodGroup g = wizardui_mkGroup("SelectAny", {
            withDesc("Glass Glowset", kGlowset, "Recommended"),
        });
        g.name = QStringLiteral("Glass Glowset");
        FomodStep st;
        st.name   = QStringLiteral("Compatibility Options");
        st.groups = { g };
        auto *w = FomodWizardTestHook::build({ st }, QStringLiteral("0:0:0"));
        check("the prior tick loses to the modlist",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        delete w;
    }
}


// The reported case, end to end through the wizard: Grand Solitude's SMIM
// Rotor ships TICKED and needs a mod the user may not have.
static void wizardui_testRequiredModTogglesTheOption()
{
    const QString kDesc =
        "SMIM Rotor for Solitude Windmill. "
        "Required Static Mesh Improvement Mod - SMIM by Brumbek.";

    auto build = [&](const QStringList &installed) {
        FomodPlugin p = wizardui_mkPlugin("SMIM Rotor");
        p.description = kDesc;
        FomodGroup g = wizardui_mkGroup("SelectAny", { p });
        g.name = QStringLiteral("SMIM Rotor");
        FomodStep st;
        st.name   = QStringLiteral("Step 1 of 1");
        st.groups = { g };
        return FomodWizardTestHook::build({ st }, {}, installed);
    };

    std::cout << "\n[an option whose required mod is missing is unticked]\n";
    {
        auto *w = build({QStringLiteral("Alternate Start - Live Another Life"),
                         QStringLiteral("JK's Whiterun Outskirts")});
        auto *btn = FomodWizardTestHook::btn(w, 0, 0, 0);
        check("unticked", !btn->isChecked());
        check("and says which mod is missing",
              btn->text().contains(QStringLiteral("Static Mesh Improvement Mod")),
              btn->text());
        check("with a warning, not a recommendation",
              btn->text().contains(QString::fromUtf8("⚠")), btn->text());
        // The label stays short enough to read; the full sentence is a
        // tooltip. The long form ran off the end of the dialog.
        check("the label is short", btn->text().size() < 70, btn->text());
        check("and the tooltip carries the explanation",
              btn->toolTip().contains(QStringLiteral("unticked")), btn->toolTip());
        delete w;
    }

    std::cout << "\n[and ticked when it is installed]\n";
    {
        auto *w = build({QStringLiteral("Static Mesh Improvement Mod SE")});
        auto *btn = FomodWizardTestHook::btn(w, 0, 0, 0);
        check("ticked", btn->isChecked());
        check("and names the mod on the label",
              btn->text().contains(QStringLiteral("Static Mesh Improvement Mod")),
              btn->text());
        check("with the detail in the tooltip",
              btn->toolTip().contains(QStringLiteral("is installed")), btn->toolTip());
        delete w;
    }

    std::cout << "\n[the scene's acronym counts as the same mod]\n";
    {
        // Installed under its acronym only - the lookup has to bridge the two
        // spellings or it reports a dependency the user actually has.
        auto *w = build({QStringLiteral("SMIM")});
        check("ticked from the acronym alone",
              FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        delete w;
    }

    std::cout << "\n[a three-letter acronym is an acronym too]\n";
    {
        // SMIM passing proved nothing about the rule: it is four characters,
        // and the needle builder dropped anything shorter. Three-letter names
        // are ordinary here - BOS, MOP - and one installed under its acronym
        // alone came back missing, so the option was unticked and labelled as
        // needing a mod sitting right there in the modlist.
        FomodPlugin p = wizardui_mkPlugin("Swapper Config");
        p.description = QStringLiteral("Requires Base Object Swapper to work.");
        FomodGroup g = wizardui_mkGroup("SelectAny", { p });
        g.name = QStringLiteral("Swapper Config");
        FomodStep st;
        st.name   = QStringLiteral("Step 1 of 1");
        st.groups = { g };
        auto *w = FomodWizardTestHook::build({ st }, {},
                                             {QStringLiteral("BOS")});
        check("ticked from the three-letter acronym alone",
              FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked(),
              FomodWizardTestHook::btn(w, 0, 0, 0)->text());
        delete w;
    }

    std::cout << "\n[but a short needle still has to start the name]\n";
    {
        // The anchoring is what makes a three-letter needle safe to allow:
        // "BOS" may open a mod name, never sit inside one.
        FomodPlugin p = wizardui_mkPlugin("Swapper Config");
        p.description = QStringLiteral("Requires Base Object Swapper to work.");
        FomodGroup g = wizardui_mkGroup("SelectAny", { p });
        g.name = QStringLiteral("Swapper Config");
        FomodStep st;
        st.name   = QStringLiteral("Step 1 of 1");
        st.groups = { g };
        auto *w = FomodWizardTestHook::build(
            { st }, {}, {QStringLiteral("Skyrim BOS Patch Collection")});
        check("not ticked by an acronym buried mid-name",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked(),
              FomodWizardTestHook::btn(w, 0, 0, 0)->text());
        delete w;
    }


    std::cout << "\n[a FOMOD's own required entry is not a missing mod]\n";
    {
        // Grand Solitude's Required group holds one option, "Main files",
        // described as "Required Main Files." That parsed as a missing mod
        // called "Main Files" and warned on the one option with no choice.
        FomodPlugin p = wizardui_mkPlugin("Main files", "Required");
        p.description = QStringLiteral("Required Main Files.");
        FomodGroup g = wizardui_mkGroup("SelectAll", { p });
        g.name = QStringLiteral("Required");
        FomodStep st;
        st.name   = QStringLiteral("Step 1 of 1");
        st.groups = { g };
        auto *w = FomodWizardTestHook::build({ st });
        const QString text = FomodWizardTestHook::btn(w, 0, 0, 0)->text();
        check("no warning on the installer's own required files",
              !text.contains(QString::fromUtf8("⚠")), text);
        delete w;
    }

    std::cout << "\n[an option with no stated requirement is left alone]\n";
    {
        FomodPlugin p = wizardui_mkPlugin("Parallax meshes");
        p.description = QStringLiteral("Optional parallax meshes for the walls.");
        FomodGroup g = wizardui_mkGroup("SelectAny", { p });
        g.name = QStringLiteral("Optional");
        FomodStep st;
        st.name = QStringLiteral("Step 1 of 1");
        st.groups = { g };
        auto *w = FomodWizardTestHook::build({ st });
        const QString text = FomodWizardTestHook::btn(w, 0, 0, 0)->text();
        check("no verdict of any kind",
              !text.contains(QString::fromUtf8("⚠"))
                  && !text.contains(QStringLiteral("is installed")), text);
        delete w;
    }
}


// The reported case end to end: Producers of Skyrim ships a framework
// pre-selected, and with neither installed that writes config nothing reads.
static void wizardui_testFrameworkGroup()
{
    auto build = [](const QStringList &installed) {
        FomodGroup g = wizardui_mkGroup("SelectExactlyOne", {
            wizardui_mkPlugin("Don't Install"),
            wizardui_mkPlugin("Container Distribution Framework"),
            wizardui_mkPlugin("SkyPatcher"),
        });
        g.name = QStringLiteral("Orc Stronghold Blacksmiths");
        FomodStep st;
        st.name   = QStringLiteral("Step 1 of 1");
        st.groups = { g };
        return FomodWizardTestHook::build({ st }, {}, installed);
    };

    std::cout << "\n[no framework installed: the opt-out is selected]\n";
    {
        auto *w = build({QStringLiteral("Base Object Swapper")});
        check("Don't Install is picked",
              FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("the framework the FOMOD defaulted to is not",
              !FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        check("and it is marked missing",
              FomodWizardTestHook::btn(w, 0, 0, 1)->text()
                  .contains(QStringLiteral("not installed")),
              FomodWizardTestHook::btn(w, 0, 0, 1)->text());
        delete w;
    }

    std::cout << "\n[both installed: the tiebreak is explained, not asserted]\n";
    {
        auto *w = build({QStringLiteral("SkyPatcher"),
                         QStringLiteral("Container Distribution Framework")});
        check("SkyPatcher is picked",
              FomodWizardTestHook::btn(w, 0, 0, 2)->isChecked());
        const QString tip = FomodWizardTestHook::btn(w, 0, 0, 2)->toolTip();
        check("the tooltip says either would work",
              tip.contains(QStringLiteral("either would work")), tip);
        check("and does not claim the other is unstable",
              tip.contains(QStringLiteral("not because")), tip);
        delete w;
    }

    std::cout << "\n[only the one you have is picked]\n";
    {
        auto *w = build({QStringLiteral("Container Distribution Framework")});
        check("CDF chosen when it is the only one present",
              FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        delete w;
    }
}

static void run_fomod_wizard_ui()
{
    std::cout << "=== fomod_wizard_ui (buildUi) tests ===\n";

    wizardui_testFindFomodRoot();
    wizardui_testModlistVerdict();
    wizardui_testMissingCitedMod();
    wizardui_testRequiredModTogglesTheOption();
    wizardui_testFrameworkGroup();

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


// ===== BainWizard: the picker applies the verdicts =====
//
// Fills the friend seam declared in bainwizard.h. Packages are stuffed in
// directly, so none of this touches a filesystem; master evidence is covered
// against real TES3 headers in the bain_hint suite above.
struct BainWizardTestHook {
    static BainWizard *build(const QList<bain::Package> &pkgs,
                             const QString &prior = {},
                             const QStringList &installed = {},
                             const QString &ownModName = {},
                             const QSet<QString> &availablePlugins = {})
    {
        auto *w = new BainWizard(QStringLiteral("/tmp/nrv_bain_ui_test"), prior);
        w->m_packages              = pkgs;
        w->m_installedModNames     = installed;
        w->m_ownModName            = ownModName;
        w->m_availablePluginsLower = availablePlugins;
        w->buildUi();
        return w;
    }
    static QCheckBox  *box(BainWizard *w, int i) { return w->m_boxes.value(i); }
    static int         boxCount(BainWizard *w)   { return w->m_boxes.size(); }
    static QStringList chosen(BainWizard *w)     { return w->chosenNames(); }
    // isHidden(), not isVisible(): the dialog is never show()n here, so every
    // child is invisible regardless and isVisible() would pass vacuously.
    static bool        pickerRevealed(BainWizard *w)
                          { return w->m_scroll && !w->m_scroll->isHidden(); }
    static QWidget    *chooser(BainWizard *w)    { return w->m_chooser; }
};

static QList<bain::Package> bw_pkgs(const QStringList &names)
{
    QList<bain::Package> out;
    for (const QString &n : names) out.append({n, QString()});
    return out;
}

// The archive that prompted all this.
static const QStringList kShipwrecks = {
    QStringLiteral("00 Core"),
    QStringLiteral("01 Patch - Uncharted Artifacts"),
    QStringLiteral("02 Patch - TR Patch"),
    QStringLiteral("03 Patch - Of Pillows and Peril"),
    QStringLiteral("04 Patch - Lush Synthesis"),
};
static const QStringList kShipwrecksModlist = {
    QStringLiteral("Uncharted Artifacts"),
    QStringLiteral("Tamriel Rebuilt 25.08.12"),
};

static void bainui_testOaabShipwrecks()
{
    std::cout << "\n[OAAB Shipwrecks, judged against the real modlist]\n";
    auto *w = BainWizardTestHook::build(bw_pkgs(kShipwrecks), {},
                                        kShipwrecksModlist);
    check("five packages", BainWizardTestHook::boxCount(w) == 5);

    check("Core is left alone",
          BainWizardTestHook::box(w, 0)->isChecked());
    check("and keeps its name unchanged",
          BainWizardTestHook::box(w, 0)->text() == kShipwrecks[0],
          BainWizardTestHook::box(w, 0)->text());

    check("the Uncharted Artifacts patch stays ticked",
          BainWizardTestHook::box(w, 1)->isChecked());
    check("and says which mod it found",
          BainWizardTestHook::box(w, 1)->text().contains(
              QStringLiteral("Uncharted Artifacts ✓")),
          BainWizardTestHook::box(w, 1)->text());

    check("the TR patch stays ticked",
          BainWizardTestHook::box(w, 2)->isChecked(),
          BainWizardTestHook::box(w, 2)->text());

    check("Of Pillows and Peril is unticked",
          !BainWizardTestHook::box(w, 3)->isChecked());
    check("and says why",
          BainWizardTestHook::box(w, 3)->text().contains(
              QStringLiteral("Of Pillows and Peril is not installed")),
          BainWizardTestHook::box(w, 3)->text());
    check("Lush Synthesis is unticked",
          !BainWizardTestHook::box(w, 4)->isChecked());

    // Unticked, never disabled: a mod kept outside the manager is absent from
    // the modlist and only the user knows that.
    check("an unticked package is still settable",
          BainWizardTestHook::box(w, 3)->isEnabled());
    check("with a tooltip saying how to put it back",
          BainWizardTestHook::box(w, 3)->toolTip().contains(
              QStringLiteral("Tick it back")),
          BainWizardTestHook::box(w, 3)->toolTip());

    const QStringList chosen = BainWizardTestHook::chosen(w);
    check("only the three wanted packages are staged",
          chosen.size() == 3 && !chosen.contains(kShipwrecks[3])
                             && !chosen.contains(kShipwrecks[4]),
          chosen.join(QStringLiteral(", ")));
    delete w;
}

static void bainui_testBadgeNamesTheTarget()
{
    std::cout << "\n[the badge names the mod the package is for]\n";
    // Found in the real corpus: "02 OAAB Shipwrecks Patch" is answered through
    // the OAAB alias by whichever OAAB mod the modlist happens to list first.
    // The tick is right; naming that row on the label is not.
    auto *w = BainWizardTestHook::build(
        bw_pkgs({QStringLiteral("02 OAAB Shipwrecks Patch")}), {},
        {QStringLiteral("OAAB Juniper's Twin Lamps (quests)")});
    const QString label = BainWizardTestHook::box(w, 0)->text();
    check("the label names the package's own target",
          label.contains(QStringLiteral("OAAB Shipwrecks ✓")), label);
    check("and not the row that happened to answer",
          !label.contains(QStringLiteral("Juniper")), label);
    check("which is explained in the tooltip instead",
          BainWizardTestHook::box(w, 0)->toolTip().contains(
              QStringLiteral("Juniper")),
          BainWizardTestHook::box(w, 0)->toolTip());
    delete w;
}

static void bainui_testChooser()
{
    std::cout << "\n[the one-click chooser gives way to a recommendation]\n";
    {
        // A recommendation you cannot see is not one, and "Install everything"
        // force-ticks every box - so the list has to be on screen.
        auto *w = BainWizardTestHook::build(bw_pkgs(kShipwrecks), {},
                                            kShipwrecksModlist);
        check("the picker is open", BainWizardTestHook::pickerRevealed(w));
        check("and the compact chooser was never built",
              BainWizardTestHook::chooser(w) == nullptr);
        delete w;
    }
    {
        // A tick is a recommendation too. "This is for a mod you have" is the
        // answer to the question the picker exists to ask, and behind "Choose
        // packages..." the one archive you wondered about is the one that
        // stays silent.
        auto *w = BainWizardTestHook::build(
            bw_pkgs({QStringLiteral("00 Core"),
                     QStringLiteral("01 Icons for OpenMW SSQN")}), {},
            {QStringLiteral("OpenMW Skyrim Style Quest Notifications (SSQN)")});
        check("a positive verdict opens the list too",
              BainWizardTestHook::pickerRevealed(w));
        check("with no compact chooser in the way",
              BainWizardTestHook::chooser(w) == nullptr);
        check("and it says which mod it found",
              BainWizardTestHook::box(w, 1)->text().contains(
                  QStringLiteral("OpenMW SSQN ✓")),
              BainWizardTestHook::box(w, 1)->text());
        check("nothing is unticked by a positive",
              BainWizardTestHook::chosen(w).size() == 2);
        delete w;
    }
    {
        // Nothing to say: the one-click path is exactly as it was.
        auto *w = BainWizardTestHook::build(
            bw_pkgs({QStringLiteral("00 Core"),
                     QStringLiteral("01 Stratified Rocks")}), {},
            kShipwrecksModlist);
        check("no recommendation leaves the chooser in place",
              BainWizardTestHook::chooser(w) != nullptr);
        check("with the list still hidden",
              !BainWizardTestHook::pickerRevealed(w));
        check("and every package ticked",
              BainWizardTestHook::chosen(w).size() == 2);
        delete w;
    }
}

static void bainui_testPriorChoices()
{
    std::cout << "\n[a remembered selection versus what the modlist says]\n";
    {
        // The stored tick was made against a modlist that had the mod. That
        // modlist is gone, and the label beside it now says so.
        auto *w = BainWizardTestHook::build(
            bw_pkgs(kShipwrecks),
            QStringLiteral("00 Core;03 Patch - Of Pillows and Peril"),
            kShipwrecksModlist);
        check("a prior tick loses to the modlist",
              !BainWizardTestHook::box(w, 3)->isChecked(),
              BainWizardTestHook::box(w, 3)->text());
        delete w;
    }
    {
        // The reverse is NOT forced. This verdict is an inference off a folder
        // name, and a prior untick is a deliberate prune by the user.
        auto *w = BainWizardTestHook::build(
            bw_pkgs(kShipwrecks), QStringLiteral("00 Core"),
            kShipwrecksModlist);
        check("a prior untick survives an Installed verdict",
              !BainWizardTestHook::box(w, 1)->isChecked(),
              BainWizardTestHook::box(w, 1)->text());
        check("though the label still says the mod is there",
              BainWizardTestHook::box(w, 1)->text().contains(
                  QStringLiteral("Uncharted Artifacts ✓")));
        delete w;
    }
}

static void bainui_testSilenceLeavesUiAlone()
{
    std::cout << "\n[a package with no verdict is not touched at all]\n";
    const QStringList names = {QStringLiteral("00 Core"),
                               QStringLiteral("01 Stratified Rocks"),
                               QStringLiteral("02 - Atlas")};
    auto *w = BainWizardTestHook::build(bw_pkgs(names), {},
                                        kShipwrecksModlist);
    for (int i = 0; i < names.size(); ++i) {
        check("label is byte-identical to the folder name",
              BainWizardTestHook::box(w, i)->text() == names[i],
              BainWizardTestHook::box(w, i)->text());
        check("and it is ticked", BainWizardTestHook::box(w, i)->isChecked());
    }
    delete w;
}

static void run_bain_wizard_ui()
{
    std::cout << "=== bain wizard UI tests ===\n";
    bainui_testOaabShipwrecks();
    bainui_testBadgeNamesTheTarget();
    bainui_testChooser();
    bainui_testPriorChoices();
    bainui_testSilenceLeavesUiAlone();
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
    run_bain_hint();
    run_fomod_hint();
    run_fomod_wizard_ui();
    run_bain_wizard_ui();

    std::cout << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
