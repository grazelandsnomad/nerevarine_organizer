#pragma once

// Typed wrapper around the app's QSettings store.
//
// Why bother:
//   · A typo in `QSettings().value("games/" + id + "/mods_dir")` was a
//     silent runtime miss - the read returned the default, the write
//     went to a new key that no reader ever looked up, and the bug was
//     only spotted by a user reporting "my settings vanished."  Now
//     such a typo is a compile error.
//   · settings_migrations.cpp exists specifically because key naming
//     drifted across refactors.  Centralising the key strings here gives
//     migrations a single source of truth to mirror.
//   · Adding a new per-game-profile field used to require editing four
//     QSettings call sites consistently.  Now it's a single accessor
//     pair.
//
// Conventions:
//   · Namespaced under `struct Settings { static ... };`.  No state -
//     every accessor constructs a fresh QSettings(), same as the call
//     sites this replaces.  Keeps test setup (QSettings::setPath +
//     setDefaultFormat) working unchanged.
//   · Functions whose key is parameterised by game id or profile name
//     take that as the first argument; the accessor formats the key.
//   · Defaults live with the accessor, never at the call site.
//   · Migrations in settings_migrations.cpp reference key string
//     LITERALS (not constants from this header), so a migration can
//     rewrite an old key even after the accessor for it has been
//     dropped here.

#include <QString>
#include <QStringList>

struct Settings {
    // -- Game registry (`games/list`, `games/current`) ---
    static QStringList gameIds();
    static void        setGameIds(const QStringList &ids);
    static QString     currentGameId();          // defaults to "morrowind"
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
    // Skyrim SE INI directory (per-game so non-Skyrim profiles don't
    // clobber it; today only `skyrimspecialedition` reads/writes it).
    static QString iniDir(const QString &gameId);
    static void    setIniDir(const QString &gameId, const QString &dir);

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

    // Drop the entire `games/<id>/profile/<name>` group.  Used on profile
    // remove/rename so the next save() doesn't leave an orphan entry.
    static void    removeModlistProfileGroup(const QString &gameId, const QString &profile);

    // -- Legacy/migration keys ---
    // Read once on first launch by GameProfileRegistry::load() and
    // promoted onto the per-game OpenMW paths.  Kept here so the literal
    // lives in one place even after the live writers are gone.
    static QString legacyOpenmwPath();
    static QString legacyOpenmwLauncherPath();
    static QString legacyModsDir();             // pre-multi-game default

    // -- Window state ---
    static QByteArray windowGeometry();
    static void       setWindowGeometry(const QByteArray &geom);
    static bool       windowMaximized();
    static void       setWindowMaximized(bool maximized);

    // -- UI ---
    static int     uiZoomPt(int defaultPt);
    static void    setUiZoomPt(int pt);
    static double  uiScaleFactor();             // defaults to 1.0
    static void    setUiScaleFactor(double factor);
    static QString uiLanguage();
    static void    setUiLanguage(const QString &lang);
    static bool    utilityExplainerSeen();
    static void    setUtilityExplainerSeen(bool seen);

    // Mod-list column visibility (`ui/col_<col>`).
    static bool    colVisible(const QString &col, bool defaultVisible);
    static void    setColVisible(const QString &col, bool visible);

    // Mod-list column widths (`ui/col_w_<col>[+stateSuffix]`).
    // `stateSuffix` is "" for windowed mode, "_max" for maximised, so
    // we get one persisted width per visual state.
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
    // Persistent escape hatch for users on NixOS / sandboxes / unusual
    // module layouts where the kernel reboot heuristic produces a false
    // positive.
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

    // -- Nexus API key (plain-text fallback only) ---
    // The app prefers the system keychain when HAVE_QTKEYCHAIN is on;
    // these accessors are both the fallback storage AND the legacy
    // location the keychain code migrates away from.
    static QString nexusApiKey();
    static void    setNexusApiKey(const QString &key);
    static void    removeNexusApiKey();

    // -- Forbidden-mods registry (legacy QSettings shape) ---
    // The portable forbidden_mods.txt is the live storage; these
    // accessors exist solely for the one-time migration in
    // ForbiddenModsRegistry::load().
    static int     forbiddenCount();
    static QString forbiddenName(int idx);
    static QString forbiddenUrl(int idx);
    static QString forbiddenAnnotation(int idx);
    static bool    forbiddenSeededV1();
    static void    setForbiddenSeededV1(bool seeded);
};
