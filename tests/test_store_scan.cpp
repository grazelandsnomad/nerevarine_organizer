// What the stores know about where games live.
//
// The bug these exist for: Gothic II is installed through Heroic at
// "/mnt/nvme_4TB/Jocs/Gothic 2 Gold", the shipped candidates read "Gothic II
// Gold Edition" / "Gothic 2 Gold Edition" / "Gothic II", and matching only the
// folder leaf found nothing - so the setup asked the user to go find their own
// game. Heroic's library cache calls that install "Gothic 2 Gold Edition",
// which is a candidate we already ship and never read.
//
// Fixtures are shaped from the real files on the author's machine, including
// the fields that look wrong and are not (an empty "executable", an
// "is_installed": false on a game that is installed).

#include "store_scan.h"
#include "game_adapter.h"
#include "game_store.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <iostream>

#include "test_harness.h"

namespace {

// One entry of Heroic's gog_store/installed.json, as written for the author's
// Gothic II. "executable" really is empty, so nothing can rely on it.
const QByteArray kInstalledJson = R"([
  {
    "platform": "windows",
    "executable": "",
    "install_path": "/mnt/nvme_4TB/Jocs/Gothic 2 Gold",
    "install_size": "2.81 GiB",
    "is_dlc": false,
    "appName": "1207658718"
  },
  {
    "platform": "windows",
    "executable": "Fallout4.exe",
    "install_path": "/mnt/nvme_2TB/Jocs/Fallout London",
    "appName": "1491728574"
  }
])";

// store_cache/gog_library.json: the names Heroic has for those ids. Note
// is_installed is false for a game that IS installed - the cache is stale about
// installation and authoritative only about names.
const QByteArray kLibraryJson = R"({
  "games": [
    {
      "app_name": "1207658718",
      "title": "Gothic 2 Gold Edition",
      "folder_name": "Gothic 2 Gold",
      "is_installed": false
    },
    {
      "app_name": "1491728574",
      "title": "Fallout: London One-click Edition",
      "folder_name": "Fallout London",
      "is_installed": true
    }
  ]
})";

const QByteArray kAppManifest = R"(
"AppState"
{
	"appid"		"489830"
	"name"		"The Elder Scrolls V: Skyrim Special Edition"
	"StateFlags"		"4"
	"installdir"		"Skyrim Special Edition"
	"LastUpdated"		"1723800000"
}
)";

void testHeroicInstalledParse()
{
    std::cout << "\n[reading what Heroic says is installed]\n";
    const auto installs = store_scan::parseHeroicInstalled(kInstalledJson);
    check("both entries read", installs.size() == 2, QString::number(installs.size()));
    check("the install path is the only place the real folder shows",
          installs.first().installPath == QLatin1String("/mnt/nvme_4TB/Jocs/Gothic 2 Gold"),
          installs.first().installPath);
    check("the app id is kept, it is the join to the names",
          installs.first().appName == QLatin1String("1207658718"));
    check("an empty executable is not invented",
          installs.first().executable.isEmpty());

    // These are another program's caches, rewritten while we may be reading.
    check("truncated json yields nothing, not a crash",
          store_scan::parseHeroicInstalled("[{\"install_path\":").isEmpty());
    check("an unexpected shape yields nothing",
          store_scan::parseHeroicInstalled("{\"nope\":true}").isEmpty());
    check("an entry with no install path is dropped",
          store_scan::parseHeroicInstalled("[{\"appName\":\"1\"}]").isEmpty());
}

void testHeroicLibraryParse()
{
    std::cout << "\n[reading what Heroic calls them]\n";
    const auto titles = store_scan::parseHeroicLibrary(kLibraryJson);
    check("both titles read", titles.size() == 2, QString::number(titles.size()));
    check("the title is the signal that was missing",
          titles.value("1207658718").title == QLatin1String("Gothic 2 Gold Edition"),
          titles.value("1207658718").title);
    check("and the store's own folder name comes too",
          titles.value("1207658718").folderName == QLatin1String("Gothic 2 Gold"));
    // The flag is stale for an installed game, so reading it would drop the
    // very entry this all exists for.
    check("is_installed:false does not suppress an entry",
          titles.contains("1207658718"));
    check("malformed json yields nothing",
          store_scan::parseHeroicLibrary("not json at all").isEmpty());
}

