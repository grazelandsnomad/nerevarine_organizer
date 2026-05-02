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
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <Qt>

// -- ModlistProfile / GameProfile helpers ---

namespace {

// Bare-filename scheme for new modlist/load-order files.  Migration keeps
// the legacy `modlist_<gameId>.txt` instead so existing users don't get
// their state files moved on first run under the new code.
QString modlistFilenameFor(const QString &gameId, const QString &profileName)
{
    QString sanitized = profileName;
    static const QRegularExpression invalid(QStringLiteral("[^A-Za-z0-9_-]+"));
    sanitized.replace(invalid, QStringLiteral("_"));
    if (sanitized.isEmpty()) sanitized = QStringLiteral("profile");
    return QStringLiteral("modlist_%1__%2.txt").arg(gameId, sanitized);
}

QString loadOrderFilenameFor(const QString &gameId, const QString &profileName)
{
    QString sanitized = profileName;
    static const QRegularExpression invalid(QStringLiteral("[^A-Za-z0-9_-]+"));
    sanitized.replace(invalid, QStringLiteral("_"));
    if (sanitized.isEmpty()) sanitized = QStringLiteral("profile");
    return QStringLiteral("loadorder_%1__%2.txt").arg(gameId, sanitized);
}

// Discover every Steam library root configured for this user, not just the
// hardcoded handful.  Steam keeps the authoritative list in
// `steamapps/libraryfolders.vdf` — both the modern shape
//   "0" { "path" "/mnt/nvme_2TB/SteamLibrary" ... }
// and the legacy flat shape
//   "1"  "/mnt/nvme_2TB/SteamLibrary"
// occur in the wild, so the parser accepts both via two regexes.
//
// Without this, games installed on a non-default Steam library (custom
// SSD mount, second drive, etc.) silently fail detection and the user has
// to point at the .exe manually.
QStringList steamCommonRoots()
{
    QStringList roots;
    auto pushIfNew = [&](const QString &p) {
        if (!p.isEmpty() && !roots.contains(p) && QFileInfo::exists(p))
            roots.append(p);
    };

    const QString home = QDir::homePath();

    // Hardcoded fallbacks - kept so detection works even when no vdf is
    // findable (e.g. a flatpak Steam that points elsewhere).
    pushIfNew(home + "/.steam/steam/steamapps/common");
    pushIfNew(home + "/.local/share/Steam/steamapps/common");
    pushIfNew("/mnt/games/Steam/steamapps/common");

    // libraryfolders.vdf candidates - parse each one we can read.
    const QStringList vdfCandidates = {
        home + "/.steam/steam/steamapps/libraryfolders.vdf",
        home + "/.local/share/Steam/steamapps/libraryfolders.vdf",
        home + "/.steam/root/steamapps/libraryfolders.vdf",
        home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/libraryfolders.vdf",
    };

    static const QRegularExpression rxPath(
        QStringLiteral("\"path\"\\s+\"([^\"]+)\""),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rxLegacy(
        QStringLiteral("^\\s*\"\\d+\"\\s+\"(/[^\"]+)\"\\s*$"),
        QRegularExpression::MultilineOption);

    for (const QString &vdf : vdfCandidates) {
        QFile f(vdf);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString contents = QString::fromUtf8(f.readAll());
        f.close();

        auto it = rxPath.globalMatch(contents);
        while (it.hasNext()) {
            const QString libRoot = it.next().captured(1);
            pushIfNew(libRoot + "/steamapps/common");
        }
        auto it2 = rxLegacy.globalMatch(contents);
        while (it2.hasNext()) {
            const QString libRoot = it2.next().captured(1);
            pushIfNew(libRoot + "/steamapps/common");
        }
    }

    return roots;
}

QString resolveStateFile(const QString &filename)
{
    // Mirror of MainWindow::resolveUserStatePath - duplicated here so the
    // game-profile registry doesn't pull QtWidgets / MainWindow into its
    // dependency graph.  Both implementations land on the same path.
    const bool inAppImage = !qEnvironmentVariableIsEmpty("APPIMAGE");
    const QString data = QStandardPaths::writableLocation(
                             QStandardPaths::AppDataLocation) + "/" + filename;
    if (inAppImage) {
        QDir().mkpath(QFileInfo(data).absolutePath());
        return QFileInfo(data).absoluteFilePath();
    }
    const QString next = QCoreApplication::applicationDirPath() + "/" + filename;
    QFileInfo binDir(QCoreApplication::applicationDirPath());
    for (const QString &p : {next, data})
        if (QFile::exists(p)) return QFileInfo(p).absoluteFilePath();
    if (binDir.isWritable())
        return QFileInfo(next).absoluteFilePath();
    QDir().mkpath(QFileInfo(data).absolutePath());
    return QFileInfo(data).absoluteFilePath();
}

} // namespace

ModlistProfile& GameProfile::activeModlist()
{
    static ModlistProfile empty;
    if (activeModlistIdx < 0 || activeModlistIdx >= modlistProfiles.size())
        return empty;
    return modlistProfiles[activeModlistIdx];
}

const ModlistProfile& GameProfile::activeModlist() const
{
    static const ModlistProfile empty;
    if (activeModlistIdx < 0 || activeModlistIdx >= modlistProfiles.size())
        return empty;
    return modlistProfiles[activeModlistIdx];
}

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

