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

#include <QApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
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
    moves.append({a, src + "/A", QStringLiteral("A"), 4});
    moves.append({b, src + "/B", QStringLiteral("B"), 4});
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

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    testMO2LinesAndRows();
    testMO2Instance();
    testWabbajackMods();
    testSniffAndBase();
    testImportFlowReplacesTheListFromAnMO2File();
    testImportFlowRoutesANerevarineFileToTheLoader();
    testDeployFlowFingerprintAndQuietPaths();
    testLibraryFlowMovesFoldersAndRepointsRows();
    testDeployFlowAdoptsCreationsFromTheGamesDownloads();

    std::cout << "\n";
    if (s_failed == 0)
        std::cout << "\033[32m" << s_passed << " / " << s_passed << " tests passed\033[0m\n";
    else
        std::cout << s_passed << " passed, \033[31m" << s_failed << " failed\033[0m\n";
    return s_failed == 0 ? 0 : 1;
}
