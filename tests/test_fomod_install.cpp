// tests/test_fomod_install.cpp
//
// Unit tests for fomod_install::promote.  Historical context:
//   · OAAB_Data once survived a FOMOD install with zero files and got
//     promoted anyway, resulting in the launcher silently losing its
//     OAAB_Data.esm.  The empty-fallback branch exists to prevent that
//     regression - these tests lock it in.
//   · The rename-to-Nexus-title branch runs against a numeric-suffix
//     collision path that has needed two fixes; same idea here.

#include "fomod_install.h"

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

static void testEmptyFomodOutputFallsBack()
{
    std::cout << "\n[empty FOMOD output → EmptyFallback]\n";
    QTemporaryDir dir;
    const QString modsDir    = dir.filePath("mods");
    const QString extractDir = dir.filePath("mods/archive-stuff");
    const QString fomodPath  = extractDir + "/fomod_install";
    const QString rawModPath = extractDir;
    QDir().mkpath(fomodPath);       // empty directory
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

static void testPromoteWithoutTitleReusesExtractDirName()
{
    std::cout << "\n[non-empty FOMOD + no title → lands at extractDir basename, wrapper removed]\n";
    QTemporaryDir dir;
    const QString modsDir    = dir.filePath("mods");
    const QString extractDir = dir.filePath("mods/archive-stuff");
    const QString fomodPath  = extractDir + "/fomod_install";
    QDir().mkpath(modsDir);
    writeFile(fomodPath + "/picked.esp", "content");
    // Simulate a sibling variant that was in the raw archive but the user
    // didn't select (the exact shape of the Caldera Priory bug in miniature).
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
          // After relocate, the new dir lives at exactly the path the raw
          // extractDir used to occupy (same basename collision allowance).
          // What must NOT exist: the pre-move fomod_install subdir.
          !QFileInfo::exists(extractDir + "/fomod_install"));
}

static void testPromoteWithTitleRenamesAndCleansWrapper()
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

static void testPromoteWithTitleCollisionAddsSuffix()
{
    std::cout << "\n[title collision → numeric suffix]\n";
    QTemporaryDir dir;
    const QString modsDir    = dir.filePath("mods");
    const QString extractDir = dir.filePath("mods/archive-stuff");
    const QString fomodPath  = extractDir + "/fomod_install";
    QDir().mkpath(modsDir);
    // Pre-occupy the target directory name.
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

static void testPromoteNonexistentFomodPathFallsBack()
{
    std::cout << "\n[missing fomod_install path → EmptyFallback]\n";
    // The wizard sometimes reports a path that never got created if the
    // install step bailed in a weird way.  Treat that as EmptyFallback so
    // the caller keeps the raw extract - same treatment as an empty dir,
    // so the OAAB_Data regression stays covered.
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

// Caldera-Priory regression: an archive with radio-exclusive patch folders
// (00 Core / 01 Rocky / 02 BCOM / 03 Aggressively-Compatible / 04 / 05) must
// only leave the user's PICKED variants on disk after a FOMOD install.
// Before this tightening, extractDir was kept as a "wrapper" when there was
// no Nexus title hint, so every unpicked variant stayed reachable as a
// sibling of fomod_install and collectDataFolders then promoted them all
// into openmw.cfg's managed section - the user ended up with three Rocky
// West Gash variants loaded at once.
static void testPromoteScrubsSiblingVariants()
{
    std::cout << "\n[Caldera-style sibling variants scrubbed on promote]\n";
    QTemporaryDir dir;
    const QString modsDir    = dir.filePath("mods");
    const QString extractDir = dir.filePath("mods/Caldera Priory-52898-2-2");
    const QString fomodPath  = extractDir + "/fomod_install";
    QDir().mkpath(modsDir);

    // Raw archive contents - all six FOMOD variants materialized as siblings
    // of fomod_install, the way a cold-extract would leave them.
    writeFile(extractDir + "/00 Core/Caldera Priory.ESP",             "core");
    writeFile(extractDir + "/01 Rocky West Gash Patch/rocky.esp",     "rocky");
    writeFile(extractDir + "/02 BCOM Rocky West Gash Patch/bcom.esp", "bcom-not-picked");
    writeFile(extractDir + "/03 Rocky WG Aggressively Compatible/agg.esp", "agg-not-picked");
    writeFile(extractDir + "/04 Remiros' Groundcover Patch/rem.esp",  "rem");
    writeFile(extractDir + "/05 Aesthesia Groundcover Patch/aes.esp", "aes");

    // What the wizard's applySelections() emitted - user kept Core, Rocky,
    // Remiros, Aesthesia; rejected BCOM and Aggressively-Compatible.
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
    check("UNPICKED BCOM variant is gone (the whole point of this fix)",
          !QFileInfo::exists(r.finalModPath + "/02 BCOM Rocky West Gash Patch/bcom.esp"));
    check("UNPICKED Aggressively-Compatible variant is gone",
          !QFileInfo::exists(r.finalModPath + "/03 Rocky WG Aggressively Compatible/agg.esp"));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::cout << "=== fomod_install::promote ===\n";
    testEmptyFomodOutputFallsBack();
    testPromoteWithoutTitleReusesExtractDirName();
    testPromoteWithTitleRenamesAndCleansWrapper();
    testPromoteWithTitleCollisionAddsSuffix();
    testPromoteNonexistentFomodPathFallsBack();
    testPromoteScrubsSiblingVariants();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
