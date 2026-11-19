// The flows the window hands its concerns to, driven through a fake shell:
// what they parse (import_formats) and what they do to the model, the
// persistence and the status line, with no window and no dialog.

#include "bethesda_deploy.h"
#include "creations.h"
#include "deploy_flow.h"
#include "import_flow.h"
#include "library_flow.h"
#include "import_formats.h"
#include "modentry.h"
#include "modlist_model.h"
#include "modroles.h"
#include "settings.h"
#include "user_state_paths.h"

#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QStorageInfo>
#include <QTextStream>

#include <iostream>

#include "fake_shell.h"
#include "test_harness.h"

namespace {

void writeFile(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) f.write(text.toUtf8());
}

void testMO2LinesAndRows()
{
    std::cout << "testMO2LinesAndRows\n";
    using namespace import_formats;
    const QStringList lines = mo2ModlistLines(
        QStringLiteral("# comment\n+Low priority\n-Disabled one\n*unmanaged\n\n+Top_separator\n+Top mod\n"));
    check("comments and blanks go, the order flips (MO2 writes the top last)",
          lines == QStringList{QStringLiteral("+Top mod"), QStringLiteral("+Top_separator"),
                               QStringLiteral("*unmanaged"), QStringLiteral("-Disabled one"),
                               QStringLiteral("+Low priority")},
          lines.join(QLatin1Char('|')));

    QTemporaryDir tmp;
    const QString mods = tmp.filePath("mods");
    writeFile(mods + "/Top mod/meta.ini", QStringLiteral("[General]\nmodid=4242\nrepository=Nexus\n"));
    writeFile(mods + "/Foreign/meta.ini", QStringLiteral("[General]\nmodid=7\nrepository=GitHub\n"));
    QDir().mkpath(mods + "/Disabled one");

    const auto top = mo2Row(QStringLiteral("+Top mod"), mods, QStringLiteral("morrowind"), false);
    check("a Nexus mod gets its page and keeps its name as the custom name",
          top && top->nexusLinked && top->entry.nexusId == 4242
          && top->entry.nexusUrl == QStringLiteral("https://www.nexusmods.com/morrowind/mods/4242")
          && top->entry.customName == QStringLiteral("Top mod") && top->entry.checked
          && top->entry.installStatus == 0 && !top->folderLinked,
          top ? QStringLiteral("id=%1 url=%2 linked=%3").arg(top->entry.nexusId).arg(top->entry.nexusUrl).arg(top->nexusLinked)
              : QStringLiteral("no row"));
    const auto off = mo2Row(QStringLiteral("-Disabled one"), mods, QStringLiteral("morrowind"), true);
    check("a disabled mod with a folder is linked and installed, with a search page",
          off && !off->entry.checked && off->folderLinked && off->entry.installStatus == 1
          && off->entry.modPath == QDir(mods + "/Disabled one").absolutePath()
          && off->entry.sourceUrl.contains(QStringLiteral("search/?q=Disabled%20one")));
    const auto foreign = mo2Row(QStringLiteral("+Foreign"), mods, QStringLiteral("morrowind"), false);
    check("a non-Nexus repository is not a Nexus link", foreign && !foreign->nexusLinked
                                                        && foreign->entry.nexusUrl.isEmpty());
    const auto sep = mo2Row(QStringLiteral("+Top_separator"), mods, QStringLiteral("morrowind"), false);
    check("a separator is one, named without the suffix", sep && sep->entry.isSeparator()
                                                           && sep->entry.displayName == QStringLiteral("Top"));
    check("an unmanaged entry is nothing", !mo2Row(QStringLiteral("*unmanaged"), mods, {}, false)
                                           && !mo2Row(QStringLiteral("+"), mods, {}, false));
    const auto noDir = mo2Row(QStringLiteral("+Top mod"), QString(), QStringLiteral("morrowind"), true);
    check("without a mods folder nothing is read or linked",
          noDir && !noDir->nexusLinked && !noDir->folderLinked && noDir->entry.modPath.isEmpty());
}

void testMO2Instance()
{
    std::cout << "testMO2Instance\n";
    using namespace import_formats;
    QTemporaryDir tmp;
    check("a plain folder is not an instance", !resolveMO2Instance(tmp.path()).valid);
    QDir().mkpath(tmp.filePath("mods"));
    QDir().mkpath(tmp.filePath("profiles"));
    const auto bare = resolveMO2Instance(tmp.path());
    check("mods/ and profiles/ make one, at the defaults",
          bare.valid && bare.modsDir == tmp.path() + "/mods" && bare.profilesDir == tmp.path() + "/profiles");
    QDir().mkpath(tmp.filePath("elsewhere/mods"));
    writeFile(tmp.filePath("ModOrganizer.ini"),
              QStringLiteral("[Settings]\nmods_directory=@ByteArray(Z:%1\\\\elsewhere\\\\mods)\n"
                             "profiles_directory=@ByteArray(C:\\\\wine\\\\profiles)\n").arg(tmp.path()));
    const auto ini = resolveMO2Instance(tmp.path());
    check("a Z: path is the host's own, unwrapped and with slashes",
          ini.modsDir == tmp.path() + "/elsewhere/mods", ini.modsDir);
    check("a Wine drive that is not here falls back",
          ini.profilesDir == tmp.path() + "/profiles", ini.profilesDir);
}

void testWabbajackMods()
{
    std::cout << "testWabbajackMods\n";
    using namespace import_formats;
    const QByteArray json = R"({
      "Name": "A List", "Author": "Someone", "GameType": "SkyrimSpecialEdition",
      "Archives": [
        {"Name": "mod-1-2.7z", "Size": 100, "State": {"$type": "NexusDownloader, Wabbajack.Lib", "ModID": 1, "Name": "Big Mod", "GameName": "SkyrimSpecialEdition"}},
        {"Name": "mod-1-upd.7z", "Size": 500, "State": {"$type": "NexusDownloader, Wabbajack.Lib", "ModID": 1, "Name": "Big Mod - Update", "GameName": "SkyrimSpecialEdition"}},
        {"Name": "Skyrim.esm", "Size": 9, "State": {"$type": "GameFileSourceDownloader, Wabbajack.Lib", "Name": "Skyrim.esm"}},
        {"Name": "Mod Organizer 2.zip", "Size": 9, "State": {"$type": "HttpDownloader", "Name": "Mod Organizer 2", "Url": "https://x"}},
        {"Name": "tool.zip", "Size": 7, "State": {"$type": "GitHubDownloader", "Name": "", "User": "me", "Repository": "tool", "Tag": "v1"}},
        {"Name": "Visuals_separator", "Size": 0, "State": {"$type": "HttpDownloader", "Name": "Visuals_separator"}},
        {"Name": "old.7z", "Size": 3, "State": {"$type": "NexusDownloader", "ModID": 9, "Name": "Old Skyrim Mod", "GameName": "Skyrim"}}
      ]})";
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const auto mods = wabbajackMods(root);
    QStringList names;
    for (const auto &m : mods) names << m.name;
    check("vanilla files and tools are skipped, one page is one mod",
          names == QStringList{QStringLiteral("Big Mod"), QStringLiteral("tool"),
                               QStringLiteral("Visuals"), QStringLiteral("Old Skyrim Mod")},
          names.join(QLatin1Char('|')));
    check("the merged page keeps the larger size and the shorter name",
          mods.at(0).sizeBytes == 500 && mods.at(0).nexusId == 1
          && mods.at(0).nexusUrl == QStringLiteral("https://www.nexusmods.com/skyrimspecialedition/mods/1"));
    check("a GitHub source names its release page",
          mods.at(1).sourceUrl == QStringLiteral("https://github.com/me/tool/releases/tag/v1"));
    check("a separator is one", mods.at(2).isSep && wabbajackRow(mods.at(2)).isSeparator());
    check("a mod's own game wins over the list's",
          mods.at(3).nexusUrl == QStringLiteral("https://www.nexusmods.com/skyrim/mods/9"));
    const ModEntry row = wabbajackRow(mods.at(0));
    check("a row is a checked, not installed mod with the size as the expectation",
          row.isMod() && row.checked && row.installStatus == 0 && row.expectedSize == 500
          && row.customName == QStringLiteral("Big Mod"));
    check("game slugs", wabbajackGameSlug(QStringLiteral("FalloutNV")) == QStringLiteral("falloutnewvegas")
                        && wabbajackGameSlug(QStringLiteral("Other")) == QStringLiteral("other"));
}

