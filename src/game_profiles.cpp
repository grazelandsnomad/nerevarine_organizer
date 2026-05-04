#include "game_profiles.h"

#include "game_adapter.h"

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
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <Qt>

#include "settings.h"

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
    QStringList ids = Settings::gameIds();
    ids.removeAll(QString());

    if (ids.isEmpty()) {
        // First-run: create the default OpenMW profile.  Promote legacy
        // single-game keys (`mods/dir`, `launch/openmw*`) onto the new
        // per-game shape so an upgrade keeps the user's setup intact.
        GameProfile morrowind;
        morrowind.id                 = "morrowind";
        morrowind.displayName        = "OpenMW (Morrowind)";
        const QString legacyMods = Settings::legacyModsDir();
        morrowind.modsDir            = legacyMods.isEmpty()
            ? QDir::homePath() + "/Games/nerevarine_mods"
            : legacyMods;
        morrowind.openmwPath         = Settings::legacyOpenmwPath();
        morrowind.openmwLauncherPath = Settings::legacyOpenmwLauncherPath();
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
            gp.displayName = Settings::displayName(id);
            const QString persistedModsDir = Settings::modsDirFor(id);
            gp.modsDir     = persistedModsDir.isEmpty()
                ? QDir::homePath() + "/Games/" + id + "_mods"
                : persistedModsDir;
            gp.openmwPath         = Settings::openmwPath(id);
            gp.openmwLauncherPath = Settings::openmwLauncherPath(id);
            m_games.append(gp);
        }
        if (m_games.isEmpty()) {
            GameProfile morrowind;
            morrowind.id                 = "morrowind";
            morrowind.displayName        = "OpenMW (Morrowind)";
            morrowind.modsDir            = QDir::homePath() + "/Games/nerevarine_mods";
            morrowind.openmwPath         = Settings::legacyOpenmwPath();
            morrowind.openmwLauncherPath = Settings::legacyOpenmwLauncherPath();
            m_games.append(morrowind);
            save();
        }
        const QString currentId = Settings::currentGameId();
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
        QStringList profileNames = Settings::modlistProfileNames(gp.id);
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
                ModlistProfile mp;
                mp.name = pn;
                mp.modsDir           = Settings::modlistProfileModsDir(gp.id, pn);
                QString mlist        = Settings::modlistFilename(gp.id, pn);
                if (mlist.isEmpty())   mlist = modlistFilenameFor(gp.id, pn);
                QString lord         = Settings::loadOrderFilename(gp.id, pn);
                if (lord.isEmpty())    lord  = loadOrderFilenameFor(gp.id, pn);
                mp.modlistFilename   = mlist;
                mp.loadOrderFilename = lord;
                gp.modlistProfiles.append(mp);
            }
            QString activeName = Settings::activeModlistProfileName(gp.id);
            if (activeName.isEmpty())
                activeName = gp.modlistProfiles.first().name;
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
    QStringList ids;
    for (const auto &gp : m_games) {
        ids << gp.id;
        Settings::setDisplayName(gp.id,         gp.displayName);
        Settings::setModsDirFor(gp.id,          gp.modsDir);
        Settings::setOpenmwPath(gp.id,          gp.openmwPath);
        Settings::setOpenmwLauncherPath(gp.id,  gp.openmwLauncherPath);

        // Modlist profiles for this game.
        QStringList profileNames;
        for (const ModlistProfile &mp : gp.modlistProfiles) {
            profileNames << mp.name;
            Settings::setModlistProfileModsDir(gp.id, mp.name, mp.modsDir);
            Settings::setModlistFilename(gp.id, mp.name, mp.modlistFilename);
            Settings::setLoadOrderFilename(gp.id, mp.name, mp.loadOrderFilename);
        }
        Settings::setModlistProfileNames(gp.id, profileNames);
        Settings::setActiveModlistProfileName(gp.id,
            gp.modlistProfiles.isEmpty()
                ? QString()
                : gp.modlistProfiles[gp.activeModlistIdx].name);
    }
    Settings::setGameIds(ids);
    Settings::setCurrentGameId(m_games.isEmpty() ? QString() : m_games[m_currentIdx].id);
}

