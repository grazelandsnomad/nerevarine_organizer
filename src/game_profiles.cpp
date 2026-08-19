#include "game_profiles.h"

#include "game_adapter.h"
#include "store_scan.h"

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

#include <utility>   // std::as_const
#include <QString>
#include <QStringList>
#include <Qt>

#include "settings.h"

namespace {

// Filename scheme for new modlist/load-order files. Migrated profiles keep
// legacy `modlist_<gameId>.txt` so upgrades don't rename state files.
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

// Every steamapps/common root, derived from the library roots store_scan
// discovers (libraryfolders.vdf plus the default locations). The vdf parsing
// lives there so the Steam layout is known in exactly one place, and so the
// appmanifest lookup below can reuse it.
QStringList steamCommonRoots()
{
    QStringList out;
    for (const QString &lib : store_scan::steamLibraryRoots()) {
        const QString common = lib + QStringLiteral("/common");
        if (QFileInfo::exists(common) && !out.contains(common)) out << common;
    }
    return out;
}

QString resolveStateFile(const QString &filename)
{
    // Same logic as MainWindow::resolveUserStatePath, duplicated so the
    // registry doesn't pull QtWidgets/MainWindow into its deps. Keep both in
    // sync - they must resolve to the same path.
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
        // First run: create the default OpenMW profile. Promote legacy
        // single-game keys (`mods/dir`, `launch/openmw*`) onto the per-game
        // shape so upgrades keep the user's setup.
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

        // Rename modlist.txt -> modlist_morrowind.txt if present.
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
            // The adapter is the source of truth for the label - the pinned
            // menu already ignored the stored one. Re-reading it here means a
            // renamed game (plain "Skyrim" -> "Skyrim Legendary Edition
            // (Oldrim)", once AE and SE became separate entries) also updates
            // the toolbar button and the dialogs for existing profiles, rather
            // than leaving one stale name behind in half the UI. Display names
            // are not user-editable, so nothing of the user's is overwritten.
            const GameAdapter *adapter = GameAdapterRegistry::find(id);
            gp.displayName = adapter ? adapter->displayName()
                                     : Settings::displayName(id);
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

    // Modlist-profile load + first-run migration. Per game, either:
    // (a) read an existing `games/<id>/profiles` list (v0.4+); or
    // (b) auto-create a "Default" adopting legacy modlist_<gameId>.txt +
    //     loadorder_<gameId>.txt + per-game modsDir.
    // Migrated Default keeps legacy filenames (no rename mid-upgrade);
    // new profiles use the `modlist_<gameId>__<name>.txt` scheme.
    for (GameProfile &gp : m_games) {
        QStringList profileNames = Settings::modlistProfileNames(gp.id);
        profileNames.removeAll(QString());

        if (profileNames.isEmpty()) {
            // First launch under new code.
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
                mp.translationLanguage =
                    Settings::modlistTranslationLanguage(gp.id, pn);
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

        // Copy active profile's modsDir onto gp.modsDir so call sites reading
        // the field see the right dir.
        gp.modsDir = gp.activeModlist().modsDir;
    }

    // Persist migration changes (Default profile entry, etc).
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
            Settings::setModlistTranslationLanguage(gp.id, mp.name,
                                                    mp.translationLanguage);
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
    mp.modsDir           = QString();   // empty: first install prompts
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
    dst.modsDir           = QString();   // clone starts with its own unset modsDir
    dst.modlistFilename   = modlistFilenameFor(gp.id, dst.name);
    dst.loadOrderFilename = loadOrderFilenameFor(gp.id, dst.name);

    // Copy source state files before registering the profile, so a mid-copy
    // failure doesn't leave a profile pointing at nothing. Missing source is
    // fine - the new profile just starts empty.
    auto duplicate = [](const QString &srcFile, const QString &dstFile) {
        if (srcFile.isEmpty() || dstFile.isEmpty()) return;
        const QString srcAbs = resolveStateFile(srcFile);
        const QString dstAbs = resolveStateFile(dstFile);
        if (!QFile::exists(srcAbs)) return;
        QFile::remove(dstAbs);                // drop any stale copy first
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

    // Drop the QSettings group, else a recreated profile of the same name
    // inherits the stale modsDir.
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
        // Best-effort rename on disk. No source file (profile never had
        // installs) is a no-op; just register the new filename for next time.
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

    // Drop the old QSettings group before renaming, else save() leaves an
    // orphan.
    Settings::removeModlistProfileGroup(gp.id, oldName);

    mp.name              = trimmed;
    mp.modlistFilename   = newMlist;
    mp.loadOrderFilename = newLOrd;
    save();
    return true;
}

// Per-game knowledge (Steam app IDs, install layouts, Lutris match tokens)
// lives in src/game_adapters.cpp, one class per game. These four statics are
// thin wrappers over GameAdapterRegistry::find() so existing call sites
// (mostly MainWindow's launch dispatch) keep compiling.

QString GameProfileRegistry::steamAppId(const QString &gameId)
{
    if (const GameAdapter *a = GameAdapterRegistry::find(gameId))
        return a->steamAppId();
    return {};
}

namespace {

// Walk every steamapps/common root looking for the declared layout. Some
// library snapshots drop the trailing " goty" from Bethesda's "Fallout 3
// goty" folder, so chop(5) retries without it.
QString locateInSteam(const QString &folder, const QString &exe,
                      const QString &appId = {})
{
    if (exe.isEmpty()) return {};

    // The app's own manifest first. Steam writes "installdir" there and uses it
    // itself, so it is right even when the folder was renamed or never matched
    // the name we ship. Folder guessing below is the fallback, not the rule.
    if (!appId.isEmpty()) {
        const QString dir = store_scan::steamAppInstallPath(appId);
        if (!dir.isEmpty()) {
            const QString path = dir + "/" + exe;
            if (QFile::exists(path)) return path;
        }
    }

    if (folder.isEmpty()) return {};
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
    return locateInSteam(layout.folder, layout.exe, a->steamAppId());
}

QString GameProfileRegistry::findSteamLauncherExe(const QString &gameId)
{
    const GameAdapter *a = GameAdapterRegistry::find(gameId);
    if (!a) return {};
    const auto layout = a->steamLayout();
    return locateInSteam(layout.folder, layout.launcher, a->steamAppId());
}

QString GameProfileRegistry::findGogGameExe(const QString &gameId, bool wantLauncher)
{
    // Per-game GOG layout candidates live in src/game_adapters.cpp. The
    // detection (Heroic installed.json scan + directory walk fallback) is
    // data-agnostic so it stays here; candidates are just three strings each.
    const GameAdapter *a = GameAdapterRegistry::find(gameId);
    if (!a) return {};
    const QList<GameAdapter::GogLayout> candidates = a->gogLayouts();
    if (candidates.isEmpty()) return {};

    const QString home = QDir::homePath();

    // Heroic first, because it is the one store that can put a game anywhere.
    //
    // An entry is matched on three name signals, not one: the last component of
    // its install_path (all this used to look at), plus the title and
    // folder_name Heroic's own library cache records for that app id. The
    // author's Gothic II sits in a folder called "Gothic 2 Gold" while the
    // library calls it "Gothic 2 Gold Edition" - a candidate we already ship,
    // and the reason detection failed and sent the user to a folder picker.
    const QList<store_scan::HeroicInstall> installs = store_scan::heroicInstalls();
    const QHash<QString, store_scan::StoreTitle> titles = store_scan::heroicTitles();

    auto exeFor = [wantLauncher](const GameAdapter::GogLayout &gi) {
        return (wantLauncher && !gi.launcher.isEmpty()) ? gi.launcher : gi.exe;
    };

    // Pass one: an exact name, however it is spelled in the store. Behaviour
    // for these is unchanged, including returning the expected path when the
    // exe is not on disk - that is what lets the caller launch via heroic://
    // for a game Heroic knows about but has not unpacked here.
    for (const store_scan::HeroicInstall &h : installs) {
        QStringList names{QFileInfo(h.installPath).fileName()};
        const store_scan::StoreTitle t = titles.value(h.appName);
        if (!t.title.isEmpty())      names << t.title;
        if (!t.folderName.isEmpty()) names << t.folderName;

        for (const GameAdapter::GogLayout &gi : candidates) {
            const QString underscored = QString(gi.folder).replace(' ', '_');
            bool hit = false;
            for (const QString &n : std::as_const(names))
                if (n.compare(gi.folder, Qt::CaseInsensitive) == 0
                 || n.compare(underscored, Qt::CaseInsensitive) == 0) { hit = true; break; }
            if (!hit) continue;

            if (!wantLauncher && !h.executable.isEmpty()) {
                const QString path = h.installPath + "/" + h.executable;
                if (QFile::exists(path)) return path;
            }
            return h.installPath + "/" + exeFor(gi);
        }
    }

    // Pass two: the same names compared loosely (roman numerals, edition
    // words, punctuation), which needs evidence to be safe. A loose name test
    // alone would hand back an Enderal Special Edition install for a Skyrim
    // Special Edition query - both ship SkyrimSE.exe - so the file has to be
    // there before the answer counts.
    for (const store_scan::HeroicInstall &h : installs) {
        QStringList names{QFileInfo(h.installPath).fileName()};
        const store_scan::StoreTitle t = titles.value(h.appName);
        if (!t.title.isEmpty())      names << t.title;
        if (!t.folderName.isEmpty()) names << t.folderName;

        for (const GameAdapter::GogLayout &gi : candidates) {
            bool hit = false;
            for (const QString &n : std::as_const(names))
                if (store_scan::titleMatches(gi.folder, n)) { hit = true; break; }
            if (!hit) continue;

            const QString path = h.installPath + "/" + exeFor(gi);
            if (QFile::exists(path)) return path;
        }
    }

    // Fallback: plain directory scan (lgogdownloader, minigalaxy, manual).
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
    // Match tokens live in the per-game classes (src/game_adapters.cpp).
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
            // Normalized, so a "gothic-ii-gold-edition.yml" still answers to
            // the {"gothic", "2"} tokens the adapter declares. The raw
            // filename was compared before, and a roman numeral or an edition
            // word in it was enough to lose the game.
            const QString lc  = name.toLower();
            const QString norm = store_scan::normalizeTitle(name);
            bool match = true;
            for (const QString &t : needed) {
                if (lc.contains(t) || norm.contains(store_scan::normalizeTitle(t)))
                    continue;
                match = false;
                break;
            }
            if (!match) continue;

            QFile f(d.absoluteFilePath(name));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
            // Prefer exe: under a "game:" section, else any "exe:".
            QString exePath;
            QString fallbackExe;
            bool inGame = false;
            for (const QString &raw : lines) {
                const QString line = raw;
                const QString trimmed = line.trimmed();
                if (trimmed.startsWith('#') || trimmed.isEmpty()) continue;

                // Top-level key (no indent) ends the previous section.
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