    // Modlist-profile load + first-run migration.  For every game we either
    // (a) read an existing `games/<id>/profiles` list - the v0.4+ shape; or
    // (b) auto-create a "Default" profile that adopts the legacy
    //     modlist_<gameId>.txt + loadorder_<gameId>.txt + per-game modsDir.
    // The legacy filenames are KEPT for the migrated Default so users don't
    // see file renames mid-upgrade; new profiles use the canonical
    // `modlist_<gameId>__<name>.txt` scheme.
    for (GameProfile &gp : m_games) {
        const QString base = "games/" + gp.id;
        QStringList profileNames = s.value(base + "/profiles").toStringList();
        profileNames.removeAll(QString());

        if (profileNames.isEmpty()) {
            // Migration path - first launch under new code.
            ModlistProfile def;
            def.name              = QStringLiteral("Default");
            def.modsDir           = gp.modsDir;
            def.modlistFilename   = QStringLiteral("modlist_")   + gp.id + QStringLiteral(".txt");
            def.loadOrderFilename = QStringLiteral("loadorder_") + gp.id + QStringLiteral(".txt");
            gp.modlistProfiles.append(def);
            gp.activeModlistIdx = 0;
        } else {
            for (const QString &pn : profileNames) {
                const QString pkey = base + "/profile/" + pn;
                ModlistProfile mp;
                mp.name              = pn;
                mp.modsDir           = s.value(pkey + "/mods_dir").toString();
                mp.modlistFilename   = s.value(pkey + "/modlist_filename",
                                          modlistFilenameFor(gp.id, pn)).toString();
                mp.loadOrderFilename = s.value(pkey + "/loadorder_filename",
                                          loadOrderFilenameFor(gp.id, pn)).toString();
                gp.modlistProfiles.append(mp);
            }
            const QString activeName = s.value(base + "/active_profile",
                                               gp.modlistProfiles.first().name).toString();
            gp.activeModlistIdx = 0;
            for (int i = 0; i < gp.modlistProfiles.size(); ++i) {
                if (gp.modlistProfiles[i].name == activeName) {
                    gp.activeModlistIdx = i;
                    break;
                }
            }
        }

        // Mirror the active profile's modsDir onto gp.modsDir so existing
        // call sites that read the field keep observing the right dir.
        gp.modsDir = gp.activeModlist().modsDir;
    }

    // Persist any migration changes (Default profile entry, etc).
    save();

    if (!m_games[m_currentIdx].modsDir.isEmpty())
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