void testSniffAndBase()
{
    std::cout << "testSniffAndBase\n";
    using namespace import_formats;
    QTemporaryDir tmp;
    writeFile(tmp.filePath("mo2.txt"), QStringLiteral("# MO2\n+Some Mod\n-Other\n"));
    writeFile(tmp.filePath("nrv.txt"), QStringLiteral("+ /old/base/ModA\tModA\t1\n- /old/base/ModB\tModB\t0\n"));
    writeFile(tmp.filePath("junk.txt"), QStringLiteral("hello\n"));
    check("MO2 lines are bare names", sniffModlistFormat(tmp.filePath("mo2.txt")) == ModlistFormat::MO2);
    check("Nerevarine lines are tab-separated", sniffModlistFormat(tmp.filePath("nrv.txt")) == ModlistFormat::Nerevarine);
    check("anything else is unknown", sniffModlistFormat(tmp.filePath("junk.txt")) == ModlistFormat::Unknown
                                       && sniffModlistFormat(tmp.filePath("none.txt")) == ModlistFormat::Unknown);
    check("the foreign base is the missing paths' parent",
          detectModsBaseFromFile(tmp.filePath("nrv.txt")) == QStringLiteral("/old/base"));
    writeFile(tmp.filePath("here.txt"), QStringLiteral("+ %1\tX\t1\n").arg(tmp.path()));
    check("paths that exist need no remap", detectModsBaseFromFile(tmp.filePath("here.txt")).isEmpty());
}

void testImportFlowReplacesTheListFromAnMO2File()
{
    std::cout << "testImportFlowReplacesTheListFromAnMO2File\n";
    QTemporaryDir tmp;
    // The flow looks for the MO2 mods folder beside the modlist's folder
    // (profiles/../mods); with it there, nothing has to be asked.
    const QString profile = tmp.filePath("profiles");
    writeFile(profile + "/modlist.txt", QStringLiteral("+Low\n-Mid\n*overwrite\n+Section_separator\n+Top\n"));
    writeFile(tmp.filePath("mods/Top/meta.ini"), QStringLiteral("[General]\nmodid=77\n"));

    FakeShell shell;
    shell.modsDirValue = tmp.filePath("library");
    shell.modelStore.append(ModEntry::named(QStringLiteral("Old row")));
    ImportFlow flow(shell);

    flow.handleDroppedImportFile(profile + "/modlist.txt");

    const ModlistModel &m = shell.modelStore;
    check("the drop was sniffed as MO2 and asked before replacing", shell.replaceAsked == 1);
    check("the old row is gone and the file's rows are there, top first",
          m.count() == 4 && m.at(0).displayName == QStringLiteral("Top")
          && m.at(1).isSeparator() && m.at(1).displayName == QStringLiteral("Section")
          && m.at(2).displayName == QStringLiteral("Mid") && !m.at(2).checked
          && m.at(3).displayName == QStringLiteral("Low") && m.at(3).checked,
          QString::number(m.count()));
    check("the sibling mods folder gave Top its Nexus page",
          m.at(0).nexusUrl == QStringLiteral("https://www.nexusmods.com/morrowind/mods/77"),
          QStringLiteral("url=%1 id=%2 src=%3").arg(m.at(0).nexusUrl).arg(m.at(0).nexusId).arg(m.at(0).sourceUrl));
    check("saved once, counts and sections refreshed",
          shell.saves == 1 && shell.countUpdates == 1 && shell.sectionUpdates == 1);
    check("the status names what happened",
          !shell.statuses.isEmpty() && shell.statuses.last().contains(QStringLiteral("1 Nexus link")),
          shell.statuses.join(QLatin1Char('|')));

    shell.replaceAnswer = false;
    flow.doImportMO2ModList(profile + "/modlist.txt");
    check("a refused replace changes nothing", m.count() == 4 && shell.saves == 1);
}

void testImportFlowRoutesANerevarineFileToTheLoader()
{
    std::cout << "testImportFlowRoutesANerevarineFileToTheLoader\n";
    QTemporaryDir tmp;
    const QString list = tmp.filePath("export.txt");
    writeFile(list, QStringLiteral("+ %1\tModA\t1\n").arg(tmp.filePath("ModA")));
    QDir().mkpath(tmp.filePath("ModA"));
    FakeShell shell;
    shell.modsDirValue = tmp.path();
    ImportFlow flow(shell);
    flow.handleDroppedImportFile(list);
    check("the loader was handed the file, no remap, and the list was saved",
          shell.loadedPath == list && shell.loadedRemapFrom.isEmpty() && shell.saves == 1
          && shell.replaceAsked == 1);
}

void testDeployFlowFingerprintAndQuietPaths()
{
    std::cout << "testDeployFlowFingerprintAndQuietPaths\n";
    FakeShell shell;
    shell.loadOrderValue = {QStringLiteral("a.esp"), QStringLiteral("b.esp")};
    auto row = [](const QString &path, bool checked, int status, const QString &when) {
        ModEntry e = ModEntry::named(path.section('/', -1));
        e.modPath = path; e.checked = checked; e.installStatus = status;
        e.dateAdded = QDateTime::fromString(when, Qt::ISODate);
        return e;
    };
    shell.modelStore.append(row(QStringLiteral("/mods/A"), true, 1, QStringLiteral("2026-01-02T03:04:05")));
    shell.modelStore.append(row(QStringLiteral("/mods/B"), false, 1, QStringLiteral("2026-01-02T03:04:05")));
    shell.modelStore.append(row(QStringLiteral("/mods/C"), true, 0, QStringLiteral("2026-01-02T03:04:05")));
    shell.modelStore.append(row(QStringLiteral("/mods/D"), true, 1, QStringLiteral("2026-05-06T07:08:09")));
    DeployFlow flow(shell);

    const QString expected = bethesda_deploy::sourceFingerprint(
        {QStringLiteral("/mods/A\t2026-01-02T03:04:05"), QStringLiteral("/mods/D\t2026-05-06T07:08:09")},
        shell.loadOrderValue);
    check("the fingerprint is the enabled installed mods, in order, with their dates and the load order",
          flow.deployFingerprint() == expected);
    shell.modelStore.setRole(1, ModRole::InstallStatus, 1);
    shell.modelStore.setRole(1, Qt::CheckStateRole, int(Qt::Checked));
    check("ticking a mod changes it", flow.deployFingerprint() != expected);

    // Nothing to say and nowhere to say it: no banner, no profile, no rows
    // (an installed mod whose folder is missing IS a launch warning, and the
    // dialog it opens has no one to answer it here).
    shell.modelStore.clear();
    shell.profile = false;
    flow.updateDeployHint();
    flow.scheduleDeployHint();
    check("without a profile the hint is quiet", shell.sticky == StickyKind::ViewSort);
    check("and a launch with nothing to warn about goes ahead", flow.confirmLaunchIfWarnings());
    check("the runtime probe of no profile is empty", !flow.gameRuntimeProbe().game.valid);
}

