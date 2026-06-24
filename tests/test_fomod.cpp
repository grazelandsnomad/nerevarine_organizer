// FOMOD/BAIN path resolution, copy, script rescue, install promote, bain, wizard UI.
// Wizard tests need a QApplication (offscreen QPA, see main).

#include "fomod_path.h"
#include "fomod_copy.h"
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

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok, const QString &hint = QString())
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name;
        if (!hint.isNull() && !hint.isEmpty())
            std::cout << " (" << hint.toStdString() << ")";
        std::cout << "\n";
        ++s_failed;
    }
}

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

static void run_fomod_copy()
{
    std::cout << "=== fomod_copy ===\n";
    fomodcopy_testEmptySourceProducesNoDest();
    fomodcopy_testNonexistentSourceIsNoop();
    fomodcopy_testPopulatedSourceCopiesEverything();
    fomodcopy_testPatchHubMixedEmptyAndContent();
    fomodcopy_testCaseVariantFoldersMerge();
    fomodcopy_testMergeOverlayOverridesMainDownload();
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

// Friend hook into buildUi() and the private button tree.
struct FomodWizardTestHook {
    static FomodWizard *build(const QList<FomodStep> &steps,
                              const QString &prior = {},
                              const QStringList &installed = {})
    {
        auto *w = new FomodWizard(QStringLiteral("/tmp/nrv_fomod_ui_test"));
        w->m_steps            = steps;
        w->m_priorChoices     = prior;
        w->m_installedModNames = installed;
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

static void run_fomod_wizard_ui()
{
    std::cout << "=== fomod_wizard_ui (buildUi) tests ===\n";

    wizardui_testFindFomodRoot();

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
    run_fomod_wizard_ui();

    std::cout << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
