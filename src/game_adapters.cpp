// One adapter class per supported game.  See include/game_adapter.h for
// the interface and the why.  Adding a new game = drop a class here and
// register it in `kAdapters` at the bottom; everything that consumes the
// data (Steam locator, GOG locator, LOOT slug, pinned-menu order,
// first-run wizard chooser) picks it up automatically.
//
// Per-class overrides are intentionally one-liners.  The default impls
// in GameAdapter cover "missing field = not on this storefront / no
// LOOT support / etc." so a class only spells out what's actually
// distinct about its game.

#include "game_adapter.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace {

// -- OpenMW (Morrowind) ------------------------------------------------
// Canonical engine for the Morrowind profile -- everything OpenMW-
// specific (openmw.cfg sync, plugin parser, BSA discovery) gates on
// `isMorrowind()` returning true here.  GOG GOTY edition is the typical
// install; Steam exists but is rare among OpenMW users.
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
    bool        pinned()             const override { return true; }
    bool        builtin()            const override { return true; }
};

// -- Bethesda titles ---------------------------------------------------
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
};

class SkyrimAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("skyrim"); }
    QString displayName() const override { return QStringLiteral("Skyrim"); }
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
};

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
        // Trailing " goty" is probed via .chop(5) fallback by the locator.
        return {"Fallout 3 goty", "Fallout3.exe", "Fallout3Launcher.exe"};
    }
    QList<GogLayout> gogLayouts() const override { return {
        {"Fallout 3 GOTY",                       "Fallout3.exe", "Fallout3Launcher.exe"},
        {"Fallout 3 Game of the Year Edition",   "Fallout3.exe", "Fallout3Launcher.exe"},
        {"Fallout 3",                            "Fallout3.exe", "Fallout3Launcher.exe"},
    }; }
    QStringList lutrisTokens() const override { return {"fallout", "3"}; }
    QString lootSlug()         const override { return QStringLiteral("Fallout3"); }
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
};

// Total conversion: GOG-only release of Fallout 4 with the London mod
// pre-applied.  steamAppId is intentionally empty so we don't pop a
// "buy on Steam" prompt; the Steam-side launcher button hides via
// hasLauncher() returning false.
class FalloutLondonAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("falloutlondon"); }
    QString displayName() const override { return QStringLiteral("Fallout: London"); }
    SteamLayout steamLayout() const override {
        // Sometimes Wabbajack-style installs reuse Fallout 4's Steam
        // dir; keep the locator able to find it even though we won't
        // launch via Steam.
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
        // GOG ships a Linux-native build under the same folder name with
        // no `.exe` suffix; the locator falls through to the second entry.
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

class Gothic2Adapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("gothic2"); }
    QString displayName() const override { return QStringLiteral("Gothic II"); }
    QString steamAppId()  const override { return QStringLiteral("39510"); }
    SteamLayout steamLayout() const override { return {"Gothic II", "Gothic2.exe", ""}; }
    QList<GogLayout> gogLayouts() const override { return {
        {"Gothic II Gold Edition", "Gothic2.exe", ""},
        {"Gothic 2 Gold Edition",  "Gothic2.exe", ""},
        {"Gothic II",              "Gothic2.exe", ""},
    }; }
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

class DarkSouls2Adapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("darksouls2"); }
    QString displayName() const override { return QStringLiteral("Dark Souls II"); }
    QString steamAppId()  const override { return QStringLiteral("236430"); }
    SteamLayout steamLayout() const override {
        return {"Dark Souls II", "Game/DarkSoulsII.exe", ""};
    }
};

class DarkSouls2SOTFSAdapter : public GameAdapter {
public:
    QString id()          const override { return QStringLiteral("darksouls2sotfs"); }
    QString displayName() const override { return QStringLiteral("Dark Souls II: Scholar of the First Sin"); }
    QString steamAppId()  const override { return QStringLiteral("335300"); }
    SteamLayout steamLayout() const override {
        return {"Dark Souls II Scholar of the First Sin", "Game/DarkSoulsII.exe", ""};
    }
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

// -- Registry (one source of truth for "all games we support") --------
//
// Defined as an inline list of unique_ptrs constructed lazily so static-
// initialization-order doesn't bite -- callers always go through the
// registry accessors which guarantee the list is built first.

// std::vector rather than QList because QList::reserve copy-constructs
// existing elements when growing detached storage, which doesn't compile
// for move-only `std::unique_ptr` payloads.  The container is process-
// global anyway; QList's COW shape buys us nothing here.
const std::vector<std::unique_ptr<GameAdapter>> &kAdapters()
{
    static const auto *const list = []() {
        auto *out = new std::vector<std::unique_ptr<GameAdapter>>;
        out->reserve(32);
        // Order matters: pinned() entries appear in the toolbar's
        // "switch game" menu in the order they're listed here.
        out->push_back(std::make_unique<MorrowindAdapter>());
        out->push_back(std::make_unique<FalloutNVAdapter>());
        out->push_back(std::make_unique<SkyrimSpecialEditionAdapter>());
        out->push_back(std::make_unique<SkyrimAdapter>());
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