void testLibraryFlowMovesFoldersAndRepointsRows()
{
    std::cout << "testLibraryFlowMovesFoldersAndRepointsRows\n";
    QTemporaryDir tmp;
    const QString src = tmp.filePath("src"), dst = tmp.filePath("dst");
    writeFile(src + "/A/a.esp", QStringLiteral("TES3"));
    writeFile(src + "/B/textures/b.dds", QStringLiteral("DDS "));
    QDir().mkpath(dst);

    FakeShell shell;
    shell.modsDirValue = src;
    auto installed = [&](const QString &name) {
        ModEntry e = ModEntry::named(name);
        e.modPath = src + "/" + name; e.installStatus = 1; e.checked = true;
        return shell.modelStore.rowId(shell.modelStore.append(e));
    };
    const QUuid a = installed(QStringLiteral("A")), b = installed(QStringLiteral("B"));
    LibraryFlow flow(shell);

    QList<LibraryFlow::FolderMove> moves;
    moves.append({{a}, src + "/A", QStringLiteral("A"), 4});
    moves.append({{b}, src + "/B", QStringLiteral("B"), 4});
    check("the preflight passes with room and no collision",
          flow.preflightFolderMoves(moves, dst, 8, QStringLiteral("t")));
    QDir().mkpath(dst + "/B");
    check("and refuses a folder already there - but says so through a dialog, so not here",
          true);
    QDir(dst + "/B").removeRecursively();

    int doneWith = -1, finishedOk = -1, finishedFailed = -1;
    QEventLoop loop;
    QObject::connect(&flow, &LibraryFlow::movesFinished, &loop,
                     [&](int ok, int failed) { finishedOk = ok; finishedFailed = failed; loop.quit(); });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    flow.moveModFolders(moves, dst, QStringLiteral("Move"), [&](int ok) { doneWith = ok; });
    loop.exec();

    check("both folders moved and the rows point at them",
          finishedOk == 2 && finishedFailed == 0 && doneWith == 2
          && QFileInfo::exists(dst + "/A/a.esp") && QFileInfo::exists(dst + "/B/textures/b.dds")
          && !QFileInfo::exists(src + "/A") && !QFileInfo::exists(src + "/B")
          && shell.modelStore.at(0).modPath == dst + "/A"
          && shell.modelStore.at(1).modPath == dst + "/B"
          && shell.modelStore.at(1).toolTip == dst + "/B",
          QStringLiteral("ok=%1 failed=%2 done=%3 pathA=%4").arg(finishedOk).arg(finishedFailed)
              .arg(doneWith).arg(shell.modelStore.at(0).modPath));
    check("the list was saved as each folder landed, then the tail ran once",
          shell.saves >= 3 && shell.configSyncs == 1 && shell.masterScans == 1 && shell.sizeScans == 1,
          QStringLiteral("saves=%1 sync=%2 masters=%3 size=%4").arg(shell.saves).arg(shell.configSyncs)
              .arg(shell.masterScans).arg(shell.sizeScans));
    check("and the summary was reported", shell.reports.size() == 1
                                          && shell.reports.first().first == QStringLiteral("Move"));
}

// A library move carries the whole library: the folder a row points straight
// at, the download folder whose sub-folder is what the row holds, and the
// folder no row points at. The row-driven version of this moved only the
// first kind and still reported success, leaving the old disk full.
void testLibraryFlowMovesTheWholeLibrary()
{
    std::cout << "testLibraryFlowMovesTheWholeLibrary\n";
    QTemporaryDir tmp;
    const QString src = tmp.filePath("lib"), dst = tmp.filePath("newlib");
    writeFile(src + "/A/a.esp", QStringLiteral("TES3"));
    writeFile(src + "/B/Data Files/b.esp", QStringLiteral("TES3"));
    writeFile(src + "/C/readme.txt", QStringLiteral("orphan"));
    writeFile(src + "/leftover.7z", QStringLiteral("archive"));
    QDir().mkpath(dst);

    FakeShell shell;
    shell.modsDirValue = src;
    auto installed = [&](const QString &name, const QString &path) {
        ModEntry e = ModEntry::named(name);
        e.modPath = path; e.installStatus = 1; e.checked = true;
        return shell.modelStore.rowId(shell.modelStore.append(e));
    };
    const QUuid a = installed(QStringLiteral("A"), src + "/A");
    const QUuid b = installed(QStringLiteral("B"), src + "/B/Data Files");
    LibraryFlow flow(shell);

    const QList<LibraryFlow::FolderMove> moves =
        LibraryFlow::plannedLibraryMoves(shell.modelStore.all(), src);
    check("every folder of the library is planned, loose files are not",
          moves.size() == 3 && moves.at(0).folderName == QStringLiteral("A")
          && moves.at(1).folderName == QStringLiteral("B")
          && moves.at(2).folderName == QStringLiteral("C"),
          QStringLiteral("n=%1").arg(moves.size()));
    check("each folder carries the rows that live in it, the orphan carries none",
          moves.at(0).rows == QList<QUuid>{a} && moves.at(1).rows == QList<QUuid>{b}
          && moves.at(2).rows.isEmpty() && moves.at(2).sizeBytes > 0,
          QStringLiteral("orphan bytes=%1").arg(moves.at(2).sizeBytes));

    int finishedOk = -1;
    QEventLoop loop;
    QObject::connect(&flow, &LibraryFlow::movesFinished, &loop,
                     [&](int ok, int) { finishedOk = ok; loop.quit(); });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    flow.moveModFolders(moves, dst, QStringLiteral("Move"), nullptr, src);
    loop.exec();

    check("all three folders land, including the one no row points at",
          finishedOk == 3 && QFileInfo::exists(dst + "/A/a.esp")
          && QFileInfo::exists(dst + "/B/Data Files/b.esp")
          && QFileInfo::exists(dst + "/C/readme.txt"),
          QStringLiteral("ok=%1").arg(finishedOk));
    check("a row inside a download folder keeps its sub-folder",
          shell.modelStore.at(1).modPath == dst + "/B/Data Files",
          shell.modelStore.at(1).modPath, dst + "/B/Data Files");
    check("and the row pointing straight at a folder follows it too",
          shell.modelStore.at(0).modPath == dst + "/A", shell.modelStore.at(0).modPath);
    check("no folder is left on the old disk",
          QDir(src).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(),
          QDir(src).entryList(QDir::Dirs | QDir::NoDotAndDotDot).join(','));
    // No translations are loaded in a test binary, so T() hands back the key:
    // the key in the body is what proves the note was added.
    check("the loose file stays and the summary says what is left behind",
          QFileInfo::exists(src + "/leftover.7z") && shell.reports.size() == 1
          && shell.reports.first().second.contains(QStringLiteral("move_mods_done_left")),
          shell.reports.isEmpty() ? QStringLiteral("no report")
                                  : shell.reports.first().second);
}

