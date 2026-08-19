// One adapter class per game; interface in include/game_adapter.h.
// New game = add a class here + register it in kAdapters below.
// Defaults mean "missing field = not on this storefront / no LOOT / etc.",
// so each class only overrides what differs.

#include "game_adapter.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace {

// Seeds for SArchiveList, used ONLY when the ini has none (the launcher
// normally writes one). Wrong entries here would leave the base game's own
// archives unloaded, so a game we cannot state confidently gets no seed and we
// simply never invent the key.
const QStringList kOblivionVanillaBsas = {
    QStringLiteral("Oblivion - Meshes.bsa"),
    QStringLiteral("Oblivion - Textures - Compressed.bsa"),
    QStringLiteral("Oblivion - Sounds.bsa"),
    QStringLiteral("Oblivion - Voices1.bsa"),
    QStringLiteral("Oblivion - Voices2.bsa"),
    QStringLiteral("Oblivion - Misc.bsa"),
};
const QStringList kFalloutNVVanillaBsas = {
    QStringLiteral("Fallout - Textures.bsa"),
    QStringLiteral("Fallout - Textures2.bsa"),
    QStringLiteral("Fallout - Meshes.bsa"),
    QStringLiteral("Fallout - Voices1.bsa"),
    QStringLiteral("Fallout - Sound.bsa"),
    QStringLiteral("Fallout - Misc.bsa"),
};

// -- OpenMW (Morrowind) ------------------------------------------------
// All the OpenMW-specific paths (openmw.cfg sync, plugin parser, BSA
// discovery) gate on isMorrowind(). GOG GOTY is the usual install.
class MorrowindAdapter : public GameAdapter {
public:
    QString     id()                 const override { return QStringLiteral("morrowind"); }
    QString     displayName()        const override { return QStringLiteral("OpenMW (Morrowind)"); }
    QString     defaultModsDirName() const override { return QStringLiteral("nerevarine_mods"); }
    QList<GogLayout> gogLayouts()    const override { return {
        {"The Elder Scrolls III Morrowind GOTY", "Morrowind.exe", ""},
        {"Morrowind",                            "Morrowind.exe", ""},
    }; }
    QStringList lutrisTokens()       const override { return {"morrowind"}; }
    QString     lootSlug()           const override { return QStringLiteral("OpenMW"); }
    bool        isMorrowind()        const override { return true; }
    LoadOrderStyle loadOrderStyle()  const override { return LoadOrderStyle::OpenMW; }
    bool        pinned()             const override { return true; }
    bool        builtin()            const override { return true; }
};

// -- Bethesda titles ---------------------------------------------------

// Skyrim ships as three things people mod separately, so there are three
// adapters. They are NOT interchangeable profiles of one game:
//
//   Special Edition   64-bit 2016 remaster, SkyrimSE.exe, runtime 1.5.97
//   Anniversary Ed.   the same install updated to 1.6.x + Creation Club
//   Legendary/Oldrim  32-bit 2011 original, TESV.exe, its own everything
//
// SE and AE share an engine, an exe, a Steam app and a Nexus section; LE
// shares none of them. What splits SE from AE is the runtime version, and
// that is exactly what mods are built against - a 1.5.97 SKSE plugin does
// not load on 1.6.x and vice versa - so they get separate profiles (mods
// dir, modlist, load order, forbidden list) rather than one shared one.
class SkyrimSpecialEditionAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("skyrimspecialedition"); }
    QString displayName() const override { return QStringLiteral("Skyrim Special Edition"); }
    QString steamAppId()  const override { return QStringLiteral("489830"); }
    SteamLayout steamLayout() const override {
        return {"Skyrim Special Edition", "SkyrimSE.exe", "SkyrimSELauncher.exe"};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"The Elder Scrolls V Skyrim Special Edition", "SkyrimSE.exe", "SkyrimSELauncher.exe"},
        {"Skyrim Special Edition",                     "SkyrimSE.exe", "SkyrimSELauncher.exe"},
    }; }
    QStringList lutrisTokens() const override { return {"skyrim", "special"}; }
    QString lootSlug()         const override { return QStringLiteral("Skyrim Special Edition"); }
    LoadOrderStyle loadOrderStyle() const override { return LoadOrderStyle::AsteriskPluginsTxt; }
    QString dataSubdir()       const override { return QStringLiteral("Data"); }
    QString localAppDataName() const override { return QStringLiteral("Skyrim Special Edition"); }
    QString myGamesName()      const override { return QStringLiteral("Skyrim Special Edition"); }
    QString prefsIniName()     const override { return QStringLiteral("SkyrimPrefs.ini"); }
    QStringList scriptExtenderLoaders() const override { return {QStringLiteral("skse64_loader.exe")}; }
    // Fully classified for deploy, so hiding it behind "Show all games"
    // only made working support undiscoverable (same call as Fallout 4).
    bool    pinned()           const override { return true; }
};

