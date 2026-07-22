#ifndef GAME_ADAPTER_H
#define GAME_ADAPTER_H

// GameAdapter - one impl per supported game. Gathers the per-game facts
// that used to be spread across ~6 QHash tables in game_profiles.cpp and
// mainwindow.cpp: Steam app ID + layout, GOG layouts, Lutris match
// tokens, LOOT slug, engine-is-OpenMW, separate launcher.exe, and the
// builtin/pinned menu data.
//
// New game = subclass GameAdapter + register it in game_adapters.cpp,
// instead of editing 7 hashes and missing one.
//
// The path-detection generics (findSteamExe/Gog/Lutris) in
// game_adapters.cpp consume the adapter data and know nothing per-game.

#include <QList>
#include <QString>
#include <QStringList>

// How a game writes plugin load order on disk; picks the writer for the
// future Bethesda sync path. Unknown until explicitly classified, so an
// unclassified game is never treated as managed.
enum class LoadOrderStyle {
    Unknown,             // not classified / not yet supported here
    OpenMW,              // openmw.cfg data=/content= lines (Morrowind)
    TimestampPluginsTxt, // Plugins.txt active set + file-mtime order (Oblivion, FO3, FNV)
    AsteriskPluginsTxt,  // Plugins.txt with '*'-prefixed active entries, in order (Skyrim SE, FO4)
};

class GameAdapter {
public:
    virtual ~GameAdapter() = default;

    // Stable lowercase id, the profile key on disk and in QSettings.
    // Never change it after a release or users lose their profile.
    virtual QString id() const = 0;

    // Label shown in menus / wizards. English-only for now.
    virtual QString displayName() const = 0;

    // Default mods-dir name the wizard suggests. Relative; the wizard
    // prepends a writable parent.
    virtual QString defaultModsDirName() const { return id() + "_mods"; }

    // -- Steam ----------------------------------------------------------
    // Empty if not on Steam (Fallout: London is GOG-only).
    virtual QString steamAppId() const { return {}; }

    struct SteamLayout {
        // Subdir under steamapps/common/. Locator also tries a .chop(5)
        // fallback for a trailing " goty".
        QString folder;
        QString exe;       // relative to folder
        QString launcher;  // relative to folder; empty = no launcher.exe
    };
    virtual SteamLayout steamLayout() const { return {}; }

    // -- GOG / Heroic ---------------------------------------------------
    // Multiple candidates: GOG folder names vary by edition / vintage /
    // lgogdownloader naming. Preference order, first match wins.
    struct GogLayout {
        QString folder;
        QString exe;
        QString launcher;  // empty = no launcher.exe
    };
    virtual QList<GogLayout> gogLayouts() const { return {}; }

    // -- Lutris ---------------------------------------------------------
    // Tokens that must ALL appear in a Lutris config yml filename to
    // match. Empty = don't probe Lutris.
    virtual QStringList lutrisTokens() const { return {}; }

    // -- LOOT -----------------------------------------------------------
    // LOOT --game <slug>. Empty = unsupported; the toolbar's "Sort with
    // LOOT" hides accordingly.
    virtual QString lootSlug() const { return {}; }

    // -- Launch / engine flags -----------------------------------------
    // True iff this game launches via OpenMW (Morrowind today). Gates the
    // OpenMW toolbar buttons and the "is this Morrowind" checks around
    // openmw.cfg sync, plugin parsing, and BSA discovery.
    virtual bool isMorrowind() const { return false; }

    // -- Mod deployment / load order (Bethesda titles) -----------------
    // Unconsumed yet: where load order and per-user config live, for a
    // later non-OpenMW deploy/sync path. Defaults = "not a managed
    // Bethesda title".

    // How load order is written. Default Unknown: acted on only once
    // explicitly classified. See LoadOrderStyle.
    virtual LoadOrderStyle loadOrderStyle() const { return LoadOrderStyle::Unknown; }

    // The single folder the engine loads content from ("Data" for
    // Bethesda). Empty for engines with no single root (OpenMW reads
    // many data= paths).
    virtual QString dataSubdir() const { return {}; }

    // Per-user config folders inside the Proton prefix:
    // AppData/Local/<localAppDataName> holds Plugins.txt;
    // Documents/My Games/<myGamesName> holds the engine .ini. Empty if N/A.
    virtual QString localAppDataName() const { return {}; }
    virtual QString myGamesName()      const { return {}; }

    // How this engine has to be told to load the content we deployed, and in
    // which ini. Getting this wrong is invisible: the mods land in Data/, the
    // plugin list is right, and nothing happens in game.
    struct ArchiveConfig {
        enum class Style {
            None,                 // nothing to do (Skyrim SE loads loose files
                                  // and name-matched BSAs by itself)
            GamebryoArchiveList,  // Oblivion/FO3/FNV: SArchiveList +
                                  // bInvalidateOlderFiles + empty SInvalidationFile
            ModernCustomIni,      // FO4/Starfield: bInvalidateOlderFiles +
                                  // empty sResourceDataDirsFinal
        };
        Style   style = Style::None;
        QString iniName;         // "Oblivion.ini", "Fallout4Custom.ini", ...
        QString archiveSuffix;   // ".bsa" or ".ba2"
        // Gamebryo only, and only used when the ini has no SArchiveList at all
        // (a launcher normally writes one). Empty means "we do not know this
        // game's vanilla archives" - then we never invent a list, because a
        // wrong one would leave the base game's own archives unloaded.
        QStringList vanillaSeed;
        // The *Custom.ini files are user overrides the game does not ship, so
        // they have to be created. A missing Oblivion.ini/Fallout.ini instead
        // means we resolved the wrong directory and must not invent one.
        bool createIfMissing = false;
    };
    virtual ArchiveConfig archiveConfig() const { return {}; }

    // Script-extender loaders (OBSE/SKSE/F4SE/...), most-preferred first.
    // When launching an exe directly (not via Steam, where the extender's
    // own loader auto-loads), we run one of these instead of the game exe
    // so extender mods work. Empty = no known extender.
    virtual QStringList scriptExtenderLoaders() const { return {}; }

    // Game ships a separate launcher .exe and the "Launch launcher"
    // button shows. Default: yes if any layout declares a launcher.
    // Override for total conversions (Fallout: London) where it's moot.
    virtual bool hasLauncher() const;

    // Show in the toolbar "switch game" pinned section: fixed order at
    // the top, greyed out if no profile exists yet.
    virtual bool pinned() const { return false; }

    // Eligible for the first-run chooser. True for OpenMW + a small
    // curated list; grows as install/launch paths get tested.
    virtual bool builtin() const { return isMorrowind(); }
};

namespace GameAdapterRegistry {

// Lookup by id; nullptr if unknown.
const GameAdapter *find(const QString &id);

// All registered adapters in registration order.
QList<const GameAdapter *> all();

// Subset for the first-run wizard's chooser.
QList<const GameAdapter *> builtin();

// Subset for the toolbar's pinned menu, in registration (display) order.
QList<const GameAdapter *> pinned();

} // namespace GameAdapterRegistry

#endif // GAME_ADAPTER_H