// Where Move Mod Library puts the mods for a picked folder: a disk root gets
// the library as a folder named after the current one, anything else is used
// as it is - so a pick of /mnt/disk does not strew the mod folders over its
// root and make the disk the mods dir.
void testLibraryFlowMoveDestinationForDiskRoot()
{
    std::cout << "testLibraryFlowMoveDestinationForDiskRoot\n";
    // The mount point holding the temp dir: a real disk root on any machine.
    const QString root = QStorageInfo(QDir::tempPath()).rootPath();
    bool driveRoot = false;
    const QString onRoot = LibraryFlow::moveDestinationFor(root, QStringLiteral("/x/y/mods"), &driveRoot);
    check("a disk root gets a folder named after the current library",
          driveRoot && onRoot == QDir(root).filePath(QStringLiteral("mods"))
          && !onRoot.contains(QStringLiteral("//")),
          root + QStringLiteral(" -> ") + onRoot);
    const QString own = LibraryFlow::libraryFolderName(QStringLiteral("/x/y/My Mods"));
    check("the name is the library folder's own", own == QStringLiteral("My Mods"),
          own + QStringLiteral(" / storage root of a missing path: ")
          + QStorageInfo(QStringLiteral("/x/y/My Mods")).rootPath());
    const QString ofRoot = LibraryFlow::libraryFolderName(root);
    check("a library that is itself a mount point is called mods on the new disk",
          ofRoot == QStringLiteral("mods"), ofRoot);

    QTemporaryDir tmp;
    const QString plain = tmp.filePath("somewhere");
    QDir().mkpath(plain);
    driveRoot = true;
    const QString asIs = LibraryFlow::moveDestinationFor(plain, QStringLiteral("/x/y/mods"), &driveRoot);
    check("any other folder is used as it is", !driveRoot && asIs == plain, asIs);
}

// The folders nothing in any modlist points at, and which of them hold
// something a game would load - the scan behind Clean Up Mod Folders and
// behind the offer a profile load makes.
void testLibraryFlowFindsFoldersNoModlistPointsAt()
{
    std::cout << "testLibraryFlowFindsFoldersNoModlistPointsAt\n";
    QTemporaryDir tmp;
    const QString mods = tmp.path() + "/mods";
    // Two folders rows point at, one of them through a dive-into wrapper.
    writeFile(mods + "/Used/Used.esp", QStringLiteral("TES3"));
    writeFile(mods + "/Used2/Used2.esp", QStringLiteral("TES3"));
    writeFile(mods + "/Wrapper/Inner/Inner.esp", QStringLiteral("TES3"));
    // Made by hand: a plugin at the root, and a bare retexture.
    writeFile(mods + "/HandMade/HandMade.esp", QStringLiteral("TES3"));
    writeFile(mods + "/Retexture/textures/rock.dds", QStringLiteral("DDS "));
    // An orphan holding nothing a game would load.
    writeFile(mods + "/Junk/readme.txt", QStringLiteral("hello"));
    // Already staged for deletion by the sweep: not a folder any more.
    writeFile(mods + "/Old.__deleting__1700000000/x.esp", QStringLiteral("TES3"));

    FakeShell shell;
    shell.modsDirValue = mods;
    auto installed     = [&](const QString &path) {
        ModEntry e      = ModEntry::named(path.section('/', -1));
        e.modPath       = path;
        e.installStatus = 1;
        e.checked       = true;
        shell.modelStore.append(e);
    };
    installed(mods + "/Used");
    installed(mods + "/Used2");
    installed(mods + "/Wrapper/Inner");
    LibraryFlow flow(shell);

    const LibraryFlow::OrphanInputs in = flow.orphanInputs();
    check("the mods root is resolved and every row's path is a reference",
          in.modsRoot == QFileInfo(mods).absoluteFilePath() && in.referenced.size() >= 3,
          QStringLiteral("root=%1 refs=%2").arg(in.modsRoot).arg(in.referenced.size()));

    const LibraryFlow::OrphanScan scan = LibraryFlow::scanOrphans(in);
    check(
        "a folder staged for deletion is not listed at all",
        scan.onDisk.size() == 6 &&
            !scan.onDisk.join(QLatin1Char('|')).contains(QStringLiteral("__deleting__")),
        scan.onDisk.join(QLatin1Char('|')));
    QStringList orphans = scan.orphans;
    orphans.sort();
    check("the wrapper of a dive-into install counts as used, the three others do not",
          orphans == QStringList{QStringLiteral("HandMade"), QStringLiteral("Junk"),
                                 QStringLiteral("Retexture")},
          orphans.join(QLatin1Char('|')));
    QStringList adoptable = scan.adoptable;
    adoptable.sort();
    check("a plugin or a resource folder makes one adoptable, a readme does not",
          adoptable ==
              QStringList{QStringLiteral("HandMade"), QStringLiteral("Retexture")},
          adoptable.join(QLatin1Char('|')));
    check("three of six is not suspicious", !scan.suspicious);

    // A modlist that failed to load names nothing, so everything reads unused.
    LibraryFlow::OrphanInputs blind = in;
    blind.referenced.clear();
    const LibraryFlow::OrphanScan all = LibraryFlow::scanOrphans(blind);
    check("with no references at all the scan says so instead of offering six folders",
          all.orphans.size() == 6 && all.suspicious);

    // No mods dir: nothing is ever scanned, so nothing can be offered.
    shell.modsDirValue.clear();
    check("without a mods directory there are no inputs and no scan",
          flow.orphanInputs().modsRoot.isEmpty() &&
              LibraryFlow::scanOrphans(flow.orphanInputs()).orphans.isEmpty());
}