// Anniversary Edition is not a separate install: it is Special Edition
// patched to 1.6.x with the Creation Club content bought on top (the AE
// Upgrade is DLC against the same Steam app, in the same folder, run from
// the same SkyrimSE.exe). Everything that faces the outside world is
// therefore SE's and is inherited unchanged: the Steam/GOG layouts, the
// LOOT slug, the Proton prefix dirs, the '*'-prefixed Plugins.txt, SKSE64.
//
// Nexus has no AE section either - AE mods are filed under
// skyrimspecialedition - hence the nexusDomain() override, which is the
// one place a bare id() would have produced a dead URL.
//
// It earns its own profile anyway. Half the SE scene is pinned to 1.5.97
// and half to 1.6.x, each needs its own SKSE build and its own versions of
// most SKSE mods, and people who mod both keep two copies of the game.
// One shared profile would mean one mods dir and one load order for both.
class SkyrimAnniversaryEditionAdapter : public SkyrimSpecialEditionAdapter {
public:
    QString id()          const override { return QStringLiteral("skyrimanniversaryedition"); }
    QString displayName() const override { return QStringLiteral("Skyrim Anniversary Edition"); }
    // Deliberately NOT id(): there is no such Nexus domain.
    QString nexusDomain() const override { return QStringLiteral("skyrimspecialedition"); }
};

// The 2011 original - Legendary Edition, "Oldrim" to the modding scene.
// A different engine from SE/AE (32-bit, TESV.exe), its own Steam app, its
// own Nexus section, its own LOOT game, its own config dirs, and 32-bit
// SKSE. Nothing is shared with SE/AE but the word "Skyrim".
//
// Deliberately left unclassified for deployment (no loadOrderStyle, no
// dataSubdir), so the Deploy actions stay hidden while profiles, modlists,
// downloads, LOOT and launching all work - the same posture as Enderal's
// LE-engine original. Oldrim encodes load order in a THIRD scheme this app
// has no writer for: plugins.txt holds the active set and loadorder.txt
// holds the order, both in AppData/Local/Skyrim, rather than the file
// mtimes of the Gamebryo titles or SE's '*'-prefixed lines. Classifying it
// as either existing style would deploy the mods and then load them in the
// wrong order, which is worse than not offering the button.
class SkyrimAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("skyrim"); }
    QString displayName() const override { return QStringLiteral("Skyrim Legendary Edition (Oldrim)"); }
    QString steamAppId()  const override { return QStringLiteral("72850"); }
    SteamLayout steamLayout() const override {
        return {"Skyrim", "TESV.exe", "SkyrimLauncher.exe"};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"The Elder Scrolls V Skyrim Legendary Edition", "TESV.exe", "SkyrimLauncher.exe"},
        {"Skyrim Legendary Edition",                     "TESV.exe", "SkyrimLauncher.exe"},
        {"Skyrim",                                        "TESV.exe", "SkyrimLauncher.exe"},
    }; }
    QStringList lutrisTokens() const override { return {"skyrim"}; }
    QString lootSlug()         const override { return QStringLiteral("Skyrim"); }
    // Recorded for whoever classifies this engine later; inert until then,
    // since every consumer of these bails on an empty dataSubdir().
    QString localAppDataName() const override { return QStringLiteral("Skyrim"); }
    QString myGamesName()      const override { return QStringLiteral("Skyrim"); }
    // No prefsIniName: LE has a SkyrimPrefs.ini too, but the tuner's key set
    // is verified against SE's only, and it WRITES the file. Turning it on
    // here would be guessing into the user's config.

    // 32-bit SKSE, a different binary from SE/AE's skse64_loader.exe.
    QStringList scriptExtenderLoaders() const override { return {QStringLiteral("skse_loader.exe")}; }
    bool    pinned()           const override { return true; }
};

