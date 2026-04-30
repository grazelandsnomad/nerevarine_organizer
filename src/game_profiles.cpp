#include "game_profiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <Qt>

GameProfileRegistry::GameProfileRegistry(QObject *parent)
    : QObject(parent)
{
}

void GameProfileRegistry::load()
{
    QSettings s;
    QStringList ids = s.value("games/list").toStringList();
    ids.removeAll(QString());

    if (ids.isEmpty()) {
        // First-run: create the default OpenMW profile.
        GameProfile morrowind;
        morrowind.id                 = "morrowind";
        morrowind.displayName        = "OpenMW (Morrowind)";
        morrowind.modsDir            = s.value("mods/dir",
            QDir::homePath() + "/Games/nerevarine_mods").toString();
        morrowind.openmwPath         = s.value("launch/openmw").toString();
        morrowind.openmwLauncherPath = s.value("launch/openmw_launcher").toString();
        m_games.append(morrowind);

        // Migrate modlist.txt → modlist_morrowind.txt if it exists.
        for (const QString &dir : {QCoreApplication::applicationDirPath() + "/..",
                                    QCoreApplication::applicationDirPath()}) {
            QString oldPath = QFileInfo(dir + "/modlist.txt").absoluteFilePath();
            if (QFile::exists(oldPath)) {
                QString newPath = QFileInfo(dir + "/modlist_morrowind.txt").absoluteFilePath();
                QFile::rename(oldPath, newPath);
                break;
            }
        }

        m_currentIdx = 0;
        save();
    } else {
        for (const QString &id : ids) {
            GameProfile gp;
            gp.id          = id;
            gp.displayName = s.value("games/" + id + "/name", id).toString();
            gp.modsDir     = s.value("games/" + id + "/mods_dir",
                QDir::homePath() + "/Games/" + id + "_mods").toString();
            gp.openmwPath         = s.value("games/" + id + "/openmw_path").toString();
            gp.openmwLauncherPath = s.value("games/" + id + "/openmw_launcher_path").toString();
            m_games.append(gp);
        }
        if (m_games.isEmpty()) {
            GameProfile morrowind;
            morrowind.id                 = "morrowind";
            morrowind.displayName        = "OpenMW (Morrowind)";
            morrowind.modsDir            = QDir::homePath() + "/Games/nerevarine_mods";
            morrowind.openmwPath         = s.value("launch/openmw").toString();
            morrowind.openmwLauncherPath = s.value("launch/openmw_launcher").toString();
            m_games.append(morrowind);
            save();
        }
        QString currentId = s.value("games/current", "morrowind").toString();
        m_currentIdx = 0;
        for (int i = 0; i < m_games.size(); ++i)
            if (m_games[i].id == currentId) { m_currentIdx = i; break; }
    }

    QDir().mkpath(m_games[m_currentIdx].modsDir);
}

void GameProfileRegistry::save()
{
    QSettings s;
    QStringList ids;
    for (const auto &gp : m_games) {
        ids << gp.id;
        s.setValue("games/" + gp.id + "/name",                 gp.displayName);
        s.setValue("games/" + gp.id + "/mods_dir",             gp.modsDir);
        s.setValue("games/" + gp.id + "/openmw_path",          gp.openmwPath);
        s.setValue("games/" + gp.id + "/openmw_launcher_path", gp.openmwLauncherPath);
    }
    s.setValue("games/list",    ids);
    s.setValue("games/current", m_games.isEmpty() ? "" : m_games[m_currentIdx].id);
}

void GameProfileRegistry::setCurrentIndex(int idx)
{
    if (idx < 0 || idx >= m_games.size() || idx == m_currentIdx) return;
    m_currentIdx = idx;
    QSettings().setValue("games/current", m_games[idx].id);
}

