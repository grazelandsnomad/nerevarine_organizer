#pragma once

// Typed wrapper around the app's QSettings store. A stringly-typed key
// typo used to read the default and write to a dead key (user reported
// "settings vanished"); now it's a compile error.
//
// No state: every accessor builds a fresh QSettings() so test setup
// (setPath + setDefaultFormat) still works. Defaults live with the
// accessor. game-id/profile-keyed accessors take that as the first arg.
//
// settings_migrations.cpp uses raw key LITERALS (not these constants) so
// it can rewrite an old key even after its accessor is dropped here.

#include <QString>
#include <QStringList>

struct Settings {
    // -- Game registry (`games/list`, `games/current`) ---
    static QStringList gameIds();
    static void        setGameIds(const QStringList &ids);
    static QString     currentGameId();          // default "morrowind"
    static void        setCurrentGameId(const QString &id);

    // -- Per-game profile fields (`games/<id>/...`) ---
    static QString displayName(const QString &gameId);
    static void    setDisplayName(const QString &gameId, const QString &name);
    static QString modsDirFor(const QString &gameId);
    static void    setModsDirFor(const QString &gameId, const QString &dir);
    static QString openmwPath(const QString &gameId);
    static void    setOpenmwPath(const QString &gameId, const QString &path);
    static QString openmwLauncherPath(const QString &gameId);
    static void    setOpenmwLauncherPath(const QString &gameId, const QString &path);
    static QString gameExePath(const QString &gameId);
    static void    setGameExePath(const QString &gameId, const QString &path);
    static QString launcherExePath(const QString &gameId);
    static void    setLauncherExePath(const QString &gameId, const QString &path);
    // Skyrim SE INI dir, per-game so other profiles can't clobber it.
    // Only `skyrimspecialedition` touches it today.
    static QString iniDir(const QString &gameId);
    static void    setIniDir(const QString &gameId, const QString &dir);
    // Bethesda per-game manual overrides for Data/ and Plugins.txt, used
    // when Proton auto-detection fails. Empty until the user sets them.
    static QString dataDir(const QString &gameId);
    static void    setDataDir(const QString &gameId, const QString &dir);
    static QString pluginsTxtPath(const QString &gameId);
    static void    setPluginsTxtPath(const QString &gameId, const QString &path);

    // -- Modlist profiles within a game ---
    // List + active selector
    static QStringList modlistProfileNames(const QString &gameId);
    static void        setModlistProfileNames(const QString &gameId, const QStringList &names);
    static QString     activeModlistProfileName(const QString &gameId);
    static void        setActiveModlistProfileName(const QString &gameId, const QString &name);

    // Per-modlist-profile fields (`games/<id>/profile/<name>/...`)
    static QString modlistProfileModsDir(const QString &gameId, const QString &profile);
    static void    setModlistProfileModsDir(const QString &gameId, const QString &profile, const QString &dir);
    static QString modlistFilename(const QString &gameId, const QString &profile);
    static void    setModlistFilename(const QString &gameId, const QString &profile, const QString &name);
    static QString loadOrderFilename(const QString &gameId, const QString &profile);
    static void    setLoadOrderFilename(const QString &gameId, const QString &profile, const QString &name);

    // Drop the whole `games/<id>/profile/<name>` group on profile
    // remove/rename so save() doesn't leave an orphan.
    static void    removeModlistProfileGroup(const QString &gameId, const QString &profile);

    // -- Legacy/migration keys ---
    // Read once on first launch by GameProfileRegistry::load(), promoted
    // onto the per-game OpenMW paths. Kept here so the literal survives
    // the live writers being gone.
    static QString legacyOpenmwPath();
    static QString legacyOpenmwLauncherPath();
    static QString legacyModsDir();             // pre-multi-game

    // -- Window state ---
    static QByteArray windowGeometry();
    static void       setWindowGeometry(const QByteArray &geom);
    static bool       windowMaximized();
    static void       setWindowMaximized(bool maximized);