// Enderal ships as its own free Steam app bundling the Skyrim engine, with its
// own My Games / AppData dirs - a first-class game here, not a Skyrim profile.
// The ids deliberately equal the Nexus game domains (enderal /
// enderalspecialedition): that's what an nxm:// link carries as its host and
// what the Wabbajack import table in mainwindow_import.cpp emits, so downloads
// route with no extra mapping.
class EnderalSEAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("enderalspecialedition"); }
    QString displayName() const override { return QStringLiteral("Enderal Special Edition"); }
    QString steamAppId()  const override { return QStringLiteral("976620"); }
    SteamLayout steamLayout() const override {
        return {"Enderal Special Edition", "SkyrimSE.exe", "Enderal Launcher.exe"};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"Enderal Special Edition", "SkyrimSE.exe", "Enderal Launcher.exe"},
    }; }
    QStringList lutrisTokens() const override { return {"enderal", "special"}; }
    QString lootSlug()         const override { return QStringLiteral("Enderal Special Edition"); }
    bool    pinned()           const override { return true; }
    // SE engine, so it mirrors Skyrim Special Edition exactly: '*'-prefixed
    // Plugins.txt in its own AppData dir, loose files and name-matched BSAs
    // load natively (no archiveConfig), SKSE64 as the script extender.
    LoadOrderStyle loadOrderStyle() const override { return LoadOrderStyle::AsteriskPluginsTxt; }
    QString dataSubdir()       const override { return QStringLiteral("Data"); }
    QString localAppDataName() const override { return QStringLiteral("Enderal Special Edition"); }
    QString myGamesName()      const override { return QStringLiteral("Enderal Special Edition"); }
    QStringList scriptExtenderLoaders() const override { return {QStringLiteral("skse64_loader.exe")}; }
};

class EnderalAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("enderal"); }
    QString displayName() const override { return QStringLiteral("Enderal: Forgotten Stories"); }
    QString steamAppId()  const override { return QStringLiteral("933480"); }
    SteamLayout steamLayout() const override {
        return {"Enderal", "TESV.exe", "Enderal Launcher.exe"};
    }
    QStringList lutrisTokens() const override { return {"enderal"}; }
    QString lootSlug()         const override { return QStringLiteral("Enderal"); }
    bool    pinned()           const override { return true; }
    // LE engine: like classic Skyrim above, its plugins.txt scheme has no
    // deploy classification here yet, so profiles/modlists/downloads/LOOT work
    // but the Deploy actions stay hidden.
};

// Starfield shares Skyrim SE / FO4's load-order model: Plugins.txt order IS the
// load order, active entries '*'-prefixed. So classifying it is enough - the
// deploy path, the Plugins.txt writer and the Proton prefix resolvers are all
// generic over the fields below.
//
// Not done, and not a blocker: pluginparser only understands TES3 headers, so
// Starfield plugins carry no master metadata. Master satisfaction and
// missing-master suppression are gated on isMorrowind(), and mastersFirst()
// orders by file extension, so nothing downstream needs the parse.
class StarfieldAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("starfield"); }
    QString displayName() const override { return QStringLiteral("Starfield"); }
    QString steamAppId()  const override { return QStringLiteral("1716740"); }
    SteamLayout steamLayout() const override {
        return {"Starfield", "Starfield.exe", ""};
    }
    QStringList lutrisTokens() const override { return {"starfield"}; }
    QString lootSlug()         const override { return QStringLiteral("Starfield"); }
    bool    pinned()           const override { return true; }
    LoadOrderStyle loadOrderStyle() const override { return LoadOrderStyle::AsteriskPluginsTxt; }
    QString dataSubdir()       const override { return QStringLiteral("Data"); }
    QString localAppDataName() const override { return QStringLiteral("Starfield"); }
    QString myGamesName()      const override { return QStringLiteral("Starfield"); }
    ArchiveConfig archiveConfig() const override {
        return { ArchiveConfig::Style::ModernCustomIni,
                 QStringLiteral("StarfieldCustom.ini"), QStringLiteral(".ba2"),
                 {}, /*createIfMissing=*/true };
    }
    QStringList scriptExtenderLoaders() const override { return {QStringLiteral("sfse_loader.exe")}; }
};

class OblivionAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("oblivion"); }
    QString displayName() const override { return QStringLiteral("Oblivion"); }
    QString steamAppId()  const override { return QStringLiteral("22330"); }
    SteamLayout steamLayout() const override {
        return {"Oblivion", "Oblivion.exe", "OblivionLauncher.exe"};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"The Elder Scrolls IV Oblivion GOTY Deluxe", "Oblivion.exe", "OblivionLauncher.exe"},
        {"The Elder Scrolls IV Oblivion GOTY",        "Oblivion.exe", "OblivionLauncher.exe"},
        {"The Elder Scrolls IV Oblivion",             "Oblivion.exe", "OblivionLauncher.exe"},
        {"Oblivion",                                   "Oblivion.exe", "OblivionLauncher.exe"},
    }; }
    QStringList lutrisTokens() const override { return {"oblivion"}; }
    QString lootSlug()         const override { return QStringLiteral("Oblivion"); }
    // Pinned so an Oblivion profile can be made and deployment (experimental) tested.
    bool    pinned()           const override { return true; }
    // OG Oblivion: load order is file-mtime in Data/, active set in Plugins.txt.
    LoadOrderStyle loadOrderStyle() const override { return LoadOrderStyle::TimestampPluginsTxt; }
    QString dataSubdir()       const override { return QStringLiteral("Data"); }
    QString localAppDataName() const override { return QStringLiteral("Oblivion"); }
    QString myGamesName()      const override { return QStringLiteral("Oblivion"); }
    // OBSE (xOBSE preferred). Steam auto-loads it; these are for launching
    // the exe directly (GOG/manual).
    QStringList scriptExtenderLoaders() const override {
        return {QStringLiteral("xobse_loader.exe"), QStringLiteral("obse_loader.exe")};
    }
    ArchiveConfig archiveConfig() const override {
        return { ArchiveConfig::Style::GamebryoArchiveList,
                 QStringLiteral("Oblivion.ini"), QStringLiteral(".bsa"),
                 kOblivionVanillaBsas, /*createIfMissing=*/false };
    }
};

class OblivionRemasteredAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("oblivionremastered"); }
    QString displayName() const override { return QStringLiteral("Oblivion Remastered"); }
    QString lootSlug()    const override { return QStringLiteral("Oblivion Remastered"); }
};

class Fallout3Adapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("fallout3"); }
    QString displayName() const override { return QStringLiteral("Fallout 3"); }
    QString steamAppId()  const override { return QStringLiteral("22370"); }
    SteamLayout steamLayout() const override {
        // Locator strips the trailing " goty" via a .chop(5) fallback.
        return {"Fallout 3 goty", "Fallout3.exe", "Fallout3Launcher.exe"};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"Fallout 3 GOTY",                       "Fallout3.exe", "Fallout3Launcher.exe"},
        {"Fallout 3 Game of the Year Edition",   "Fallout3.exe", "Fallout3Launcher.exe"},
        {"Fallout 3",                            "Fallout3.exe", "Fallout3Launcher.exe"},
    }; }
    QStringList lutrisTokens() const override { return {"fallout", "3"}; }
    QString lootSlug()         const override { return QStringLiteral("Fallout3"); }
    LoadOrderStyle loadOrderStyle() const override { return LoadOrderStyle::TimestampPluginsTxt; }
    QString dataSubdir()       const override { return QStringLiteral("Data"); }
    QString localAppDataName() const override { return QStringLiteral("Fallout3"); }
    QString myGamesName()      const override { return QStringLiteral("Fallout3"); }
    ArchiveConfig archiveConfig() const override {
        return { ArchiveConfig::Style::GamebryoArchiveList,
                 QStringLiteral("Fallout.ini"), QStringLiteral(".bsa"),
                 {}, /*createIfMissing=*/false };
    }
    QStringList scriptExtenderLoaders() const override { return {QStringLiteral("fose_loader.exe")}; }
};

class Fallout4Adapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("fallout4"); }
    QString displayName() const override { return QStringLiteral("Fallout 4"); }
    QString steamAppId()  const override { return QStringLiteral("377160"); }
    SteamLayout steamLayout() const override {
        return {"Fallout 4", "Fallout4.exe", "Fallout4Launcher.exe"};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"Fallout 4", "Fallout4.exe", "Fallout4Launcher.exe"},
    }; }
    QStringList lutrisTokens() const override { return {"fallout", "4"}; }
    QString lootSlug()         const override { return QStringLiteral("Fallout4"); }
    LoadOrderStyle loadOrderStyle() const override { return LoadOrderStyle::AsteriskPluginsTxt; }
    QString dataSubdir()       const override { return QStringLiteral("Data"); }
    QString localAppDataName() const override { return QStringLiteral("Fallout4"); }
    QString myGamesName()      const override { return QStringLiteral("Fallout4"); }
    ArchiveConfig archiveConfig() const override {
        return { ArchiveConfig::Style::ModernCustomIni,
                 QStringLiteral("Fallout4Custom.ini"), QStringLiteral(".ba2"),
                 {}, /*createIfMissing=*/true };
    }
    // Pinned: it is fully classified, so hiding it behind "Show all games"
    // only made working support undiscoverable.
    bool    pinned()           const override { return true; }
    QStringList scriptExtenderLoaders() const override { return {QStringLiteral("f4se_loader.exe")}; }
};

class FalloutNVAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("falloutnewvegas"); }
    QString displayName() const override { return QStringLiteral("Fallout: New Vegas"); }
    QString steamAppId()  const override { return QStringLiteral("22380"); }
    SteamLayout steamLayout() const override {
        return {"Fallout New Vegas", "FalloutNV.exe", "FalloutNVLauncher.exe"};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"Fallout New Vegas Ultimate Edition", "FalloutNV.exe", "FalloutNVLauncher.exe"},
        {"Fallout New Vegas",                   "FalloutNV.exe", "FalloutNVLauncher.exe"},
    }; }
    QStringList lutrisTokens() const override { return {"fallout", "new", "vegas"}; }
    QString lootSlug()         const override { return QStringLiteral("FalloutNV"); }
    bool    pinned()           const override { return true; }
    LoadOrderStyle loadOrderStyle() const override { return LoadOrderStyle::TimestampPluginsTxt; }
    QString dataSubdir()       const override { return QStringLiteral("Data"); }
    QString localAppDataName() const override { return QStringLiteral("FalloutNV"); }
    QString myGamesName()      const override { return QStringLiteral("FalloutNV"); }
    ArchiveConfig archiveConfig() const override {
        return { ArchiveConfig::Style::GamebryoArchiveList,
                 QStringLiteral("Fallout.ini"), QStringLiteral(".bsa"),
                 kFalloutNVVanillaBsas, /*createIfMissing=*/false };
    }
    QStringList scriptExtenderLoaders() const override { return {QStringLiteral("nvse_loader.exe")}; }
};