QString GameProfileRegistry::steamAppId(const QString &gameId)
{
    static const QHash<QString, QString> ids = {
        {"skyrimspecialedition", "489830"},
        {"skyrim",               "72850"},
        {"starfield",            "1716740"},
        {"fallout3",             "22370"},
        {"fallout4",             "377160"},
        {"falloutnewvegas",      "22380"},
        {"oblivion",             "22330"},
        {"cyberpunk2077",        "1091500"},
        {"witcher",              "20900"},
        {"witcher2",             "20920"},
        {"witcher3",             "292030"},
        {"nomanssky",            "275850"},
        {"stardewvalley",        "413150"},
        // Total conversions - share the base game's App ID.
        // falloutlondon intentionally omitted: canonical release is GOG, not
        // Steam. Using Fallout 4's Steam ID would launch the wrong game.
        {"skywind",              "489830"},
        {"skyblivion",           "489830"},
        // Open-source engines
        {"arxfatalis",           "1700"},
        {"openxcom",             "7760"},
        {"openxcomex",           "7760"},
        // Gothic saga
        {"gothic1",              "65540"},
        {"gothic2",              "39510"},
        {"gothic3",              "39600"},
        {"gothic3fg",            "39640"},
        {"gothic1remake",        "1291550"},
        // Arcania saga
        {"arcania",              "40630"},
        // Dark Souls saga
        {"darksouls",            "211420"},
        {"darksoulsremastered",  "570940"},
        {"darksouls2",           "236430"},
        {"darksouls2sotfs",      "335300"},
        {"darksouls3",           "374320"},
        // Mortal Shell
        {"mortalshell",          "1110790"},
    };
    return ids.value(gameId);
}

QString GameProfileRegistry::findSteamGameExe(const QString &gameId)
{
    struct GameExeInfo { QString folder; QString exe; };
    static const QHash<QString, GameExeInfo> info = {
        {"skyrimspecialedition", {"Skyrim Special Edition", "SkyrimSE.exe"}},
        {"skyrim",               {"Skyrim",                 "TESV.exe"}},
        {"starfield",            {"Starfield",              "Starfield.exe"}},
        {"fallout3",             {"Fallout 3 goty",         "Fallout3.exe"}},
        {"fallout4",             {"Fallout 4",              "Fallout4.exe"}},
        {"falloutnewvegas",      {"Fallout New Vegas",      "FalloutNV.exe"}},
        {"oblivion",             {"Oblivion",               "Oblivion.exe"}},
        {"cyberpunk2077",        {"Cyberpunk 2077",         "bin/x64/Cyberpunk2077.exe"}},
        {"witcher",              {"The Witcher Enhanced Edition",           "System/witcher.exe"}},
        {"witcher2",             {"The Witcher 2",                          "bin/witcher2.exe"}},
        {"witcher3",             {"The Witcher 3 Wild Hunt",                "bin/x64/witcher3.exe"}},
        {"nomanssky",            {"No Man's Sky",           "Binaries/NMS.exe"}},
        {"stardewvalley",        {"Stardew Valley",         "StardewValley.exe"}},
        {"falloutlondon",        {"Fallout 4",              "Fallout4.exe"}},
        {"skywind",              {"Skyrim Special Edition", "SkyrimSE.exe"}},
        {"skyblivion",           {"Skyrim Special Edition", "SkyrimSE.exe"}},
        {"arxfatalis",           {"Arx Fatalis",                               "ArxFatalis.exe"}},
        {"openxcom",             {"UFO Defense",                               "XCOM.EXE"}},
        {"openxcomex",           {"UFO Defense",                               "XCOM.EXE"}},
        {"gothic1",              {"Gothic",                                    "Gothic.exe"}},
        {"gothic2",              {"Gothic II",                                 "Gothic2.exe"}},
        {"gothic3",              {"Gothic 3",                                  "Gothic3.exe"}},
        {"gothic3fg",            {"Gothic 3 Forsaken Gods Enhanced Edition",   "Gothic3FG.exe"}},
        {"gothic1remake",        {"Gothic 1 Remake",                           "Gothic_Remake.exe"}},
        {"arcania",              {"ArcaniA",                                   "ArcaniA.exe"}},
        {"darksouls",            {"Dark Souls Prepare to Die Edition",         "DARKSOULS.exe"}},
        {"darksoulsremastered",  {"DARK SOULS REMASTERED",                     "DarkSoulsRemastered.exe"}},
        {"darksouls2",           {"Dark Souls II",                             "Game/DarkSoulsII.exe"}},
        {"darksouls2sotfs",      {"Dark Souls II Scholar of the First Sin",    "Game/DarkSoulsII.exe"}},
        {"darksouls3",           {"DARK SOULS III",                            "Game/DarkSoulsIII.exe"}},
        {"mortalshell",          {"Mortal Shell",                              "MortalShell/Binaries/Win64/MortalShell-Win64-Shipping.exe"}},
    };
    if (!info.contains(gameId)) return {};

    const auto &ei = info[gameId];

    const QString home = QDir::homePath();
    const QStringList roots = {
        home + "/.steam/steam/steamapps/common",
        home + "/.local/share/Steam/steamapps/common",
        "/mnt/games/Steam/steamapps/common",
    };

    for (const QString &root : roots) {
        QString path = root + "/" + ei.folder + "/" + ei.exe;
        if (QFile::exists(path))
            return path;
        if (ei.folder.endsWith(" goty", Qt::CaseInsensitive)) {
            QString altFolder = ei.folder;
            altFolder.chop(5);
            path = root + "/" + altFolder + "/" + ei.exe;
            if (QFile::exists(path))
                return path;
        }
    }
    return {};
}