void testTheReportedCaseMatches()
{
    std::cout << "\n[the Gothic II install is recognised]\n";
    const auto installs = store_scan::parseHeroicInstalled(kInstalledJson);
    const auto titles   = store_scan::parseHeroicLibrary(kLibraryJson);
    const GameAdapter *a = GameAdapterRegistry::find("gothic2");
    check("the profile exists", a != nullptr);
    if (!a) return;

    const store_scan::HeroicInstall &g = installs.first();
    const store_scan::StoreTitle t = titles.value(g.appName);

    // The three name signals the locator now compares, in one place.
    const QStringList names = {QStringLiteral("Gothic 2 Gold"), t.title, t.folderName};

    bool exact = false, loose = false;
    for (const auto &gi : a->gogLayouts()) {
        for (const QString &n : names) {
            if (n.compare(gi.folder, Qt::CaseInsensitive) == 0) exact = true;
            if (store_scan::titleMatches(gi.folder, n))         loose = true;
        }
    }
    check("a shipped candidate matches the library title exactly", exact);
    check("and the loose comparison agrees", loose);
}

void testTitleNormalisation()
{
    std::cout << "\n[the spellings a store and a mod manager pick]\n";
    check("roman numerals fold to digits",
          store_scan::normalizeTitle("Gothic II") == QLatin1String("gothic 2"),
          store_scan::normalizeTitle("Gothic II"));
    check("edition noise is dropped",
          store_scan::normalizeTitle("Gothic 2 Gold Edition") == QLatin1String("gothic 2"),
          store_scan::normalizeTitle("Gothic 2 Gold Edition"));
    check("punctuation is not a difference",
          store_scan::normalizeTitle("Fallout: London") == QLatin1String("fallout london"),
          store_scan::normalizeTitle("Fallout: London"));
    check("game of the year says nothing about which game",
          store_scan::normalizeTitle("Fallout 3 Game of the Year Edition")
              == QLatin1String("fallout 3"),
          store_scan::normalizeTitle("Fallout 3 Game of the Year Edition"));

    check("Gothic II matches the folder Gothic 2 Gold",
          store_scan::titleMatches("Gothic II Gold Edition", "Gothic 2 Gold"));
    check("and the reverse spelling too",
          store_scan::titleMatches("Gothic 2", "Gothic II Gold Edition"));

    // The reason a loose name match is never enough on its own: these two ship
    // the same SkyrimSE.exe, so a name that matched loosely would hand back the
    // wrong install with a real file sitting there to confirm it.
    check("Skyrim SE does not match Enderal SE",
          !store_scan::titleMatches("Skyrim Special Edition", "Enderal Special Edition"));
    check("nor Enderal SE Skyrim SE",
          !store_scan::titleMatches("Enderal Special Edition", "Skyrim Special Edition"));
    check("a sequel does not answer for its predecessor",
          !store_scan::titleMatches("Gothic 3", "Gothic 2 Gold"));
    // A subset test, so the shorter name does match the longer one. This is why
    // the caller pairs it with a file that has to exist: Gothic 1's exe is not
    // in Gothic 2's folder.
    check("a bare name matches its sequel's folder, which is why evidence is required",
          store_scan::titleMatches("Gothic", "Gothic 2 Gold"));
    check("empty compares to nothing", !store_scan::titleMatches("", "Gothic 2 Gold"));
    check("and nothing compares to empty", !store_scan::titleMatches("Gothic", ""));
}

void testSteamManifest()
{
    std::cout << "\n[Steam's own answer for where an app is]\n";
    check("installdir is read",
          store_scan::steamInstallDir(kAppManifest) == QLatin1String("Skyrim Special Edition"),
          store_scan::steamInstallDir(kAppManifest));
    check("a manifest without one yields nothing",
          store_scan::steamInstallDir("\"AppState\" { \"appid\" \"1\" }").isEmpty());
    check("garbage yields nothing", store_scan::steamInstallDir("").isEmpty());
    // No app id, no lookup: never walk every library for an unnamed game.
    check("an empty app id resolves to nothing",
          store_scan::steamAppInstallPath(QStringList{"/nonexistent"}, QString()).isEmpty());
    check("a library that is not there resolves to nothing",
          store_scan::steamAppInstallPath(QStringList{"/nonexistent"}, "489830").isEmpty());
}


