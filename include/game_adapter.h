#ifndef GAME_ADAPTER_H
#define GAME_ADAPTER_H

// GameAdapter - one impl per supported game.  Localizes the bits of
// per-game knowledge that used to live spread across half a dozen
// QHash<QString, QString> tables in game_profiles.cpp and mainwindow.cpp:
// Steam app ID, Steam install layout, GOG install layouts, Lutris yml
// match tokens, LOOT slug, "is the canonical engine OpenMW", "does
// this game ship a separate launcher.exe", and the kBuiltinGames /
// kPinned menu data.
//
// Adding a new game is now "implement class FooGame : public GameAdapter"
// + register it in src/game_adapters.cpp's table, instead of editing 7
// scattered hash tables and risking missing one.
//
// The path-detection generics (findSteamExe, findGogExe, findLutrisExe)
// live in src/game_adapters.cpp -- they consume the adapter's data and
// know nothing per-game.

#include <QList>
#include <QString>
#include <QStringList>

class GameAdapter {
public:
    virtual ~GameAdapter() = default;

    // Stable lowercase id used as the profile key on disk and in
    // QSettings.  Must never change once a release ships, or existing
    // users lose their profile.
    virtual QString id() const = 0;

    // Human-readable label shown in menus / wizards.  May be localized
    // later -- currently English-only matches the rest of the UI.
    virtual QString displayName() const = 0;

    // Default mods-dir folder name suggested by the first-run wizard.
    // Always a relative name; the wizard prepends a writable parent.
    virtual QString defaultModsDirName() const { return id() + "_mods"; }

    // -- Steam ----------------------------------------------------------
    // Empty when the game isn't on Steam (e.g. Fallout: London is GOG-only).
    virtual QString steamAppId() const { return {}; }

    struct SteamLayout {
        // Subdirectory under steamapps/common/.  Trailing " goty" is
        // probed with a .chop(5) fallback by the locator (matches the
        // pre-adapter code's behaviour).
        QString folder;
        QString exe;       // relative to folder
        QString launcher;  // relative to folder; empty = no launcher.exe
    };
    virtual SteamLayout steamLayout() const { return {}; }

    // -- GOG / Heroic ---------------------------------------------------
    // Multiple candidates because GOG installers ship with varying
    // folder names depending on edition / vintage / lgogdownloader naming.
    // Order is preference: first match wins.
    struct GogLayout {
        QString folder;
        QString exe;
        QString launcher;  // empty = no launcher.exe
    };
    virtual QList<GogLayout> gogLayouts() const { return {}; }

    // -- Lutris ---------------------------------------------------------
    // Tokens that must ALL appear in a Lutris config yml's filename for
    // it to be considered a match.  Empty = don't probe Lutris.
    virtual QStringList lutrisTokens() const { return {}; }

    // -- LOOT -----------------------------------------------------------
    // LOOT --game <slug> argument.  Empty = LOOT doesn't support this
    // game; the toolbar's "Sort with LOOT" entry hides accordingly.
    virtual QString lootSlug() const { return {}; }

    // -- Launch / engine flags -----------------------------------------
    // True iff this game launches via OpenMW (Morrowind today; Oblivion
    // Remastered etc. when openmw.cfg sync grows for them).  Drives the
    // OpenMW-specific toolbar buttons + the host of "is this Morrowind"
    // gates around openmw.cfg sync, plugin parsing, and BSA discovery.
    virtual bool isMorrowind() const { return false; }

    // True if the game ships a separate launcher .exe (Bethesda titles)
    // and the "Launch launcher" toolbar button should be shown.  Default
    // is "yes if any layout declares a launcher", which covers the
    // typical case automatically.  Overridable for total conversions
    // like Fallout: London where the launcher is irrelevant.
    virtual bool hasLauncher() const;

    // Inclusion in the toolbar's "switch game" pinned section.
    // Pinned games appear at the top of the menu in a fixed order even
    // when the user hasn't created a profile for them yet (greyed out).
    virtual bool pinned() const { return false; }

    // First-run wizard chooser-eligible.  True for the canonical
    // OpenMW + a small curated list; expanded as per-game install/
    // launch paths get tested.  Drives kBuiltinGames.
    virtual bool builtin() const { return isMorrowind(); }
};

namespace GameAdapterRegistry {

// Lookup by stable id.  Returns nullptr for unknown ids.
const GameAdapter *find(const QString &id);

// All registered adapters in registration order.
QList<const GameAdapter *> all();

// Subset for the first-run wizard's chooser.
QList<const GameAdapter *> builtin();

// Subset for the toolbar's pinned menu.  Returned in registration
// order, which is the order users see them.
QList<const GameAdapter *> pinned();

} // namespace GameAdapterRegistry

#endif // GAME_ADAPTER_H