    // -- UI ---
    static int     uiZoomPt(int defaultPt);
    static void    setUiZoomPt(int pt);
    static double  uiScaleFactor();             // default 1.0
    static void    setUiScaleFactor(double factor);
    static QString uiLanguage();
    static void    setUiLanguage(const QString &lang);
    static bool    utilityExplainerSeen();
    static void    setUtilityExplainerSeen(bool seen);
    static bool    uiDarkMode();                 // default false (light)
    static void    setUiDarkMode(bool dark);

    // Mod-list column visibility (`ui/col_<col>`).
    static bool    colVisible(const QString &col, bool defaultVisible);
    static void    setColVisible(const QString &col, bool visible);

    // Mod-list column widths (`ui/col_w_<col>[+stateSuffix]`).
    // stateSuffix "" for windowed, "_max" for maximised: one width per
    // visual state.
    static int     colWidth(const QString &col, const QString &stateSuffix, int defaultPx);
    static void    setColWidth(const QString &col, const QString &stateSuffix, int px);

    // -- LOOT banner ---
    static bool    lootBannerDisabled();
    static void    setLootBannerDisabled(bool disabled);

    // -- Download queue ---
    static bool    queueVisible(bool defaultVisible);
    static void    setQueueVisible(bool visible);

    // -- Toolbar customisation (`toolbar/<actionId>`) ---
    static bool    toolbarActionVisible(const QString &actionId, bool defaultVisible);
    static void    setToolbarActionVisible(const QString &actionId, bool visible);

    // -- Separator presets the user has hidden ---
    static QStringList hiddenSeparatorPresets();
    static void        setHiddenSeparatorPresets(const QStringList &keys);

    // -- Launch warnings ---
    // Escape hatch for NixOS / sandboxes / odd module layouts where the
    // kernel reboot heuristic false-fires.
    static bool    skipRebootCheck();
    static void    setSkipRebootCheck(bool skip);

    // -- Patches the user has declined ---
    static QStringList declinedPatches();
    static void        setDeclinedPatches(const QStringList &keys);

    // -- Groundcover-approved set ---
    static QStringList groundcoverApproved();
    static void        setGroundcoverApproved(const QStringList &keys);

    // -- First-run wizard one-shot flag ---
    static bool    wizardCompleted();
    static void    setWizardCompleted(bool completed);

    // -- Desktop-shortcut prompt suppression ---
    static bool    skipDesktopCheck();
    static void    setSkipDesktopCheck(bool skip);

    // -- Archive-tool startup check suppression ---
    // Set when the user dismisses the "missing 7z/unzip/unrar" notice with
    // "don't show again".
    static bool    skipExtractorCheck();
    static void    setSkipExtractorCheck(bool skip);

    // -- Experimental "show all games" toggle ---
    // 0.4 dropdown shows only OpenMW + FNV (the tested install/launch
    // paths). This unhides Skyrim / Oblivion / FO4 / etc. Default false
    // to match the 0.4 "supported games" notes; a toolbar checkbox sets it.
    static bool    showAllGames();
    static void    setShowAllGames(bool show);

    // -- Nexus API key (plain-text fallback only) ---
    // Keychain preferred when HAVE_QTKEYCHAIN; this is both the fallback
    // store and the legacy location the keychain code migrates off.
    static QString nexusApiKey();
    static void    setNexusApiKey(const QString &key);
    static void    removeNexusApiKey();

    // -- Forbidden-mods registry (legacy QSettings shape) ---
    // forbidden_mods.txt is the live store; these exist only for the
    // one-time migration in ForbiddenModsRegistry::load().
    static int     forbiddenCount();
    static QString forbiddenName(int idx);
    static QString forbiddenUrl(int idx);
    static QString forbiddenAnnotation(int idx);
    static bool    forbiddenSeededV1();
    static void    setForbiddenSeededV1(bool seeded);
};