// GOG-only Fallout 4 release with the London mod pre-applied. Empty
// steamAppId so we don't pop a "buy on Steam" prompt; hasLauncher() false
// hides the Steam launcher button.
class FalloutLondonAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("falloutlondon"); }
    QString displayName() const override { return QStringLiteral("Fallout: London"); }
    SteamLayout steamLayout() const override {
        // Wabbajack-style installs sometimes reuse Fallout 4's Steam dir;
        // let the locator find it even though we don't launch via Steam.
        return {"Fallout 4", "Fallout4.exe", "Fallout4Launcher.exe"};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"Fallout London", "Fallout4.exe", "Fallout4Launcher.exe"},
    }; }
    QStringList lutrisTokens() const override { return {"fallout", "london"}; }
    bool        hasLauncher()  const override { return false; }
};

// -- Skyrim total conversions (run on the SE engine) -------------------
class SkywindAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("skywind"); }
    QString displayName() const override { return QStringLiteral("Skywind"); }
    QString steamAppId()  const override { return QStringLiteral("489830"); } // SSE
    SteamLayout steamLayout() const override {
        return {"Skyrim Special Edition", "SkyrimSE.exe", "SkyrimSELauncher.exe"};
    }
};

class SkyblivionAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("skyblivion"); }
    QString displayName() const override { return QStringLiteral("Skyblivion"); }
    QString steamAppId()  const override { return QStringLiteral("489830"); } // SSE
    SteamLayout steamLayout() const override {
        return {"Skyrim Special Edition", "SkyrimSE.exe", "SkyrimSELauncher.exe"};
    }
};

// -- CD Projekt RED ----------------------------------------------------
class Cyberpunk2077Adapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("cyberpunk2077"); }
    QString displayName() const override { return QStringLiteral("Cyberpunk 2077"); }
    QString steamAppId()  const override { return QStringLiteral("1091500"); }
    SteamLayout steamLayout() const override {
        return {"Cyberpunk 2077", "bin/x64/Cyberpunk2077.exe", ""};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"Cyberpunk 2077", "bin/x64/Cyberpunk2077.exe", ""},
    }; }
    QStringList lutrisTokens() const override { return {"cyberpunk"}; }
};

class WitcherAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("witcher"); }
    QString displayName() const override { return QStringLiteral("The Witcher"); }
    QString steamAppId()  const override { return QStringLiteral("20900"); }
    SteamLayout steamLayout() const override {
        return {"The Witcher Enhanced Edition", "System/witcher.exe", ""};
    }
    QStringList lutrisTokens() const override { return {"witcher"}; }
};

class Witcher2Adapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("witcher2"); }
    QString displayName() const override { return QStringLiteral("The Witcher 2"); }
    QString steamAppId()  const override { return QStringLiteral("20920"); }
    SteamLayout steamLayout() const override {
        return {"The Witcher 2", "bin/witcher2.exe", ""};
    }
    QStringList lutrisTokens() const override { return {"witcher", "2"}; }
};

class Witcher3Adapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("witcher3"); }
    QString displayName() const override { return QStringLiteral("The Witcher 3"); }
    QString steamAppId()  const override { return QStringLiteral("292030"); }
    SteamLayout steamLayout() const override {
        return {"The Witcher 3 Wild Hunt", "bin/x64/witcher3.exe", ""};
    }
    QStringList lutrisTokens() const override { return {"witcher", "3"}; }
};

// -- Hello Games / ConcernedApe ----------------------------------------
class NoMansSkyAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("nomanssky"); }
    QString displayName() const override { return QStringLiteral("No Man's Sky"); }
    QString steamAppId()  const override { return QStringLiteral("275850"); }
    SteamLayout steamLayout() const override {
        return {"No Man's Sky", "Binaries/NMS.exe", ""};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"No Man's Sky", "Binaries/NMS.exe", ""},
        {"No Mans Sky",  "Binaries/NMS.exe", ""},
    }; }
};

class StardewValleyAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("stardewvalley"); }
    QString displayName() const override { return QStringLiteral("Stardew Valley"); }
    QString steamAppId()  const override { return QStringLiteral("413150"); }
    SteamLayout steamLayout() const override {
        return {"Stardew Valley", "StardewValley.exe", ""};
    }
    QList<GogLayout> gogLayouts() const override { return {
        // GOG's Linux build uses the same folder name with no .exe; locator
        // falls through to the second entry.
        {"Stardew Valley", "StardewValley.exe", ""},
        {"Stardew Valley", "StardewValley",     ""},
    }; }
};

// -- Open-source engines ------------------------------------------------
class ArxFatalisAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("arxfatalis"); }
    QString displayName() const override { return QStringLiteral("Arx Fatalis"); }
    QString steamAppId()  const override { return QStringLiteral("1700"); }
    SteamLayout steamLayout() const override {
        return {"Arx Fatalis", "ArxFatalis.exe", ""};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"Arx Fatalis", "ArxFatalis.exe", ""},
    }; }
};

class OpenXcomAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("openxcom"); }
    QString displayName() const override { return QStringLiteral("OpenXcom"); }
    QString steamAppId()  const override { return QStringLiteral("7760"); }
    SteamLayout steamLayout() const override {
        return {"UFO Defense", "XCOM.EXE", ""};
    }
};

class OpenXcomExAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("openxcomex"); }
    QString displayName() const override { return QStringLiteral("OpenXcom Extended"); }
    QString steamAppId()  const override { return QStringLiteral("7760"); }
    SteamLayout steamLayout() const override {
        return {"UFO Defense", "XCOM.EXE", ""};
    }
};

// -- Gothic saga --------------------------------------------------------
class Gothic1Adapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("gothic1"); }
    QString displayName() const override { return QStringLiteral("Gothic"); }
    QString steamAppId()  const override { return QStringLiteral("65540"); }
    SteamLayout steamLayout() const override { return {"Gothic", "Gothic.exe", ""}; }
    QList<GogLayout> gogLayouts() const override { return {
        {"Gothic",                  "Gothic.exe", ""},
        {"Gothic Universe Edition", "Gothic.exe", ""},
    }; }
};

// Gothic II is managed through OpenGothic, the open re-implementation of its
// engine, which is what makes it playable natively on Linux at all - the same
// relationship Morrowind has with OpenMW.
//
// Two fields here are not what they look like:
//
//   * The exe is system/Gothic2.exe, not Gothic2.exe. It really does live one
//     level down, next to GOTHIC.INI and the mod inis, and the storefront
//     locators check the path exists - a root-level spelling simply never
//     matched and the game was never detected.
//
//   * dataSubdir is "..", because the deploy target is the folder ABOVE the
//     exe. A Gothic mod is packaged as Data/ (its archives) plus system/ (its
//     ini), so it overlays the game root; deploying into system/ would bury
//     both. bethesdaResolveDataDir cleans the path, so this resolves to the
//     root itself.
//
// No LOOT (it does not know the game), no plugins.txt (the engine has no
// plugin list: what loads is decided by the generated -game: ini and the
// archive header stamps - see opengothic.h), and no script extender.
class Gothic2Adapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("gothic2"); }
    QString displayName() const override { return QStringLiteral("Gothic II (OpenGothic)"); }
    QString steamAppId()  const override { return QStringLiteral("39510"); }
    SteamLayout steamLayout() const override { return {"Gothic II", "system/Gothic2.exe", ""}; }
    QList<GogLayout> gogLayouts() const override { return {
        {"Gothic II Gold Edition", "system/Gothic2.exe", ""},
        {"Gothic 2 Gold Edition",  "system/Gothic2.exe", ""},
        {"Gothic II",              "system/Gothic2.exe", ""},
    }; }
    QStringList lutrisTokens() const override { return {"gothic", "2"}; }
    bool    isOpenGothic() const override { return true; }
    // Nexus has a Gothic 2 section (135 mods) but no mod-manager integration
    // for it, so every file there is a manual download.
    bool    manualDownloadsOnly() const override { return true; }
    QString dataSubdir()   const override { return QStringLiteral(".."); }
    bool    overlayDeploy() const override { return true; }
    // GothicStarter.exe is the original Windows launcher and picks the mod ini
    // itself; under OpenGothic that job is the generated ini's, so there is no
    // launcher button to offer.
    bool    hasLauncher()  const override { return false; }
    bool    pinned()       const override { return true; }
};

class Gothic3Adapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("gothic3"); }
    QString displayName() const override { return QStringLiteral("Gothic 3"); }
    QString steamAppId()  const override { return QStringLiteral("39600"); }
    SteamLayout steamLayout() const override { return {"Gothic 3", "Gothic3.exe", ""}; }
    QList<GogLayout> gogLayouts() const override { return {
        {"Gothic 3", "Gothic3.exe", ""},
    }; }
};