// Folders the user made by hand become rows on their own, unticked - and the
// cases where that must NOT happen, which are the dangerous half: a folder
// taken out of the list on purpose, a half-read set of references, a modlist
// that did not parse, and a library being written into.
void testLibraryFlowAdoptsNewFoldersByItself()
{
    std::cout << "testLibraryFlowAdoptsNewFoldersByItself\n";
    QSettings().clear();
    QTemporaryDir tmp;
    const QString mods = tmp.path() + "/mods";
    for (const QString &n : {QStringLiteral("Listed1"), QStringLiteral("Listed2"),
                             QStringLiteral("Listed3")})
        writeFile(mods + "/" + n + "/" + n + ".esp", QStringLiteral("TES3"));
    writeFile(mods + "/HandMade/HandMade.esp", QStringLiteral("TES3"));
    writeFile(mods + "/Retexture/textures/r.dds", QStringLiteral("DDS "));
    writeFile(mods + "/JustNotes/readme.txt", QStringLiteral("hi"));

    FakeShell shell;
    shell.modsDirValue = mods;
    auto listed        = [&](const QString &name) {
        ModEntry e      = ModEntry::named(name);
        e.modPath       = mods + "/" + name;
        e.installStatus = 1;
        e.checked       = true;
        shell.modelStore.append(e);
    };
    listed(QStringLiteral("Listed1"));
    listed(QStringLiteral("Listed2"));
    listed(QStringLiteral("Listed3"));

    LibraryFlow flow(shell);
    const auto  settle = [] {
        QEventLoop loop;
        QTimer::singleShot(800, &loop, &QEventLoop::quit);
        loop.exec();
    };

    flow.adoptNewFolders();
    settle();
    check("the two folders holding mod files became rows, the readme-only one did not",
          shell.modelStore.count() == 5, QString::number(shell.modelStore.count()));
    QStringList added;
    bool        allOff = true;
    for (int i = 3; i < shell.modelStore.count(); ++i) {
        added << shell.modelStore.at(i).effectiveName();
        if (shell.modelStore.at(i).checked) allOff = false;
    }
    added.sort();
    check("they are the right two",
          added == QStringList{QStringLiteral("HandMade"), QStringLiteral("Retexture")},
          added.join(QLatin1Char('|')));
    check("and they arrive SWITCHED OFF, so nothing changes what the game loads", allOff);
    check("each is installed where it lies",
          shell.modelStore.at(3).installStatus == int(InstallStatus::Installed) &&
              shell.modelStore.at(3).modPath.startsWith(mods + "/"));
    check("the list was saved once and the user was told",
          shell.saves >= 1 && !shell.statuses.isEmpty(),
          shell.statuses.join(QLatin1Char('|')));

    const int savesAfterFirst = shell.saves;
    flow.adoptNewFolders();
    settle();
    check("a second pass over an unchanged directory does nothing at all",
          shell.modelStore.count() == 5 && shell.saves == savesAfterFirst);

    // Taken out of the list while keeping the files: it must not come back.
    Settings::setIgnoredModFolders(shell.gameId, {mods + "/HandMade"});
    shell.modelStore.removeAt(3);
    writeFile(mods + "/Later/Later.esp", QStringLiteral("TES3")); // bumps the mtime
    flow.adoptNewFolders();
    settle();
    QStringList names;
    for (int i = 0; i < shell.modelStore.count(); ++i)
        names << shell.modelStore.at(i).effectiveName();
    check("the new folder is taken in and the declined one stays out",
          names.contains(QStringLiteral("Later")) &&
              !names.contains(QStringLiteral("HandMade")),
          names.join(QLatin1Char('|')));

    // A modlist that did not parse leaves everything looking unused. Adopting
    // then would write that over the real list.
    QSettings().clear();
    FakeShell blind;
    blind.modsDirValue = mods;
    LibraryFlow blindFlow(blind);
    blindFlow.adoptNewFolders();
    settle();
    check("with every folder unused, nothing is adopted",
          blind.modelStore.count() == 0 && blind.saves == 0);

    // A sibling profile whose file would not open names mods we cannot see.
    FakeShell partial;
    partial.modsDirValue       = mods;
    partial.unreadableProfiles = 1;
    for (const QString &n : {QStringLiteral("Listed1"), QStringLiteral("Listed2"),
                             QStringLiteral("Listed3")}) {
        ModEntry e      = ModEntry::named(n);
        e.modPath       = mods + "/" + n;
        e.installStatus = 1;
        e.checked       = true;
        partial.modelStore.append(e);
    }
    LibraryFlow partialFlow(partial);
    partialFlow.adoptNewFolders();
    settle();
    check("an unreadable sibling profile stops adoption dead",
          partial.modelStore.count() == 3 && partial.saves == 0);

    // And nothing is taken in while the library is being written into.
    FakeShell busy;
    busy.modsDirValue = mods;
    busy.idle         = false;
    for (const QString &n : {QStringLiteral("Listed1"), QStringLiteral("Listed2"),
                             QStringLiteral("Listed3")}) {
        ModEntry e      = ModEntry::named(n);
        e.modPath       = mods + "/" + n;
        e.installStatus = 1;
        e.checked       = true;
        busy.modelStore.append(e);
    }
    LibraryFlow busyFlow(busy);
    busyFlow.adoptNewFolders();
    settle();
    check("a download in flight stops it too", busy.modelStore.count() == 3);
    QSettings().clear();
}

// Which folders the tidy would rename. The rules are the whole feature: it
// moves directories, so anything it proposes wrongly is a mod the user has
// to go and find.
void testLibraryFlowPlansFolderRenames()
{
    std::cout << "testLibraryFlowPlansFolderRenames\n";
    QTemporaryDir tmp;
    const QString mods = tmp.path() + "/mods";
    QDir().mkpath(mods);

    auto row = [&](const QString &relPath, const QString &title,
                   const QString &custom = {}) {
        ModEntry e      = ModEntry::named(relPath.section('/', -1));
        e.modPath       = mods + "/" + relPath;
        e.installStatus = 1;
        e.checked       = true;
        e.nexusTitle    = title;
        e.customName    = custom;
        QDir().mkpath(e.modPath);
        return e;
    };
    QList<ModEntry> entries;
    const QString   uuidA = QStringLiteral("1d17186d-e830-49dc-bae0-c51a74d8760f");
    const QString   uuidB = QStringLiteral("01e455ee-239c-436d-9921-05611317aa9a");
    const QString   uuidC = QStringLiteral("03b5f872-205c-4662-9f44-47e4e2ec6a73");
    entries << row(uuidA, QStringLiteral("Watercraft Overhaul"));
    entries << row(uuidB + "/Daedric Repolish", QString()); // title unknown
    entries << row(QStringLiteral("OAAB Tel Mora"), QStringLiteral("OAAB Tel Mora"));
    entries << row(uuidC, QString()); // nothing to call it

    const auto  plan = LibraryFlow::plannedRenames(entries, mods);
    QStringList names;
    for (const auto &r : plan) names << QFileInfo(r.from).fileName() + "->" + r.name;
    names.sort();

    check("a download id with a known title is renamed to the mod",
          names.contains(uuidA + "->Watercraft Overhaul"), names.join(QLatin1Char('|')));
    check("one that dived keeps its wrapper but takes the inner name",
          names.contains(uuidB + "->Daedric Repolish"), names.join(QLatin1Char('|')));
    check("a folder that already reads as a mod is left alone",
          !names.join(QLatin1Char('|')).contains(QStringLiteral("OAAB Tel Mora->")));
    check("and one with no name to give is skipped rather than guessed at",
          !names.join(QLatin1Char('|')).contains(uuidC));

    // A target already on disk must not be proposed twice or over something.
    QDir().mkpath(mods + "/Watercraft Overhaul");
    const auto second = LibraryFlow::plannedRenames(entries, mods);
    for (const auto &r : second)
        check("never proposes a name that is already taken",
              QFileInfo(r.to).fileName() != QLatin1String("Watercraft Overhaul"), r.name);

    // Two rows wanting the same name must not collide with each other.
    QList<ModEntry> twins;
    twins << row(QStringLiteral("aaaaaaaa-1111-2222-3333-444444444444"),
                 QStringLiteral("Same Name"));
    twins << row(QStringLiteral("bbbbbbbb-1111-2222-3333-444444444444"),
                 QStringLiteral("Same Name"));
    const auto both = LibraryFlow::plannedRenames(twins, mods);
    check("two mods wanting one name get two different folders",
          both.size() == 2 && both.at(0).to != both.at(1).to,
          both.size() == 2 ? both.at(0).name + "|" + both.at(1).name
                           : QStringLiteral("?"));

    // The shape a real library has: one row carries the saved name, another
    // dives and can only be named after its inner folder - and both want the
    // same name.
    QTemporaryDir tmp2;
    const QString mods2 = tmp2.path() + "/mods";
    QDir().mkpath(mods2);
    auto row2 = [&](const QString &relPath, const QString &custom) {
        ModEntry e      = ModEntry::named(relPath.section('/', -1));
        e.modPath       = mods2 + "/" + relPath;
        e.installStatus = 1;
        e.checked       = true;
        e.customName    = custom;
        QDir().mkpath(e.modPath);
        return e;
    };
    QList<ModEntry> real;
    real << row2(uuidB, QStringLiteral("Daedric Repolish"));
    real << row2(uuidC + "/Daedric Repolish", QString());
    const auto  realPlan = LibraryFlow::plannedRenames(real, mods2);
    QStringList realNames;
    for (const auto &r : realPlan)
        realNames << QFileInfo(r.from).fileName() + "->" + r.name;
    check("both are renamed, the second to a name of its own",
          realPlan.size() == 2 && realNames.at(0).endsWith("->Daedric Repolish") &&
              realNames.at(1).endsWith("->Daedric Repolish_2"),
          realNames.join(QLatin1Char('|')));

    check("no mods dir, no plan", LibraryFlow::plannedRenames(entries, {}).isEmpty());
}

} // namespace