void GameProfileRegistry::setCurrentIndex(int idx)
{
    if (idx < 0 || idx >= m_games.size() || idx == m_currentIdx) return;
    m_currentIdx = idx;
    Settings::setCurrentGameId(m_games[idx].id);
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
    Settings::removeModlistProfileGroup(gp.id, gp.modlistProfiles[idx].name);

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
    Settings::removeModlistProfileGroup(gp.id, oldName);

    mp.name              = trimmed;
    mp.modlistFilename   = newMlist;
    mp.loadOrderFilename = newLOrd;
    save();
    return true;
}

// Per-game knowledge (Steam app IDs, install layouts, Lutris match
// tokens, …) lives in src/game_adapters.cpp now -- one class per game,
// one source of truth.  These four static methods are thin wrappers
// around GameAdapterRegistry::find() so existing call sites (mostly
// MainWindow's launch dispatch) compile unchanged.

QString GameProfileRegistry::steamAppId(const QString &gameId)
{
    if (const GameAdapter *a = GameAdapterRegistry::find(gameId))
        return a->steamAppId();
    return {};
}

namespace {

// Walk every steamapps/common root the user has and try to land the
// declared layout.  Bethesda's "Fallout 3 goty" folder dropped its
// trailing space-goty in some library snapshots; the .chop(5) fallback
// matches the pre-adapter behaviour byte-for-byte.
QString locateInSteam(const QString &folder, const QString &exe)
{
    if (folder.isEmpty() || exe.isEmpty()) return {};
    const QStringList roots = steamCommonRoots();
    for (const QString &root : roots) {
        QString path = root + "/" + folder + "/" + exe;
        if (QFile::exists(path))
            return path;
        if (folder.endsWith(" goty", Qt::CaseInsensitive)) {
            QString altFolder = folder;
            altFolder.chop(5);
            path = root + "/" + altFolder + "/" + exe;
            if (QFile::exists(path))
                return path;
        }
    }
    return {};
}

} // namespace

QString GameProfileRegistry::findSteamGameExe(const QString &gameId)
{
    const GameAdapter *a = GameAdapterRegistry::find(gameId);
    if (!a) return {};
    const auto layout = a->steamLayout();
    return locateInSteam(layout.folder, layout.exe);
}

QString GameProfileRegistry::findSteamLauncherExe(const QString &gameId)
{
    const GameAdapter *a = GameAdapterRegistry::find(gameId);
    if (!a) return {};
    const auto layout = a->steamLayout();
    return locateInSteam(layout.folder, layout.launcher);
}

QString GameProfileRegistry::findGogGameExe(const QString &gameId, bool wantLauncher)
{
    // Per-game GOG layout candidates moved to src/game_adapters.cpp.
    // Detection (Heroic-installed.json scan + directory walk fallback)
    // stays here because it's data-agnostic and pulls in no per-game
    // knowledge -- the candidates are just three strings each.
    const GameAdapter *a = GameAdapterRegistry::find(gameId);
    if (!a) return {};
    const QList<GameAdapter::GogLayout> candidates = a->gogLayouts();
    if (candidates.isEmpty()) return {};

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

            for (const GameAdapter::GogLayout &gi : candidates) {
                if (folderName.compare(gi.folder, Qt::CaseInsensitive) != 0 &&
                    folderName.compare(QString(gi.folder).replace(' ', '_'), Qt::CaseInsensitive) != 0)
                    continue;

                const QString wantedExe = (wantLauncher && !gi.launcher.isEmpty())
                                          ? gi.launcher : gi.exe;

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

    for (const GameAdapter::GogLayout &gi : candidates) {
        const QString wantedExe = (wantLauncher && !gi.launcher.isEmpty())
                                  ? gi.launcher : gi.exe;
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
    // Match tokens moved to src/game_adapters.cpp's per-game classes.
    const GameAdapter *a = GameAdapterRegistry::find(gameId);
    if (!a) return {};
    const QStringList needed = a->lutrisTokens();
    if (needed.isEmpty()) return {};

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