// Which shop a copy of a game came from, and which shop a mod file was built
// for. The case that prompted it: Skyrim's SKSE page carries "Skyrim Script
// Extender (SKSE64) GOG" and "... Steam" side by side, the picker called one
// of them a "separate add-on", and picking it gets you an extender that does
// not load and does not complain.
void testGameStoreFromNames()
{
    using game_store::Store;
    std::cout << "\n[What a mod file's name says it was built for]\n";

    check("the GOG build is recognised",
          game_store::fromFileName("Skyrim Script Extender (SKSE64) GOG")
              == Store::Gog);
    check("and the Steam one",
          game_store::fromFileName("Skyrim Script Extender (SKSE64) Steam")
              == Store::Steam);
    check("case does not matter",
          game_store::fromFileName("SKSE64 gog build") == Store::Gog);

    // The words are words. Whole-word matching is the whole guard here.
    check("goggles are not a store",
          game_store::fromFileName("Goggles and Gasmasks 4K") == Store::Unknown);
    check("neither is steampunk",
          game_store::fromFileName("Steampunk Dwemer Armour") == Store::Unknown);
    check("a name claiming both says nothing",
          game_store::fromFileName("Steam and GOG merged package") == Store::Unknown);
    check("nor does a name with no store in it",
          game_store::fromFileName("Address Library for SKSE Plugins")
              == Store::Unknown);

    // Two builds of one file only look alike once the store word is gone.
    check("the store word comes out",
          game_store::stripStoreWords("Skyrim Script Extender (SKSE64) GOG").simplified()
              == game_store::stripStoreWords("Skyrim Script Extender (SKSE64) Steam").simplified(),
          game_store::stripStoreWords("Skyrim Script Extender (SKSE64) GOG"));

    check("and the store has a name to print",
          game_store::name(Store::Steam) == QLatin1String("Steam")
              && game_store::name(Store::Gog) == QLatin1String("GOG")
              && game_store::name(Store::Unknown).isEmpty());
}

void testGameStoreFromPaths()
{
    using game_store::Store;
    std::cout << "\n[What an install path says about where the game came from]\n";

    // The author's own Skyrim, from the profile's data_dir.
    check("a steamapps component is Steam and nothing else",
          game_store::fromInstallPath(
              "/mnt/nvme_2TB/SteamLibrary/steamapps/common/Skyrim Special Edition/Data",
              {}) == Store::Steam);
    // Libraries made by older Steam clients spell it with capitals.
    check("including the old SteamApps spelling",
          game_store::fromInstallPath(
              "/home/u/.steam/steam/SteamApps/common/Fallout 4/Fallout4.exe", {})
              == Store::Steam);

    // GOG leaves no word in the path, so the evidence is Heroic's own list.
    const QStringList heroic = { "/mnt/nvme_4TB/Jocs/Gothic 2 Gold" };
    check("a path inside a Heroic install is GOG",
          game_store::fromInstallPath("/mnt/nvme_4TB/Jocs/Gothic 2 Gold/system", heroic)
              == Store::Gog);
    check("the install root itself counts",
          game_store::fromInstallPath("/mnt/nvme_4TB/Jocs/Gothic 2 Gold", heroic)
              == Store::Gog);
    // "Gothic 2 Gold Edition" starts with "Gothic 2 Gold" and is a different
    // game folder: a prefix test without the separator claims it.
    check("a sibling folder sharing the first letters does not",
          game_store::fromInstallPath("/mnt/nvme_4TB/Jocs/Gothic 2 Gold Edition", heroic)
              == Store::Unknown);
    check("a root of / owns nothing",
          game_store::fromInstallPath("/opt/games/whatever", { "/" }) == Store::Unknown);

    // No evidence is its own answer. A hand-copied install belongs to no shop,
    // and naming one would put a tick beside the wrong file.
    check("an unrecognised path says nothing",
          game_store::fromInstallPath("/home/u/Games/Morrowind", {}) == Store::Unknown);
    check("an empty path says nothing",
          game_store::fromInstallPath("", heroic) == Store::Unknown);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testHeroicInstalledParse();
    testHeroicLibraryParse();
    testTheReportedCaseMatches();
    testTitleNormalisation();
    testSteamManifest();
    testGameStoreFromNames();
    testGameStoreFromPaths();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