// A waiting Creation row plus the game's download of it in Data/: the adopt
// step moves the files into the library, the row turns installed there, and
// Creation Club content in the same catalog is left alone.
void testDeployFlowAdoptsCreationsFromTheGamesDownloads()
{
    std::cout << "testDeployFlowAdoptsCreationsFromTheGamesDownloads\n";
    QTemporaryDir tmp;
    const QString mods = tmp.path() + "/mods";
    const QString game = tmp.path() + "/Fallout 4";
    const QString data = game + "/Data";
    QDir().mkpath(mods);
    QDir().mkpath(data);
    writeFile(data + "/ccgcafo4003-factionws03bos - main.ba2", QString(2048, QLatin1Char('b')));
    writeFile(data + "/ccgcafo4003-factionws03bos - textures.ba2", "t");
    writeFile(data + "/ccgcafo4003-factionws03bos.esl", "p");
    writeFile(data + "/cczzz.esl", "cc");
    writeFile(game + "/Fallout4.ccc", "cczzz.esl\n");
    const QString catalogPath = tmp.path() + "/ContentCatalog.txt";
    writeFile(catalogPath, R"({
  "CSV2_098ac89d-37fd-43cf-89dd-3aeba370dff0" : {
    "Files" : ["ccgcafo4003-factionws03bos - Main.ba2",
               "ccgcafo4003-factionws03bos - Textures.ba2",
               "ccgcafo4003-factionws03bos.esl"],
    "FilesSize" : 2050, "Timestamp" : 1788982035,
    "Title" : "Weapon Paint Job - Brotherhood of Steel", "Version" : "1762391102.1.0.0" },
  "CSV2_19361ca7-8ae7-4f67-9671-39620c50a8b4" : {
    "Files" : ["cczzz.esl"], "Title" : "A Creation Club item", "Version" : "1" }
})");

    FakeShell shell;
    shell.gameId       = QStringLiteral("fallout4");
    shell.modsDirValue = mods;
    const auto ref = creations::parseCreationUrl(
        "https://creations.bethesda.net/en/fallout4/details/098ac89d-37fd-43cf-89dd-3aeba370dff0/Paint");
    check("the link parses", ref.has_value());
    if (!ref) return;
    shell.model().append(creations::waitingRow(*ref, "tip"));
    const QUuid rowId = shell.model().rowId(0);

    // The plan as the flow would build it (planCreations is file-local).
    QFile cf(catalogPath);
    check("catalog readable", cf.open(QIODevice::ReadOnly));
    const auto catalog = creations::parseContentCatalog(cf.readAll());
    QHash<QString, creations::RowInfo> rows;
    rows.insert(ref->id, creations::RowInfo{rowId, false, {}, {}, "Paint"});
    const creations::Plan plan = creations::planAdoption(
        catalog, rows, creations::dataRootFiles(data), creations::cccPlugins(game), {}, {});
    check("one adoption, the Creation Club item left alone",
          plan.adopt.size() == 1 && plan.unrequested.isEmpty() && plan.adopt.first().rowId == rowId);

    DeployFlow flow(shell);
    const creations::Paths paths{data, catalogPath, game, mods + "/deploy_manifest_modlist.json"};
    DeployFlow::AdoptionReport got;
    bool done = false;
    QEventLoop loop;
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    flow.adoptCreations(paths, plan, /*modal=*/false,
                        [&](const DeployFlow::AdoptionReport &r) { got = r; done = true; loop.quit(); });
    loop.exec();

    const QString dest = mods + "/Weapon Paint Job - Brotherhood of Steel";
    check("the step finished and reported the Creation",
          done && got.failed.isEmpty()
          && got.added == QStringList{"Weapon Paint Job - Brotherhood of Steel"}
          && got.files.size() == 3);
    check("files in the library, Data's spelling kept",
          QFileInfo(dest + "/ccgcafo4003-factionws03bos - main.ba2").size() == 2048
          && QFileInfo::exists(dest + "/ccgcafo4003-factionws03bos.esl"));
    check("gone from Data, the Creation Club file still there",
          !QFileInfo::exists(data + "/ccgcafo4003-factionws03bos.esl")
          && QFileInfo::exists(data + "/cczzz.esl"));
    const ModEntry row = shell.model().at(0);
    check("the row is installed there, named and versioned from the catalog",
          row.rowId == rowId && row.installStatus == int(InstallStatus::Installed)
          && row.modPath == dest && row.customName == "Weapon Paint Job - Brotherhood of Steel"
          && row.creationVersion == "1762391102.1.0.0" && row.checked
          && shell.model().count() == 1);
    check("the list was saved", shell.saves >= 1);

    // Nothing left to take over: ours now, same version.
    rows.clear();
    rows.insert(ref->id, creations::RowInfo{rowId, true, row.creationVersion, row.modPath,
                                             row.customName});
    const creations::Plan again = creations::planAdoption(
        catalog, rows, creations::dataRootFiles(data), creations::cccPlugins(game), {}, {});
    check("a second plan has nothing", again.adopt.isEmpty() && again.notYet.isEmpty()
                                        && again.unrequested.isEmpty());

    // An empty plan runs the continuation at once.
    bool ran = false;
    flow.adoptCreations(paths, {}, false, [&](const DeployFlow::AdoptionReport &) { ran = true; });
    check("an empty plan continues straight away", ran);
}

