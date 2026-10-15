// The flows the window hands its concerns to, driven through a fake shell:
// what they parse (import_formats) and what they do to the model, the
// persistence and the status line, with no window and no dialog.

#include "import_flow.h"
#include "import_formats.h"
#include "modentry.h"
#include "modlist_model.h"
#include "modroles.h"

#include <QApplication>
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

} // namespace

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

    std::cout << "\n";
    if (s_failed == 0)
        std::cout << "\033[32m" << s_passed << " / " << s_passed << " tests passed\033[0m\n";
    else
        std::cout << s_passed << " passed, \033[31m" << s_failed << " failed\033[0m\n";
    return s_failed == 0 ? 0 : 1;
}