QString GameProfileRegistry::findSteamLauncherExe(const QString &gameId)
{
    struct GameExeInfo { QString folder; QString exe; };
    static const QHash<QString, GameExeInfo> info = {
        {"skyrimspecialedition", {"Skyrim Special Edition", "SkyrimSELauncher.exe"}},
        {"skyrim",               {"Skyrim",                 "SkyrimLauncher.exe"}},
        {"fallout4",             {"Fallout 4",              "Fallout4Launcher.exe"}},
        {"falloutnewvegas",      {"Fallout New Vegas",      "FalloutNVLauncher.exe"}},
        {"oblivion",             {"Oblivion",               "OblivionLauncher.exe"}},
        {"fallout3",             {"Fallout 3 goty",         "Fallout3Launcher.exe"}},
        {"falloutlondon",        {"Fallout 4",              "Fallout4Launcher.exe"}},
        {"skywind",              {"Skyrim Special Edition", "SkyrimSELauncher.exe"}},
        {"skyblivion",           {"Skyrim Special Edition", "SkyrimSELauncher.exe"}},
    };
    if (!info.contains(gameId)) return {};

    const auto &ei = info[gameId];

    const QString home = QDir::homePath();
    const QStringList roots = {
        home + "/.steam/steam/steamapps/common",
        home + "/.local/share/Steam/steamapps/common",
        "/mnt/games/Steam/steamapps/common",
    };

    for (const QString &root : roots) {
        QString path = root + "/" + ei.folder + "/" + ei.exe;
        if (QFile::exists(path))
            return path;
        if (ei.folder.endsWith(" goty", Qt::CaseInsensitive)) {
            QString altFolder = ei.folder;
            altFolder.chop(5);
            path = root + "/" + altFolder + "/" + ei.exe;
            if (QFile::exists(path))
                return path;
        }
    }
    return {};
}