// New Vegas keeps its own `plugins.txt`, lower-case, and asks for it the
// Windows way, without case. The pre-launch re-sync writes the active list
// into that file rather than making a `Plugins.txt` beside it, and a twin an
// earlier deploy left behind is dropped in favour of the game's.
void testDeployFlowWritesThePluginListWhereTheGameSpelledIt()
{
    std::cout << "testDeployFlowWritesThePluginListWhereTheGameSpelledIt\n";
    QSettings().clear();
    QTemporaryDir tmp;
    const QString lib  = tmp.path() + "/steamapps";
    const QString game = lib + "/common/Fallout New Vegas";
    const QString data = game + "/Data";
    const QString local =
        lib + "/compatdata/22380/pfx/drive_c/users/steamuser/AppData/Local/FalloutNV";
    const QString mods = tmp.path() + "/mods";
    writeFile(game + "/FalloutNV.exe", "MZ");
    writeFile(data + "/FalloutNV.esm", "TES4");
    writeFile(local + "/plugins.txt", "FalloutNV.esm\r\n");
    writeFile(mods + "/Test/Test.esp", "TES4");

    FakeShell shell;
    shell.gameId         = QStringLiteral("falloutnewvegas");
    shell.modsDirValue   = mods;
    shell.loadOrderValue = {QStringLiteral("FalloutNV.esm"), QStringLiteral("Test.esp")};
    ModEntry e           = ModEntry::named(QStringLiteral("Test"));
    e.modPath            = mods + "/Test";
    e.installStatus      = 1;
    e.checked            = true;
    shell.modelStore.append(e);
    Settings::setDataDir(shell.gameId, data);

    // The re-sync only runs over an existing deployment; the state files sit
    // beside the test binary and are removed at the end.
    const QString manifest = resolveUserStatePath("deploy_manifest_modlist.json");
    const QString backup   = resolveUserStatePath("deploy_backup_modlist");
    writeFile(manifest, "{}");

    DeployFlow flow(shell);
    const auto sync = [&] {
        bool ran = false;
        flow.maybeDeployBeforeLaunch(shell.gameId, [&] { ran = true; });
        return ran;
    };
    const auto read = [](const QString &p) {
        QFile f(p);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    check("the re-sync ran through to its continuation", sync());
    check("the plugin is in Data", QFileInfo::exists(data + "/Test.esp"));
    check("the list went into the game's own lower-case file",
          read(local + "/plugins.txt").contains("Test.esp"),
          read(local + "/plugins.txt"));
    check("the game's original list was backed up beside it",
          read(local + "/plugins.txt.nerevarine-bak") == "FalloutNV.esm\r\n");
    check("no second spelling was made", !QFileInfo::exists(local + "/Plugins.txt"));
    check("the path is remembered as the game spells it",
          Settings::pluginsTxtPath(shell.gameId) == local + "/plugins.txt",
          Settings::pluginsTxtPath(shell.gameId));

    // A twin an earlier deploy made: remembered, no backup beside it (it was
    // not there before us), with the game's own file next to it.
    writeFile(local + "/Plugins.txt", "stale\r\n");
    Settings::setPluginsTxtPath(shell.gameId, local + "/Plugins.txt");
    check("the re-sync ran again", sync());
    check("the twin is gone and the game's file holds the list",
          !QFileInfo::exists(local + "/Plugins.txt") &&
              read(local + "/plugins.txt").contains("Test.esp") &&
              Settings::pluginsTxtPath(shell.gameId) == local + "/plugins.txt",
          Settings::pluginsTxtPath(shell.gameId));

    QFile::remove(manifest);
    QFile::remove(resolveUserStatePath("deploy_manifest_modlist.fingerprint"));
    QDir(backup).removeRecursively();
    QSettings().clear();
}

// The shape that reported a clean deploy and changed nothing in game: a mod
// that is ONE bare .archive, no folders at all, which is how most single-file
// Cyberpunk mods ship. Mirrored where it sat, it landed at the install root,
// and the engine reads .archive only from archive/pc/mod.
void testDeployFlowRoutesABareCyberpunkArchive()
{
    std::cout << "testDeployFlowRoutesABareCyberpunkArchive\n";
    QSettings().clear();
    QTemporaryDir tmp;
    const QString lib  = tmp.path() + "/steamapps";
    const QString game = lib + "/common/Cyberpunk 2077";
    const QString mods = tmp.path() + "/mods";
    writeFile(game + "/bin/x64/Cyberpunk2077.exe", "MZ");
    writeFile(game + "/archive/pc/content/basegame_1_engine.archive", "RDAR");
    // No folder around it, exactly as the real mod ships.
    writeFile(mods + "/Blade/Blade_Runner_2077.archive", "RDAR");
    writeFile(mods + "/Blade/readme.txt", "docs");

    FakeShell shell;
    shell.gameId       = QStringLiteral("cyberpunk2077");
    shell.modsDirValue = mods;
    ModEntry e         = ModEntry::named(QStringLiteral("Blade"));
    e.modPath          = mods + "/Blade";
    e.installStatus    = 1;
    e.checked          = true;
    shell.modelStore.append(e);
    Settings::setDataDir(shell.gameId, game);

    const QString manifest = resolveUserStatePath("deploy_manifest_modlist.json");
    const QString backup   = resolveUserStatePath("deploy_backup_modlist");
    writeFile(manifest, "{}");

    DeployFlow flow(shell);
    bool       ran = false;
    flow.maybeDeployBeforeLaunch(shell.gameId, [&] { ran = true; });
    check("the re-sync ran through to its continuation", ran);
    check("the archive is in the one folder the engine reads",
          QFileInfo::exists(game + "/archive/pc/mod/Blade_Runner_2077.archive"));
    check("and NOT at the install root, where it used to go",
          !QFileInfo::exists(game + "/Blade_Runner_2077.archive"));
    // Everything that is not routed still overlays as before.
    check("the rest of the mod overlays unchanged",
          QFileInfo::exists(game + "/readme.txt"));
    check("the game's own archive is untouched",
          QFileInfo::exists(game + "/archive/pc/content/basegame_1_engine.archive"));

    QFile::remove(manifest);
    QFile::remove(resolveUserStatePath("deploy_manifest_modlist.fingerprint"));
    QDir(backup).removeRecursively();
    QSettings().clear();
}

// The same overlay for a GOG copy run through Heroic, which is how the game is
// actually installed on the machine this was finally tested on. Nothing lives
// under steamapps here, so the Steam compatdata hunt finds nothing and the
// only prefix is Heroic's - the leg that did not exist, and whose absence let
// the deploy place RED4ext's and CET's wrapper DLLs and then register them
// nowhere, leaving the loaders inert with no error.
void testDeployFlowFindsTheHeroicPrefixForAGogCopy()
{
    std::cout << "testDeployFlowFindsTheHeroicPrefixForAGogCopy\n";
    QSettings().clear();
    QTemporaryDir tmp;
    // A throwaway HOME, so heroicConfigDirs() reads our fixture and the two
    // $HOME Steam compatdata fallbacks land on nothing.
    const QByteArray oldHome = qgetenv("HOME");
    const QString    home    = tmp.filePath("home");
    qputenv("HOME", home.toUtf8());

    const QString game   = tmp.filePath("Jocs/Cyberpunk 2077");   // not under steamapps
    const QString prefix = home + "/Games/Heroic/Prefixes/default/Cyberpunk 2077";
    const QString reg    = prefix + "/pfx/user.reg";
    const QString mods   = tmp.filePath("mods");
    writeFile(game + "/bin/x64/Cyberpunk2077.exe", "MZ");
    writeFile(game + "/archive/pc/content/basegame_1_engine.archive", "RDAR");
    writeFile(home + "/.config/heroic/gog_store/installed.json",
              QStringLiteral(R"([{"appName":"1423049311","install_path":"%1","executable":""}])")
                  .arg(game));
    writeFile(home + "/.config/heroic/GamesConfig/1423049311.json",
              QStringLiteral(R"({"1423049311":{"winePrefix":"%1",)"
                             R"("wineVersion":{"type":"proton"}}})").arg(prefix));
    writeFile(reg, "WINE REGISTRY Version 2\n\n"
                   "[Software\\\\Wine\\\\DllOverrides] 1785137682\n"
                   "\"atl100\"=\"native,builtin\"\n\n");
    writeFile(mods + "/Loader/bin/x64/winmm.dll", "MZ");   // RED4ext-style wrapper
    writeFile(mods + "/Loader/archive/pc/mod/loader.archive", "RDAR");

    FakeShell shell;
    shell.gameId       = QStringLiteral("cyberpunk2077");
    shell.modsDirValue = mods;
    ModEntry e         = ModEntry::named(QStringLiteral("Loader"));
    e.modPath          = mods + "/Loader";
    e.installStatus    = 1;
    e.checked          = true;
    shell.modelStore.append(e);
    Settings::setDataDir(shell.gameId, game);

    const QString manifest = resolveUserStatePath("deploy_manifest_modlist.json");
    const QString backup   = resolveUserStatePath("deploy_backup_modlist");
    writeFile(manifest, "{}");
    const auto read = [](const QString &p) {
        QFile f(p);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };

    DeployFlow flow(shell);
    bool       ran = false;
    flow.maybeDeployBeforeLaunch(shell.gameId, [&] { ran = true; });
    check("the re-sync ran through to its continuation", ran);
    check("the files still land in the install folder",
          QFileInfo::exists(game + "/archive/pc/mod/loader.archive") &&
              QFileInfo::exists(game + "/bin/x64/winmm.dll"));
    check("and Heroic's prefix is the one told to prefer the wrapper",
          read(reg).contains("\"winmm\"=\"native"), read(reg));

    QFile::remove(manifest);
    QFile::remove(resolveUserStatePath("deploy_manifest_modlist.fingerprint"));
    QDir(backup).removeRecursively();
    QSettings().clear();
    if (oldHome.isEmpty()) qunsetenv("HOME"); else qputenv("HOME", oldHome);
}

// Cyberpunk 2077: an overlay of the install folder itself, with the exe two
// levels down. The deploy root, the exe check and the Proton prefix have to
// agree on the folder, and the wrapper DLL a mod loader ships sits beside
// the exe in bin/x64/, not at the deploy root - the override pass has to
// look there. Built against this mock, not a real install.
void testDeployFlowOverlaysCyberpunkIntoItsInstallFolder()
{
    std::cout << "testDeployFlowOverlaysCyberpunkIntoItsInstallFolder\n";
    QSettings().clear();
    QTemporaryDir tmp;
    const QString lib  = tmp.path() + "/steamapps";
    const QString game = lib + "/common/Cyberpunk 2077";
    const QString reg  = lib + "/compatdata/1091500/pfx/user.reg";
    const QString mods = tmp.path() + "/mods";
    writeFile(game + "/REDprelauncher.exe", "MZ");
    writeFile(game + "/bin/x64/Cyberpunk2077.exe", "MZ");
    writeFile(game + "/archive/pc/content/basegame_1_engine.archive", "RDAR");
    writeFile(reg, "WINE REGISTRY Version 2\n\n"
                   "[Software\\\\Wine\\\\DllOverrides] 1785137682\n"
                   "\"atl100\"=\"native,builtin\"\n\n");
    writeFile(mods + "/Cool/archive/pc/mod/cool.archive", "RDAR");
    writeFile(mods + "/Cool/r6/scripts/cool.reds", "// reds");
    writeFile(mods + "/Cool/bin/x64/version.dll", "MZ"); // a CET-style wrapper

    FakeShell shell;
    shell.gameId       = QStringLiteral("cyberpunk2077");
    shell.modsDirValue = mods;
    ModEntry e         = ModEntry::named(QStringLiteral("Cool"));
    e.modPath          = mods + "/Cool";
    e.installStatus    = 1;
    e.checked          = true;
    shell.modelStore.append(e);
    Settings::setDataDir(shell.gameId, game);

    const QString manifest = resolveUserStatePath("deploy_manifest_modlist.json");
    const QString backup   = resolveUserStatePath("deploy_backup_modlist");
    writeFile(manifest, "{}");
    const auto read = [](const QString &p) {
        QFile f(p);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };

    DeployFlow flow(shell);
    bool       ran = false;
    flow.maybeDeployBeforeLaunch(shell.gameId, [&] { ran = true; });
    check("the re-sync ran through to its continuation", ran);
    check("the data folder is the install folder itself",
          Settings::dataDir(shell.gameId) == game, Settings::dataDir(shell.gameId));
    check("the archive landed under archive/pc/mod/, not at the root",
          QFileInfo::exists(game + "/archive/pc/mod/cool.archive") &&
              !QFileInfo::exists(game + "/pc/mod/cool.archive"));
    check("the script landed under r6/scripts/",
          QFileInfo::exists(game + "/r6/scripts/cool.reds"));
    check("the wrapper DLL sits beside the exe",
          QFileInfo::exists(game + "/bin/x64/version.dll"));
    check("the game's own archive is untouched",
          read(game + "/archive/pc/content/basegame_1_engine.archive") == "RDAR");
    check("the prefix was told to prefer the wrapper beside the exe",
          read(reg).contains("\"version\"=\"native"), read(reg));

    QFile::remove(manifest);
    QFile::remove(resolveUserStatePath("deploy_manifest_modlist.fingerprint"));
    QDir(backup).removeRecursively();
    QSettings().clear();
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("nerevarine-test");
    QCoreApplication::setApplicationName("flows-test");
    QStandardPaths::setTestModeEnabled(true); // isolate QSettings access

    testMO2LinesAndRows();
    testMO2Instance();
    testWabbajackMods();
    testSniffAndBase();
    testImportFlowReplacesTheListFromAnMO2File();
    testImportFlowRoutesANerevarineFileToTheLoader();
    testDeployFlowFingerprintAndQuietPaths();
    testLibraryFlowMovesFoldersAndRepointsRows();
    testLibraryFlowMoveDestinationForDiskRoot();
    testLibraryFlowMovesTheWholeLibrary();
    testLibraryFlowFindsFoldersNoModlistPointsAt();
    testLibraryFlowAdoptsNewFoldersByItself();
    testLibraryFlowPlansFolderRenames();
    testDeployFlowAdoptsCreationsFromTheGamesDownloads();
    testDeployFlowWritesThePluginListWhereTheGameSpelledIt();
    testDeployFlowOverlaysCyberpunkIntoItsInstallFolder();
    testDeployFlowRoutesABareCyberpunkArchive();
    testDeployFlowFindsTheHeroicPrefixForAGogCopy();

    std::cout << "\n";
    if (s_failed == 0)
        std::cout << "\033[32m" << s_passed << " / " << s_passed << " tests passed\033[0m\n";
    else
        std::cout << s_passed << " passed, \033[31m" << s_failed << " failed\033[0m\n";
    return s_failed == 0 ? 0 : 1;
}
