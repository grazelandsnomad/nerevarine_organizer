#ifndef GAME_PROFILES_H
#define GAME_PROFILES_H

#include <QList>
#include <QObject>
#include <QString>

// A modlist profile inside a GameProfile. Each has its own mods dir and its
// own modlist + load-order files, so the user can try a Wabbajack (or any
// alternate list) in isolation. Mod content on disk is never shared - installs
// land under the active profile's modsDir.
//
// modlistFilename / loadOrderFilename are bare filenames (MainWindow feeds
// them to resolveUserStatePath()). Migrated profiles keep the legacy
// `modlist_<gameId>.txt` name so existing users' state stays untouched on
// first launch; later profiles use `modlist_<gameId>__<profileName>.txt` to
// avoid collisions per game.
struct ModlistProfile {
    QString name;
    QString modsDir;
    QString modlistFilename;
    QString loadOrderFilename;
};

struct GameProfile {
    QString id;                   // NexusMods game slug (e.g. "morrowind", "skyrimspecialedition")
    QString displayName;          // Human-readable name shown in the UI
    // Same as activeModlist().modsDir so call sites reading gp.modsDir keep
    // working. Write via GameProfileRegistry::setActiveModsDir() to keep the
    // active profile in sync; direct writes are legacy and reconciled on save().
    QString modsDir;
    // OpenMW-specific (only meaningful when id == "morrowind")
    QString openmwPath;
    QString openmwLauncherPath;

    // Modlist profiles for this game. Always >=1 after load() - migration
    // makes a "Default" from any pre-existing modlist file + modsDir.
    QList<ModlistProfile> modlistProfiles;
    int                   activeModlistIdx = 0;

    // Active profile lookup. Reference into modlistProfiles when the index is
    // valid, else a static empty entry so callers never null-check.
    ModlistProfile&       activeModlist();
    const ModlistProfile& activeModlist() const;
};

class GameProfileRegistry : public QObject {
    Q_OBJECT
public:
    explicit GameProfileRegistry(QObject *parent = nullptr);

    // Reads the game list via Settings::gameIds(); on first run falls back to
    // a default Morrowind profile and migrates legacy modlist.txt.
    void load();

    // Persists all profiles + current id via Settings::.
    void save();

    bool isEmpty() const { return m_games.isEmpty(); }
    int  size()    const { return m_games.size(); }

    GameProfile&       current()             { return m_games[m_currentIdx]; }
    const GameProfile& current() const       { return m_games[m_currentIdx]; }
    int                currentIndex() const  { return m_currentIdx; }

    // Sets current index + persists via Settings::setCurrentGameId.
    // No-op for out-of-range or unchanged index.
    void setCurrentIndex(int idx);

    QList<GameProfile>&       games()       { return m_games; }
    const QList<GameProfile>& games() const { return m_games; }

    // -- Modlist profile management (operates on the current() game) ---

    // Switch active modlist profile by index within current(). Persists
    // `games/<id>/active_profile`; emits nothing - caller (MainWindow) handles
    // save/load of modlist + load order either side. No-op if invalid/unchanged.
    void setActiveModlistIndex(int idx);

    // Set modsDir on the current game's active profile and copy it onto
    // GameProfile::modsDir. Persists immediately.
    void setActiveModsDir(const QString &dir);

    // Append a new profile to current() with a unique name and filename
    // `modlist_<gameId>__<sanitized>.txt`. modsDir starts empty so the first
    // install prompts for a location. Returns the new index (size-1), or -1 if
    // name is empty / collides.
    int addModlistProfile(const QString &name);

    // Clone-empty: copies the source profile's modlist + load-order files but
    // starts with an empty modsDir so installs land fresh. Source files are
    // duplicated on disk under the new names before the entry is appended.
    // Returns the new index, or -1 if name is empty / collides.
    int cloneModlistProfile(int srcIdx, const QString &newName);

    // Remove a profile from current(). Optionally deletes the state files;
    // modsDir is never touched automatically (user may want to keep/re-attach
    // the mods). Refuses to remove the last profile. Returns true on success.
    // Removing the active one clamps activeModlistIdx into range.
    bool removeModlistProfile(int idx, bool deleteStateFiles);

    // Rename a profile, renaming its state files on disk to match. The legacy
    // "Default" profile keeps its migration filename even after rename -
    // moving it risks losing data on a crash mid-rename. Returns true on success.
    bool renameModlistProfile(int idx, const QString &newName);

    // Locators for the launch path: find the user's installed copy of a
    // non-OpenMW game on Steam, GOG, or Heroic.
    static QString steamAppId(const QString &gameId);
    static QString findSteamGameExe(const QString &gameId);
    static QString findSteamLauncherExe(const QString &gameId);
    static QString findGogGameExe(const QString &gameId, bool wantLauncher = false);
    static QString findHeroicGogAppId(const QString &installPathHint);
    // Scans ~/.config/lutris/games/ yml for an entry matching gameId by
    // slug/name; returns the absolute exe path if found.
    static QString findLutrisGameExe(const QString &gameId);

private:
    QList<GameProfile> m_games;
    int                m_currentIdx = 0;
};

#endif // GAME_PROFILES_H
