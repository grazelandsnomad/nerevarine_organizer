#ifndef GAME_PROFILES_H
#define GAME_PROFILES_H

#include <QList>
#include <QObject>
#include <QString>

// A modlist profile sits inside a GameProfile.  Each profile has its OWN
// mods dir and its OWN modlist + load-order files, so the user can test a
// Wabbajack (or any alternate modlist) in isolation without touching the
// existing setup.  Mod content on disk is never shared between profiles -
// installs always land under the active profile's modsDir.
//
// `modlistFilename` and `loadOrderFilename` are bare filenames (no path
// prefix); MainWindow feeds them to resolveUserStatePath().  Migrated
// profiles keep the legacy `modlist_<gameId>.txt` filename so existing users
// see their state untouched on first launch under the new code; profiles
// created later use `modlist_<gameId>__<profileName>.txt` so multiple
// profiles for the same game don't collide.
struct ModlistProfile {
    QString name;
    QString modsDir;
    QString modlistFilename;
    QString loadOrderFilename;
};

struct GameProfile {
    QString id;                   // NexusMods game slug (e.g. "morrowind", "skyrimspecialedition")
    QString displayName;          // Human-readable name shown in the UI
    // Mirrors activeModlist().modsDir so existing call sites that read
    // `gp.modsDir` keep working.  Writers should go through
    // GameProfileRegistry::setActiveModsDir() so the active modlist profile
    // stays in sync; direct writes here are a legacy shape and get
    // reconciled on save().
    QString modsDir;
    // OpenMW-specific (only meaningful when id == "morrowind")
    QString openmwPath;
    QString openmwLauncherPath;

    // Modlist profiles for this game.  Always at least one entry after
    // GameProfileRegistry::load() returns - migration auto-creates a
    // "Default" profile from any pre-existing modlist file + modsDir.
    QList<ModlistProfile> modlistProfiles;
    int                   activeModlistIdx = 0;

    // Active modlist profile lookup.  Returns a reference into
    // modlistProfiles when the index is valid; falls back to a static
    // empty entry otherwise so the caller is never null-checking.
    ModlistProfile&       activeModlist();
    const ModlistProfile& activeModlist() const;
};

class GameProfileRegistry : public QObject {
    Q_OBJECT
public:
    explicit GameProfileRegistry(QObject *parent = nullptr);

    // Reads QSettings("games/list"), populates the list, falls back to a
    // default Morrowind profile + migrates legacy modlist.txt on first run.
    void load();

    // Persists all profiles + the current id under "games/...".
    void save();

    bool isEmpty() const { return m_games.isEmpty(); }
    int  size()    const { return m_games.size(); }

    GameProfile&       current()             { return m_games[m_currentIdx]; }
    const GameProfile& current() const       { return m_games[m_currentIdx]; }
    int                currentIndex() const  { return m_currentIdx; }

    // Sets the current index + persists "games/current". No-op for
    // out-of-range indices and no-op if the index hasn't changed.
    void setCurrentIndex(int idx);

    QList<GameProfile>&       games()       { return m_games; }
    const QList<GameProfile>& games() const { return m_games; }

    // -- Modlist profile management (operates on the current() game) ---

    // Switch the active modlist profile by index within current().
    // Persists `games/<id>/active_profile`; emits no signal - the caller
    // (MainWindow) is responsible for save/load of modlist + load order
    // either side of the switch.  No-op for invalid index or no-change.
    void setActiveModlistIndex(int idx);

    // Set the modsDir on the current game's active modlist profile and
    // mirror it onto GameProfile::modsDir.  Persists immediately.
    void setActiveModsDir(const QString &dir);

    // Append a new modlist profile to current() with a unique name + the
    // canonical filename scheme `modlist_<gameId>__<sanitized>.txt`.  modsDir
    // starts empty so the first install can prompt for a location.
    // Returns the index of the new profile (always size-1 on success), or
    // -1 if `name` is empty / collides with an existing profile.
    int addModlistProfile(const QString &name);

    // Clone-empty: a new profile that copies the source profile's modlist +
    // load order files but starts with an EMPTY modsDir so installs land
    // somewhere fresh.  Source files are duplicated on disk under the new
    // filenames before the profile entry is appended.  Returns the new
    // profile's index, or -1 if `name` is empty / collides.
    int cloneModlistProfile(int srcIdx, const QString &newName);

    // Remove a profile from current().  Optionally deletes the modlist +
    // load-order files from disk; the profile's modsDir is never touched
    // automatically (the user might want to keep / re-attach the mods).
    // Refuses to remove the last remaining profile.  Returns true on
    // success.  If the active profile is removed, activeModlistIdx is
    // clamped into range (the next profile becomes active).
    bool removeModlistProfile(int idx, bool deleteStateFiles);

    // Rename a profile.  Renames the underlying state files on disk so the
    // canonical filename scheme stays consistent.  The legacy "Default"
    // profile (which adopted the migration filename) keeps its filename
    // even after rename - moving that file would lose the user's data on a
    // process crash mid-rename.  Returns true on success.
    bool renameModlistProfile(int idx, const QString &newName);

    // Static locator helpers - used by the launch path to find the user's
    // installed copy of a non-OpenMW game on Steam, GOG, or Heroic.
    static QString steamAppId(const QString &gameId);
    static QString findSteamGameExe(const QString &gameId);
    static QString findSteamLauncherExe(const QString &gameId);
    static QString findGogGameExe(const QString &gameId, bool wantLauncher = false);
    static QString findHeroicGogAppId(const QString &installPathHint);
    // Scans Lutris yml configs under ~/.config/lutris/games/ for an entry
    // whose slug/name matches gameId; returns the absolute exe path if found.
    static QString findLutrisGameExe(const QString &gameId);

private:
    QList<GameProfile> m_games;
    int                m_currentIdx = 0;
};

#endif // GAME_PROFILES_H