        // Modlist profiles for this game.
        QStringList profileNames;
        for (const ModlistProfile &mp : gp.modlistProfiles) {
            profileNames << mp.name;
            const QString pkey = "games/" + gp.id + "/profile/" + mp.name;
            s.setValue(pkey + "/mods_dir",           mp.modsDir);
            s.setValue(pkey + "/modlist_filename",   mp.modlistFilename);
            s.setValue(pkey + "/loadorder_filename", mp.loadOrderFilename);
        }
        s.setValue("games/" + gp.id + "/profiles",       profileNames);
        s.setValue("games/" + gp.id + "/active_profile",
                   gp.modlistProfiles.isEmpty()
                       ? QString()
                       : gp.modlistProfiles[gp.activeModlistIdx].name);
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

void GameProfileRegistry::setActiveModlistIndex(int idx)
{
    if (m_games.isEmpty()) return;
    GameProfile &gp = m_games[m_currentIdx];
    if (idx < 0 || idx >= gp.modlistProfiles.size() || idx == gp.activeModlistIdx) return;
    gp.activeModlistIdx = idx;
    gp.modsDir          = gp.activeModlist().modsDir;
    save();
}

void GameProfileRegistry::setActiveModsDir(const QString &dir)
{
    if (m_games.isEmpty()) return;
    GameProfile &gp = m_games[m_currentIdx];
    gp.modsDir = dir;
    if (gp.activeModlistIdx >= 0 && gp.activeModlistIdx < gp.modlistProfiles.size())
        gp.modlistProfiles[gp.activeModlistIdx].modsDir = dir;
    save();
}

int GameProfileRegistry::addModlistProfile(const QString &name)
{
    if (name.trimmed().isEmpty() || m_games.isEmpty()) return -1;
    GameProfile &gp = m_games[m_currentIdx];
    for (const ModlistProfile &mp : gp.modlistProfiles) {
        if (mp.name.compare(name, Qt::CaseInsensitive) == 0) return -1;
    }

    ModlistProfile mp;
    mp.name              = name.trimmed();
    mp.modsDir           = QString();   // empty → first install will prompt
    mp.modlistFilename   = modlistFilenameFor(gp.id, mp.name);
    mp.loadOrderFilename = loadOrderFilenameFor(gp.id, mp.name);
    gp.modlistProfiles.append(mp);
    save();
    return gp.modlistProfiles.size() - 1;
}

int GameProfileRegistry::cloneModlistProfile(int srcIdx, const QString &newName)
{
    if (m_games.isEmpty()) return -1;
    GameProfile &gp = m_games[m_currentIdx];
    if (srcIdx < 0 || srcIdx >= gp.modlistProfiles.size())  return -1;
    if (newName.trimmed().isEmpty())                         return -1;
    for (const ModlistProfile &mp : gp.modlistProfiles) {
        if (mp.name.compare(newName, Qt::CaseInsensitive) == 0) return -1;
    }

    const ModlistProfile &src = gp.modlistProfiles[srcIdx];

    ModlistProfile dst;
    dst.name              = newName.trimmed();
    dst.modsDir           = QString();   // clone-empty: NEW modsDir, unset
    dst.modlistFilename   = modlistFilenameFor(gp.id, dst.name);
    dst.loadOrderFilename = loadOrderFilenameFor(gp.id, dst.name);

    // Duplicate the source's state files on disk before we register the
    // profile so a failure mid-copy doesn't leave a profile pointing at
    // nothing.  Source absent is fine - the new profile just starts empty.
    auto duplicate = [](const QString &srcFile, const QString &dstFile) {
        if (srcFile.isEmpty() || dstFile.isEmpty()) return;
        const QString srcAbs = resolveStateFile(srcFile);
        const QString dstAbs = resolveStateFile(dstFile);
        if (!QFile::exists(srcAbs)) return;
        QFile::remove(dstAbs);                // overwrite a stale copy
        QFile::copy(srcAbs, dstAbs);
    };
    duplicate(src.modlistFilename,   dst.modlistFilename);
    duplicate(src.loadOrderFilename, dst.loadOrderFilename);

    gp.modlistProfiles.append(dst);
    save();
    return gp.modlistProfiles.size() - 1;
}

bool GameProfileRegistry::removeModlistProfile(int idx, bool deleteStateFiles)
{
    if (m_games.isEmpty()) return false;
    GameProfile &gp = m_games[m_currentIdx];
    if (idx < 0 || idx >= gp.modlistProfiles.size()) return false;
    if (gp.modlistProfiles.size() <= 1)              return false;

    if (deleteStateFiles) {
        const ModlistProfile &mp = gp.modlistProfiles[idx];
        if (!mp.modlistFilename.isEmpty())
            QFile::remove(resolveStateFile(mp.modlistFilename));
        if (!mp.loadOrderFilename.isEmpty())
            QFile::remove(resolveStateFile(mp.loadOrderFilename));
    }

    // Drop the QSettings group so a recreated profile of the same name
    // doesn't accidentally inherit the old modsDir.
    QSettings().remove("games/" + gp.id + "/profile/" +
                       gp.modlistProfiles[idx].name);

    gp.modlistProfiles.removeAt(idx);
    if (gp.activeModlistIdx >= gp.modlistProfiles.size())
        gp.activeModlistIdx = gp.modlistProfiles.size() - 1;
    if (gp.activeModlistIdx < 0)
        gp.activeModlistIdx = 0;
    gp.modsDir = gp.activeModlist().modsDir;
    save();
    return true;
}

bool GameProfileRegistry::renameModlistProfile(int idx, const QString &newName)
{
    if (m_games.isEmpty()) return false;
    GameProfile &gp = m_games[m_currentIdx];
    if (idx < 0 || idx >= gp.modlistProfiles.size()) return false;
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) return false;
    for (int i = 0; i < gp.modlistProfiles.size(); ++i) {
        if (i != idx &&
            gp.modlistProfiles[i].name.compare(trimmed, Qt::CaseInsensitive) == 0)
            return false;
    }