QString GameProfileRegistry::findGogGameExe(const QString &gameId, bool wantLauncher)
{
    struct GogGameInfo { QString folder; QString exe; QString launcherExe; };
    static const QHash<QString, QList<GogGameInfo>> gogInfo = {
        {"morrowind",            {{"The Elder Scrolls III Morrowind GOTY",          "Morrowind.exe",         ""},
                                  {"Morrowind",                                      "Morrowind.exe",         ""}}},
        {"skyrimspecialedition", {{"The Elder Scrolls V Skyrim Special Edition",    "SkyrimSE.exe",          "SkyrimSELauncher.exe"},
                                  {"Skyrim Special Edition",                         "SkyrimSE.exe",          "SkyrimSELauncher.exe"}}},
        {"skyrim",               {{"The Elder Scrolls V Skyrim Legendary Edition",  "TESV.exe",              "SkyrimLauncher.exe"},
                                  {"Skyrim Legendary Edition",                       "TESV.exe",              "SkyrimLauncher.exe"},
                                  {"Skyrim",                                         "TESV.exe",              "SkyrimLauncher.exe"}}},
        {"fallout3",             {{"Fallout 3 GOTY",                                "Fallout3.exe",          "Fallout3Launcher.exe"},
                                  {"Fallout 3 Game of the Year Edition",            "Fallout3.exe",          "Fallout3Launcher.exe"},
                                  {"Fallout 3",                                      "Fallout3.exe",          "Fallout3Launcher.exe"}}},
        {"fallout4",             {{"Fallout 4",                                     "Fallout4.exe",          "Fallout4Launcher.exe"}}},
        {"falloutnewvegas",      {{"Fallout New Vegas Ultimate Edition",             "FalloutNV.exe",         "FalloutNVLauncher.exe"},
                                  {"Fallout New Vegas",                              "FalloutNV.exe",         "FalloutNVLauncher.exe"}}},
        {"oblivion",             {{"The Elder Scrolls IV Oblivion GOTY Deluxe",     "Oblivion.exe",          "OblivionLauncher.exe"},
                                  {"The Elder Scrolls IV Oblivion GOTY",            "Oblivion.exe",          "OblivionLauncher.exe"},
                                  {"The Elder Scrolls IV Oblivion",                 "Oblivion.exe",          "OblivionLauncher.exe"},
                                  {"Oblivion",                                       "Oblivion.exe",          "OblivionLauncher.exe"}}},
        {"cyberpunk2077",        {{"Cyberpunk 2077",                                "bin/x64/Cyberpunk2077.exe", ""}}},
        {"nomanssky",            {{"No Man's Sky",                                  "Binaries/NMS.exe",      ""},
                                  {"No Mans Sky",                                   "Binaries/NMS.exe",      ""}}},
        {"stardewvalley",        {{"Stardew Valley",                               "StardewValley.exe",     ""},
                                  {"Stardew Valley",                               "StardewValley",         ""}}},
        {"falloutlondon",        {{"Fallout London",                               "Fallout4.exe",          "Fallout4Launcher.exe"}}},
        {"arxfatalis",           {{"Arx Fatalis",                                  "ArxFatalis.exe",        ""}}},
        {"gothic1",              {{"Gothic",                                        "Gothic.exe",            ""},
                                  {"Gothic Universe Edition",                       "Gothic.exe",            ""}}},
        {"gothic2",              {{"Gothic II Gold Edition",                        "Gothic2.exe",           ""},
                                  {"Gothic 2 Gold Edition",                         "Gothic2.exe",           ""},
                                  {"Gothic II",                                      "Gothic2.exe",           ""}}},
        {"gothic3",              {{"Gothic 3",                                      "Gothic3.exe",           ""}}},
        {"gothic3fg",            {{"Gothic 3 - Forsaken Gods Enhanced Edition",     "Gothic3FG.exe",         ""}}},
        {"gothic1remake",        {{"Gothic 1 Remake",                              "Gothic_Remake.exe",     ""},
                                  {"Gothic Remake",                                 "Gothic_Remake.exe",     ""}}},
        {"arcania",              {{"ArcaniA - Gothic 4",                           "ArcaniA.exe",           ""},
                                  {"ArcaniA Complete Tale",                         "ArcaniA.exe",           ""},
                                  {"ArcaniA",                                       "ArcaniA.exe",           ""}}},
        {"mortalshell",          {{"Mortal Shell",    "MortalShell/Binaries/Win64/MortalShell-Win64-Shipping.exe", ""}}},
    };
    if (!gogInfo.contains(gameId)) return {};

    const QList<GogGameInfo> &candidates = gogInfo[gameId];
    const QString home = QDir::homePath();

    // Primary: scan Heroic's installed.json - captures custom install paths.
    const QStringList heroicConfigs = {
        home + "/.config/heroic",
        home + "/.var/app/com.heroicgameslauncher.hgl/config/heroic",
    };
    for (const QString &cfg : heroicConfigs) {
        QFile f(cfg + "/gog_store/installed.json");
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        QJsonArray arr;
        if (doc.isArray())
            arr = doc.array();
        else if (doc.isObject() && doc.object().contains("installed"))
            arr = doc.object().value("installed").toArray();

        for (const QJsonValue &v : arr) {
            const QJsonObject obj = v.toObject();
            const QString installPath = obj.value("install_path").toString();
            if (installPath.isEmpty()) continue;
            const QString folderName = QFileInfo(installPath).fileName();

            for (const GogGameInfo &gi : candidates) {
                if (folderName.compare(gi.folder, Qt::CaseInsensitive) != 0 &&
                    folderName.compare(QString(gi.folder).replace(' ', '_'), Qt::CaseInsensitive) != 0)
                    continue;

                const QString wantedExe = (wantLauncher && !gi.launcherExe.isEmpty())
                                          ? gi.launcherExe : gi.exe;

                if (!wantLauncher) {
                    const QString jsonExe = obj.value("executable").toString();
                    if (!jsonExe.isEmpty()) {
                        const QString path = installPath + "/" + jsonExe;
                        if (QFile::exists(path)) return path;
                    }
                }
                const QString path = installPath + "/" + wantedExe;
                if (QFile::exists(path)) return path;
                // Even if exe not found on disk, return the expected path so
                // the caller knows Heroic has it and can launch via heroic://.
                return path;
            }
        }
    }

    // Fallback: directory scan (lgogdownloader, minigalaxy, manual installs).
    const QStringList gogRoots = {
        home + "/Games/Heroic",
        home + "/GOG Games",
        home + "/Games/GOG Games",
        home + "/games/Heroic",
        home + "/games/GOG Games",
        "/mnt/games/Heroic",
        "/mnt/games/GOG Games",
        "/mnt/games/GOG",
    };

    for (const GogGameInfo &gi : candidates) {
        const QString wantedExe = (wantLauncher && !gi.launcherExe.isEmpty())
                                  ? gi.launcherExe : gi.exe;
        const QStringList folderVariants = {
            gi.folder,
            gi.folder.toUpper(),
            gi.folder.toLower(),
            QString(gi.folder).replace(' ', '_'),
            QString(gi.folder).replace(' ', '_').toLower(),
        };
        for (const QString &root : gogRoots) {
            for (const QString &folder : folderVariants) {
                const QString path = root + "/" + folder + "/" + wantedExe;
                if (QFile::exists(path)) return path;
            }
        }
    }
    return {};
}