class Gothic3FGAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("gothic3fg"); }
    QString displayName() const override { return QStringLiteral("Gothic 3: Forsaken Gods"); }
    QString steamAppId()  const override { return QStringLiteral("39640"); }
    SteamLayout steamLayout() const override {
        return {"Gothic 3 Forsaken Gods Enhanced Edition", "Gothic3FG.exe", ""};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"Gothic 3 - Forsaken Gods Enhanced Edition", "Gothic3FG.exe", ""},
    }; }
};

class Gothic1RemakeAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("gothic1remake"); }
    QString displayName() const override { return QStringLiteral("Gothic 1 Remake"); }
    QString steamAppId()  const override { return QStringLiteral("1291550"); }
    SteamLayout steamLayout() const override {
        return {"Gothic 1 Remake", "Gothic_Remake.exe", ""};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"Gothic 1 Remake", "Gothic_Remake.exe", ""},
        {"Gothic Remake",   "Gothic_Remake.exe", ""},
    }; }
};

class ArcaniaAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("arcania"); }
    QString displayName() const override { return QStringLiteral("ArcaniA"); }
    QString steamAppId()  const override { return QStringLiteral("40630"); }
    SteamLayout steamLayout() const override {
        return {"ArcaniA", "ArcaniA.exe", ""};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"ArcaniA - Gothic 4",     "ArcaniA.exe", ""},
        {"ArcaniA Complete Tale",  "ArcaniA.exe", ""},
        {"ArcaniA",                "ArcaniA.exe", ""},
    }; }
};

// -- Dark Souls saga ----------------------------------------------------
// Souls games have no plugin system and no Data/ folder: the engine reads its
// content out of the folder that holds the .exe, so deploying a mod means
// overlaying its files straight into that folder (dataSubdir "." - see
// GameAdapter). Everything else follows from the defaults, which is the point:
//   - loadOrderStyle stays Unknown. There is no Plugins.txt; conflicts resolve
//     purely last-writer-wins down the list, which is what the deploy pass
//     already does.
//   - archiveConfig stays None. No ini tells the engine what to load.
//   - lootSlug stays empty. LOOT does not know these games.
class DarkSoulsAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("darksouls"); }
    QString displayName() const override { return QStringLiteral("Dark Souls"); }
    QString steamAppId()  const override { return QStringLiteral("211420"); }
    SteamLayout steamLayout() const override {
        return {"Dark Souls Prepare to Die Edition", "DARKSOULS.exe", ""};
    }
};

class DarkSoulsRemasteredAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("darksoulsremastered"); }
    QString displayName() const override { return QStringLiteral("Dark Souls: Remastered"); }
    QString steamAppId()  const override { return QStringLiteral("570940"); }
    SteamLayout steamLayout() const override {
        return {"DARK SOULS REMASTERED", "DarkSoulsRemastered.exe", ""};
    }
};

// The two DS2 editions share a layout (Game/DarkSoulsII.exe) but are separate
// Steam apps with separate Proton prefixes and separate saves, so each gets its
// own profile rather than one "Dark Souls II" with a variant switch. The
// original is the DX9 build; Scholar is the DX11 remaster and the one nearly
// every current mod - DS2LightingEngine included - targets.
class DarkSouls2Adapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("darksouls2"); }
    QString displayName() const override { return QStringLiteral("Dark Souls II"); }
    QString steamAppId()  const override { return QStringLiteral("236430"); }
    SteamLayout steamLayout() const override {
        return {"Dark Souls II", "Game/DarkSoulsII.exe", ""};
    }
    QString dataSubdir()  const override { return QStringLiteral("."); }
};

class DarkSouls2SOTFSAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("darksouls2sotfs"); }
    QString displayName() const override { return QStringLiteral("Dark Souls II: Scholar of the First Sin"); }
    QString steamAppId()  const override { return QStringLiteral("335300"); }
    SteamLayout steamLayout() const override {
        return {"Dark Souls II Scholar of the First Sin", "Game/DarkSoulsII.exe", ""};
    }
    QString dataSubdir()  const override { return QStringLiteral("."); }
    // Pinned: it is the edition mods are written for, and hiding a working
    // setup behind "Show all games" only makes it undiscoverable. The original
    // stays unpinned - same support, one menu entry instead of two.
    bool    pinned()      const override { return true; }
};

class DarkSouls3Adapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("darksouls3"); }
    QString displayName() const override { return QStringLiteral("Dark Souls III"); }
    QString steamAppId()  const override { return QStringLiteral("374320"); }
    SteamLayout steamLayout() const override {
        return {"DARK SOULS III", "Game/DarkSoulsIII.exe", ""};
    }
};