    ModlistProfile &mp = gp.modlistProfiles[idx];
    const QString oldName     = mp.name;
    const QString legacyMlist = QStringLiteral("modlist_")   + gp.id + QStringLiteral(".txt");
    const QString legacyLOrd  = QStringLiteral("loadorder_") + gp.id + QStringLiteral(".txt");
    const bool    isLegacy    = (mp.modlistFilename == legacyMlist ||
                                 mp.loadOrderFilename == legacyLOrd);

    QString newMlist = mp.modlistFilename;
    QString newLOrd  = mp.loadOrderFilename;
    if (!isLegacy) {
        newMlist = modlistFilenameFor(gp.id, trimmed);
        newLOrd  = loadOrderFilenameFor(gp.id, trimmed);
        // Best-effort rename on disk.  If the source file isn't there
        // (e.g. profile never had any installs), the rename is a no-op
        // and the new filename is just registered for next time.
        auto renameFile = [](const QString &fromFile, const QString &toFile) {
            if (fromFile == toFile || fromFile.isEmpty() || toFile.isEmpty()) return;
            const QString fromAbs = resolveStateFile(fromFile);
            const QString toAbs   = resolveStateFile(toFile);
            if (QFile::exists(fromAbs)) {
                QFile::remove(toAbs);
                QFile::rename(fromAbs, toAbs);
            }
        };
        renameFile(mp.modlistFilename,   newMlist);
        renameFile(mp.loadOrderFilename, newLOrd);
    }

    // Drop the old QSettings group before assigning the new name so the
    // next save() doesn't leave an orphan entry behind.
    QSettings().remove("games/" + gp.id + "/profile/" + oldName);

    mp.name              = trimmed;
    mp.modlistFilename   = newMlist;
    mp.loadOrderFilename = newLOrd;
    save();
    return true;
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
    const QStringList roots = steamCommonRoots();

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
    const QStringList roots = steamCommonRoots();

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