QString GameProfileRegistry::findLutrisGameExe(const QString &gameId)
{
    static const QHash<QString, QStringList> tokens = {
        {"openmw",               {"openmw"}},
        {"morrowind",            {"morrowind"}},
        {"skyrimspecialedition", {"skyrim", "special"}},
        {"skyrim",               {"skyrim"}},
        {"starfield",            {"starfield"}},
        {"fallout3",             {"fallout", "3"}},
        {"fallout4",             {"fallout", "4"}},
        {"falloutnewvegas",      {"fallout", "new", "vegas"}},
        {"falloutlondon",        {"fallout", "london"}},
        {"oblivion",             {"oblivion"}},
        {"cyberpunk2077",        {"cyberpunk"}},
        {"witcher",              {"witcher"}},
        {"witcher2",             {"witcher", "2"}},
        {"witcher3",             {"witcher", "3"}},
    };
    if (!tokens.contains(gameId)) return {};
    const QStringList &needed = tokens[gameId];

    const QString home = QDir::homePath();
    const QStringList lutrisDirs = {
        home + "/.config/lutris/games",
        home + "/.var/app/net.lutris.Lutris/config/lutris/games",
    };

    for (const QString &dir : lutrisDirs) {
        QDir d(dir);
        if (!d.exists()) continue;
        const QStringList ymls = d.entryList(QStringList() << "*.yml" << "*.yaml", QDir::Files);
        for (const QString &name : ymls) {
            const QString lc = name.toLower();
            bool match = true;
            for (const QString &t : needed) {
                if (!lc.contains(t)) { match = false; break; }
            }
            if (!match) continue;

            QFile f(d.absoluteFilePath(name));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
            // Walk lines; capture exe: under a "game:" section, fallback to any "exe:".
            QString exePath;
            QString fallbackExe;
            bool inGame = false;
            for (const QString &raw : lines) {
                const QString line = raw;
                const QString trimmed = line.trimmed();
                if (trimmed.startsWith('#') || trimmed.isEmpty()) continue;

                // Top-level key (no leading whitespace) ends the previous section.
                if (!line.startsWith(' ') && !line.startsWith('\t')) {
                    inGame = trimmed.startsWith("game:");
                    continue;
                }
                if (trimmed.startsWith("exe:")) {
                    QString val = trimmed.mid(4).trimmed();
                    if ((val.startsWith('"') && val.endsWith('"')) ||
                        (val.startsWith('\'') && val.endsWith('\''))) {
                        val = val.mid(1, val.size() - 2);
                    }
                    if (val.isEmpty()) continue;
                    if (inGame) { exePath = val; break; }
                    if (fallbackExe.isEmpty()) fallbackExe = val;
                }
            }
            if (exePath.isEmpty()) exePath = fallbackExe;
            if (!exePath.isEmpty() && QFile::exists(exePath))
                return exePath;
        }
    }
    return {};
}

QString GameProfileRegistry::findHeroicGogAppId(const QString &installPathHint)
{
    const QString home = QDir::homePath();
    const QStringList heroicConfigs = {
        home + "/.config/heroic",
        home + "/.var/app/com.heroicgameslauncher.hgl/config/heroic",
    };

    for (const QString &cfg : heroicConfigs) {
        QFile f(cfg + "/gog_store/installed.json");
        if (!f.open(QIODevice::ReadOnly)) continue;

        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        QJsonArray arr;
        if (doc.isArray())
            arr = doc.array();
        else if (doc.isObject() && doc.object().contains("installed"))
            arr = doc.object().value("installed").toArray();

        for (const QJsonValue &v : arr) {
            const QJsonObject obj = v.toObject();
            const QString installPath = obj.value("install_path").toString();
            if (installPath.isEmpty()) continue;
            if (installPathHint.startsWith(installPath, Qt::CaseInsensitive))
                return obj.value("appName").toString();
        }
    }
    return {};
}