// -- Cold Symmetry ------------------------------------------------------
class MortalShellAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("mortalshell"); }
    QString displayName() const override { return QStringLiteral("Mortal Shell"); }
    QString steamAppId()  const override { return QStringLiteral("1110790"); }
    SteamLayout steamLayout() const override {
        return {"Mortal Shell",
                "MortalShell/Binaries/Win64/MortalShell-Win64-Shipping.exe", ""};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"Mortal Shell",
         "MortalShell/Binaries/Win64/MortalShell-Win64-Shipping.exe", ""},
    }; }
};

// -- Registry: every game we support ----------------------------------
// Lazily built to dodge static-init-order issues; callers use the accessors.

// std::vector, not QList: QList::reserve copy-constructs existing elements
// when growing, which won't compile for move-only unique_ptr.
const std::vector<std::unique_ptr<GameAdapter>> &kAdapters()
{
    static const auto *const list = []() {
        auto *out = new std::vector<std::unique_ptr<GameAdapter>>;
        out->reserve(32);
        // Order matters: pinned() entries show in the toolbar's "switch game"
        // menu in this order.
        out->push_back(std::make_unique<MorrowindAdapter>());
        out->push_back(std::make_unique<FalloutNVAdapter>());
        out->push_back(std::make_unique<SkyrimSpecialEditionAdapter>());
        out->push_back(std::make_unique<SkyrimAnniversaryEditionAdapter>());
        out->push_back(std::make_unique<SkyrimAdapter>());
        out->push_back(std::make_unique<EnderalSEAdapter>());
        out->push_back(std::make_unique<EnderalAdapter>());
        out->push_back(std::make_unique<StarfieldAdapter>());
        out->push_back(std::make_unique<OblivionAdapter>());
        out->push_back(std::make_unique<OblivionRemasteredAdapter>());
        out->push_back(std::make_unique<Fallout3Adapter>());
        out->push_back(std::make_unique<Fallout4Adapter>());
        out->push_back(std::make_unique<FalloutLondonAdapter>());
        out->push_back(std::make_unique<SkywindAdapter>());
        out->push_back(std::make_unique<SkyblivionAdapter>());
        out->push_back(std::make_unique<Cyberpunk2077Adapter>());
        out->push_back(std::make_unique<WitcherAdapter>());
        out->push_back(std::make_unique<Witcher2Adapter>());
        out->push_back(std::make_unique<Witcher3Adapter>());
        out->push_back(std::make_unique<NoMansSkyAdapter>());
        out->push_back(std::make_unique<StardewValleyAdapter>());
        out->push_back(std::make_unique<ArxFatalisAdapter>());
        out->push_back(std::make_unique<OpenXcomAdapter>());
        out->push_back(std::make_unique<OpenXcomExAdapter>());
        out->push_back(std::make_unique<Gothic1Adapter>());
        out->push_back(std::make_unique<Gothic2Adapter>());
        out->push_back(std::make_unique<Gothic3Adapter>());
        out->push_back(std::make_unique<Gothic3FGAdapter>());
        out->push_back(std::make_unique<Gothic1RemakeAdapter>());
        out->push_back(std::make_unique<ArcaniaAdapter>());
        out->push_back(std::make_unique<DarkSoulsAdapter>());
        out->push_back(std::make_unique<DarkSoulsRemasteredAdapter>());
        out->push_back(std::make_unique<DarkSouls2Adapter>());
        out->push_back(std::make_unique<DarkSouls2SOTFSAdapter>());
        out->push_back(std::make_unique<DarkSouls3Adapter>());
        out->push_back(std::make_unique<MortalShellAdapter>());
        return out;
    }();
    return *list;
}

} // namespace

bool GameAdapter::hasLauncher() const
{
    if (!steamLayout().launcher.isEmpty()) return true;
    const auto layouts = gogLayouts();
    return std::any_of(layouts.begin(), layouts.end(),
                       [](const GogLayout &g) { return !g.launcher.isEmpty(); });
}

namespace GameAdapterRegistry {

const GameAdapter *find(const QString &id)
{
    for (const auto &a : kAdapters())
        if (a->id() == id) return a.get();
    return nullptr;
}

QList<const GameAdapter *> all()
{
    QList<const GameAdapter *> out;
    out.reserve(static_cast<qsizetype>(kAdapters().size()));
    for (const auto &a : kAdapters()) out.append(a.get());
    return out;
}

QList<const GameAdapter *> builtin()
{
    QList<const GameAdapter *> out;
    for (const auto &a : kAdapters())
        if (a->builtin()) out.append(a.get());
    return out;
}

QList<const GameAdapter *> pinned()
{
    QList<const GameAdapter *> out;
    for (const auto &a : kAdapters())
        if (a->pinned()) out.append(a.get());
    return out;
}

} // namespace GameAdapterRegistry
