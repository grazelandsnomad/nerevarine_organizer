#ifndef GAME_PROFILES_H
#define GAME_PROFILES_H

#include <QList>
#include <QObject>
#include <QString>

struct GameProfile {
    QString id;                   // NexusMods game slug (e.g. "morrowind", "skyrimspecialedition")
    QString displayName;          // Human-readable name shown in the UI
    QString modsDir;
    // OpenMW-specific (only meaningful when id == "morrowind")
    QString openmwPath;
    QString openmwLauncherPath;
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
