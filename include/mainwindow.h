#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QColor>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <QHash>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QUuid>

#include <expected>
#include <functional>

#include "downloadqueue.h"
#include "installcontroller.h"   // for InstallController::VerifyFailKind in slot
#include "modroles.h"
#include "nexusclient.h"
#include "game_profiles.h"

class QAction;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QNetworkReply;
class ModlistModel;
class QPushButton;
class QTimer;
class QToolButton;
class QWidget;
class ModListDelegate;
class LoadOrderController;
class NexusClient;
class NexusController;
class UndoStack;
class ZoomController;
class FilterBar;
class NotifyBanner;
class ColumnHeader;
class ForbiddenModsRegistry;
class ToolbarCustomization;
class ScanCoordinator;
class BackupManager;
class BulkInstallQueue;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() = default;

public slots:
    void handleNxmUrl(const QString &url);
    // Entry point for external drag-drop handlers (ModListWidget dispatches
    // archive drops here via a queued invocation).  Public so the nested
    // QListWidget subclass in mainwindow.cpp can reach it without a friend
    // declaration leaking MainWindow's private internals.
    void installLocalArchive(const QString &archivePath);
    void handleDroppedImportFile(const QString &path);

private slots:
    void onAddSeparator();
    void onAddMod();
    void onRemoveSelected();
    void onMoveUp();
    void onMoveDown();
    void onCheckUpdates();
    void onCheckUpdatesFinished(int foundCount);
    void onTitleFetched(QListWidgetItem *item, const QString &name);
    void onExpectedChecksumFetched(QListWidgetItem *item,
                                   const QString &md5, qint64 sizeBytes);
    void onFileListFetched(QListWidgetItem *item,
                           const QString &game, int modId,
                           const QList<NexusClient::FileEntry> &files);
    void onFileListFetchFailed(QListWidgetItem *item, const QString &reason, int httpStatus);
    void onDependenciesScanned(QListWidgetItem *item,
                               const QString &game, int modId,
                               const QString &title,
                               const QStringList &presentDeps,
                               const QList<int> &missing);
    void onDependencyScanFailed(QListWidgetItem *item,
                                const QString &game, int modId);
    void onVerificationStarted(const QString &archivePath);
    void onArchiveVerified(const QString &archivePath, const QUuid &installToken);
    void onArchiveVerificationFailed(const QString &archivePath,
                                     const QUuid &installToken,
                                     InstallController::VerifyFailKind kind,
                                     const QString &actual,
                                     const QString &expected);
    void onExtractionSucceeded(const QString &archivePath,
                               const QString &extractDir,
                               const QString &modPathIn,
                               const QUuid &installToken);
    void onExtractionFailed(const QString &archivePath,
                            const QString &extractDir,
                            const QUuid &installToken,
                            InstallController::ExtractFailKind kind,
                            const QString &detail);
    void onReviewUpdates();
    void onContextMenu(const QPoint &pos);
    void onItemDoubleClicked(QListWidgetItem *item);
    void onSetApiKey();
    void onSetModsDir();
    void onSetLanguage(const QString &language);
    void onLaunchOpenMW();
    void onLaunchOpenMWLauncher();
    // Aggregates the four warning signals (missing masters, missing
    // deps, empty installs, forbidden enabled) and, if any are present,
    // shows a summary dialog.  Returns false if the user chose Cancel.
    bool confirmLaunchIfWarnings();
    // Detects a pending kernel / system reboot and, if one is pending,
    // shows a blocking critical dialog and returns true so the caller
    // aborts the launch.  Rationale: on Arch-family distros pacman wipes
    // /usr/lib/modules/<running-kernel> on kernel upgrade, so new module
    // loads (DRM, input) fail - OpenMW then errors out inscrutably.
    bool refuseLaunchIfRebootPending();
    void onLaunchGame();            // generic launch for non-Morrowind games
    void onLaunchSteamLauncher();   // launches the game's official launcher (e.g. SkyrimSELauncher.exe)
    void onTuneSkyrimIni();         // BethINI-style INI tweaks for Skyrim SE
    void onAnimTick();
    void onSortByDate();
    void onSortBySize();
    void onSortSeparators();
    void onInspectOpenMWSetup();
    void onInspectConflicts();
    void onTriageOpenMWLog();
    void onModlistSummary();
    void onMoveModsDir();
    // Bundle the user's modlist + load-order files, openmw.cfg, the tail
    // of OpenMW.log, and a system-summary snapshot into a single
    // `nerev_diagnostics_<timestamp>.zip` for bug reports.  Asks the
    // user where to save the archive and reveals it in the file
    // manager when done.  No network, no PII beyond what the user
    // already chose to put in their modlist.
    void onCreateDiagnosticBundle();

    // Async scan entry points - cheap to call; they just (re)start a short
    // debounce timer.  The real work runs on a worker thread via
    // QtConcurrent::run, and results are posted back to the UI thread.
    void scanMissingMasters();   // schedule a missing-master rescan
    void scanMissingDependencies(); // in-memory DependsOn → warning-icon sweep
    void switchToGame(int idx);
    // Switch to a different modlist profile within the current game.
    // Saves the current modlist + load order to the OLD profile's files,
    // flips the registry's active index, mirrors the new profile's modsDir
    // onto m_modsDir, and reloads the modlist + load order from the NEW
    // profile's files.  Re-syncs openmw.cfg so the engine sees the right
    // mod set on next launch.
    void switchToModlistProfile(int idx);
    // Pops the dialog that lets the user create / clone / rename / delete
    // modlist profiles for the current game.  Wired to the profile picker
    // button's "Manage profiles…" menu entry.
    void onManageModlistProfiles();
    // Quick "create a new empty profile and switch to it" path used by both
    // the profile menu's "New profile…" entry and the Wabbajack-into-new-
    // profile flow.  Returns the new profile's index, or -1 if the user
    // cancelled / picked an invalid name.  Does NOT switch.
    int  createNewModlistProfile(const QString &suggestedName = QString());
    // Make sure the active modlist profile has a modsDir before the caller
    // tries to install something into it.  Fires the "where do you want to
    // store this profile's mods?" prompt when modsDir is empty.  Returns
    // true if there's a mods dir to use after the call (either pre-existing
    // or just picked); false if the user cancelled.  Safe to call repeatedly
    // - no-op when m_modsDir is already set.
    bool ensureModsDirForActiveProfile();
    // Profile/game switches used to destroy in-flight install placeholders
    // (the QListWidget items with status==2) along with the rest of the row
    // set, leaving the InstallController's signal handlers with a dangling
    // pointer and the user with an "aborted" mod.  These two helpers move
    // the items into a per-profile parking lot before the clear, then
    // restore them after loadModList() rebuilds the new profile's rows -
    // so an FNV→Morrowind→FNV round-trip during an extraction now finds
    // the install still running and the row still alive when the user
    // comes back.  Token = "<gameId>__<profileName>".
    QString currentProfileKey() const;
    void    strandInflightInstalls();
    void    restoreStrandedInstalls();
    // Consolidate any of THIS profile's mods that physically live outside
    // its modsDir into it.  The driver case is a cloned profile: after
    // cloneModlistProfile, the modlist points at the source profile's
    // mods on disk; this tool copies/moves them into the active profile's
    // modsDir so the two profiles are fully decoupled.  No-op when every
    // mod is already inside m_modsDir.  Refuses to run while downloads
    // are active or m_modsDir is unset.
    void onConsolidateModsIntoActiveProfile();
    void onAddGame();
    // Detects an external (non-OpenMW) game in Steam/Heroic/Lutris,
    // prompts for the exe if not found, then creates a profile and
    // switches to it. Used by the pinned-game dropdown entries.
    void addAndDetectGame(const QString &gameId, const QString &displayName);
    void onCurrentModChanged(QListWidgetItem *current, QListWidgetItem *previous);

    bool eventFilter(QObject *obj, QEvent *event) override;

    // Drag-and-drop installable archives (.7z / .zip / .rar / .fomod) onto
    // any non-list area of the window.  The list widget area is covered by
    // ModListWidget::dropEvent which forwards here for external file drops.
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent  *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupMenuBar();
    void setupToolbar();
    void setupCentralWidget();

    void saveModList();
    // Persist a single placeholder row's current state into a SPECIFIC
    // profile's modlist file -- used when an InstallController signal
    // lands while the user is on a different profile, so the install
    // belongs to a profile other than the active one.  Reads + parses
    // the target file, replaces (or appends) the entry whose installToken
    // matches `placeholder`, and writes back.  Does NOT touch m_modList,
    // load order, openmw.cfg, or any of the active-profile side effects
    // saveModList() runs.  No-op when the token is null or the path
    // can't be resolved.
    void saveModListFor(const QString &profileKey, QListWidgetItem *placeholder);
    // Resolve the absolute modlist file path for a given profile key
    // (the same `<gameId>__<profileName>` form currentProfileKey()
    // emits).  Returns empty when the key doesn't match any registered
    // profile.
    QString modlistPathFor(const QString &profileKey) const;
    // Look up an in-flight placeholder row by its InstallToken.  Searches
    // m_modList first, then m_strandedInstalls (parked across profile
    // switches).  When found in a stranded bucket, `outProfileKey` (if
    // non-null) gets the bucket's key so the caller can route the save
    // through saveModListFor().  Returns nullptr when no row matches the
    // token (placeholder removed, app restarted, or token mismatch).
    QListWidgetItem *findPlaceholderByToken(const QUuid &installToken,
                                            QString *outProfileKey = nullptr) const;
    void loadModList(const QString &path = QString(),
                     const QString &remapFrom = QString(),
                     const QString &remapTo   = QString());
    void syncOpenMWConfig();
    void exportModList();
    void onImportModList();
    void onImportMO2ModList();
    void onImportMO2Profile();
    void onImportWabbajack();
    // Internal helpers called by menu slots and the drop handler
    void doImportMO2ModList(const QString &path);
    void doImportWabbajack(const QString &path);
    void finishWabbajackImport(const QJsonObject &root);
    void doImportNerevarineModList(const QString &path);
    static QString detectModsBaseFromFile(const QString &path);
    bool confirmReplaceModList();
    void onViewChangelog(QListWidgetItem *item);
    void onNewModList();
    // savePath is invoked whenever the resolved path changes (auto-detected
    // via PATH or picked manually via the dialog) so the call site owns the
    // persisted-key choice via the typed Settings:: accessor of its choice.
    void launchProgram(QString &storedPath,
                       std::function<void(const QString&)> savePath,
                       const QString &execName, const QString &locateTitle,
                       bool monitored = false);
    void checkNxmHandlerRegistration();
    void checkDesktopShortcut();
    void collapseSection(QListWidgetItem *sep, bool collapse);
    void onEditSeparator(QListWidgetItem *item);

    // installLocalArchive is promoted to public (see the public: section
    // above) because the nested ModListWidget drop handler in mainwindow.cpp
    // dispatches archive drops to it via a queued invocation, and a private
    // method would require either a friend declaration (awkward for a class
    // defined inline in the .cpp) or a dispatcher signal.
    void sendSelectedToSeparator(QListWidgetItem *sep);
    void openSendToDialog();
    // Bulk-move the current selection to either row 0 or past the last row,
    // preserving relative order.  Wired to Send to → Top / Bottom menu.
    void sendSelectedToEdge(bool toBeginning);
    void updateModCount();
    void addModFromPath(const QString &dirPath, QListWidgetItem *placeholder = nullptr);
    void purgeDuplicatePlaceholders(QListWidgetItem *installed);

    // Kick off extraction of the downloaded archive.  Returns immediately
    // - the actual extract runs in InstallController::extractArchive via
    // QProcess and reports back through extractionSucceeded/Failed signals.
    //
    // The return carries synchronous preconditions only (valid placeholder,
    // archive exists on disk, mods dir configured).  On unexpected(reason)
    // the controller is never called; callers should not try to resume
    // the install from that branch.  The async outcome is still delivered
    // via the signal path regardless of what this returns when preconditions
    // pass.
    //   "null-placeholder" - placeholder pointer was nullptr
    //   "archive-missing"  - archivePath does not exist or is empty
    //   "mods-dir-unset"   - m_modsDir was empty (first-run misconfigured)
    [[nodiscard]] std::expected<void, QString>
    extractAndAdd(const QString &archivePath, QListWidgetItem *placeholder);

    // Download verification - compares the finished archive's size & md5
    // against what Nexus declared before extracting.  Cheap size check is
    // synchronous; the md5 hash runs on a worker thread via QtConcurrent.
    // If either check fails, the archive is deleted and the placeholder is
    // reset to not-installed.  When no expected values were recorded
    // (local-archive drops, older installs), the verify is skipped and
    // extractAndAdd runs as-is.
    //
    // Like extractAndAdd above: the return value carries only synchronous
    // preconditions.  Reasons:
    //   "null-placeholder" - placeholder pointer was nullptr
    //   "archive-missing"  - archivePath does not exist or is empty
    [[nodiscard]] std::expected<void, QString>
    verifyAndExtract(const QString &archivePath, QListWidgetItem *placeholder);

    // Download queue - thin wrapper; real logic is in DownloadQueue.
    void setupDownloadQueue();
    void onInstallFromNexus(QListWidgetItem *item);
    // Fetches the Nexus mod page `name` field and caches it on the item as
    // ModRole::NexusTitle. Silent - network/API errors just leave the role unset.
    // If setAsCustomName is true, also writes the title to CustomName and updates
    // the display text (used when the installed folder has a generic name like "scripts").
    void fetchNexusTitle(const QString &game, int modId, QListWidgetItem *item,
                         bool setAsCustomName = false);
    // Outcome of the "this mod page is already installed" prompt that fires
    // before a new download starts.  Three branches because two distinct user
    // intents share the same trigger (modId already installed):
    //   · Replace    - the new download is an update of the existing mod;
    //                  the old folder will be removed once the new one lands
    //                  (Nexus "Update" flow).
    //   · Separate   - the new download is a different file from the same
    //                  mod page (e.g. complementary optional file like
    //                  Sage's Backgrounds vs The Wretched And The Weird,
    //                  Nexus mod 58704); install in its own folder, leave
    //                  the existing entry alone.
    //   · Cancel     - abort the install.
    //   · NotInstalled - no existing match found; caller should just continue.
    enum class ReinstallChoice {
        NotInstalled,
        Replace,
        Separate,
        Cancel,
    };
    // Looks up an installed mod with the same (game, modId).  When one
    // exists, prompts the user to pick Replace / Separate / Cancel.  Returns
    // NotInstalled when there's nothing to disambiguate.  `except` is an
    // item to skip during the lookup (the placeholder currently being
    // installed).
    ReinstallChoice confirmReinstallIfInstalled(const QString &game, int modId,
                                                 QListWidgetItem *except = nullptr);
    void checkModDependencies(const QString &game, int modId, QListWidgetItem *item);
    // Fetches the Nexus file list for (game, modId). With autoPickMain=true,
    // the per-mod picker is skipped and the first MAIN/UPDATE file is taken;
    // used by batch update where a queue of 10+ pickers is worse than letting
    // the user cancel the few updates that need refining.
    void fetchModFiles(const QString &game, int modId, QListWidgetItem *item,
                       bool autoPickMain = false);
    void prepareItemForInstall(QListWidgetItem *item);
    // Rolls a placeholder row back to "not installed" after a mid-install
    // cancel (FOMOD cancel today; could be other cancel paths later).
    // Tolerates a null/removed placeholder.
    void resetPlaceholderAfterInstallCancel(QListWidgetItem *placeholder,
                                            const QString &archivePath);
    // Counterpart of addModFromPath() for the cross-profile completion
    // case: applies the "install just completed" role updates directly to
    // a stranded placeholder (one parked in m_strandedInstalls because
    // the user switched profiles mid-extract) WITHOUT touching m_modList,
    // load order, or openmw.cfg.  Those side effects belong to the
    // active profile and would corrupt the wrong list if run here.  The
    // caller is responsible for persistence via saveModListFor().
    void applyInstalledStateToStrandedPlaceholder(QListWidgetItem *placeholder,
                                                  const QString &modPath);
    // Same-modpage auto-link: when a new install shares a Nexus mod page with
    // an existing entry, point their DependsOn lists at each other so the
    // missing-dep warnings fire on "patch enabled, base disabled" etc.
    void autoLinkSameModpage(QListWidgetItem *item, const QString &categoryHint);

    // Conflict detection (orchestrated by LoadOrderController).  schedule()
    // arms the debounce timer; the timer's slot snapshots the modlist and
    // hands the map to the controller; onConflictsScanned writes ModRole
    // results back when the worker finishes.
    void scheduleConflictScan();
    void runConflictScan();
    void onConflictsScanned(const QHash<QString, QStringList> &byModPath);

    // -- Plugin load-order (decoupled from the mod list in the main window) --
    //
    // m_loadOrder is the ordered list of plugin filenames that gets written as
    // `content=` lines to openmw.cfg, independent of the visual order of mods
    // in the main window. Lifecycle:
    //   · loadLoadOrder()     - read from disk on startup (file per game)
    //   · reconcileLoadOrder()- add new plugins, drop removed ones; called
    //                           from saveModList() so the list is always in
    //                           sync with installed mods (enabled OR not)
    //   · autoSortLoadOrder() - topological sort: masters above dependents,
    //                           stable on existing order for tiebreaks. Runs
    //                           after every mod install so dependency order
    //                           is right without needing manual reordering.
    //   · saveLoadOrder()     - persist to disk
    //   · onEditLoadOrder()   - dialog to view / drag-reorder manually
    QStringList m_loadOrder;
    QString     loadOrderPath() const;
    void        loadLoadOrder();
    void        saveLoadOrder();
    void        reconcileLoadOrder();
    // Pull plugin order from openmw.cfg if it's been modified externally
    // (e.g. the user reordered in the OpenMW Launcher) since our last save.
    void        absorbExternalLoadOrder();
    void        autoSortLoadOrder();
    void        onEditLoadOrder();

    // Per-game config sync dispatcher. Called whenever the modlist changes.
    // Hooks into engine-specific writers (currently only OpenMW for the Morrowind
    // profile; other engines are placeholders until implemented).
    void syncGameConfig();

    // Self-heal for crash-interrupted installs: wipes any placeholder left in
    // the "installing" state (status=2) with no active network reply.
    void cleanStaleDownloads();

    // Self-heal for mods whose ModPath points at an empty fomod_install or
    // wrong subdirectory: rebinds to the parent if that's where the plugins
    // live. Runs once at startup.
    void repairEmptyModPaths();

    // Separator decorations (active/total counts per section)
    void updateSectionCounts();

    // Game profile management
    void updateGameButton();
    // Repaints m_profileBtn's text + menu from the registry.  Called after
    // any switch, add, rename, or delete.
    void updateProfileButton();
    QString modlistPath() const;
    QString forbiddenModsPath() const;
    GameProfile       &currentProfile();
    const GameProfile &currentProfile() const;
    // Pulls the current profile's modsDir/openmwPath/openmwLauncherPath into
    // the convenience mirror members and pokes m_downloadQueue (when ready).
    void applyCurrentProfileToMirrors();

    QListWidget                      *m_modList      = nullptr;
    ModListDelegate                  *m_delegate     = nullptr;
    QNetworkAccessManager            *m_net          = nullptr;
    NexusClient                      *m_nexus        = nullptr;
    NexusController                  *m_nexusCtl     = nullptr;
    InstallController                *m_installCtl   = nullptr;
    // Items whose pending title fetch should also promote the name into
    // CustomName/display text when it arrives.  Populated by
    // fetchNexusTitle(..., setAsCustomName=true) and consumed by
    // onTitleFetched.  Carries call-time UI policy without teaching the
    // controller about ModRole::CustomName.
    QSet<QListWidgetItem *>           m_titleSetsCustomName;
    // Items whose fetchFileList call asked to auto-pick the first MAIN/UPDATE
    // file (batch-update flow).  Consumed by onFileListFetched - same idea
    // as m_titleSetsCustomName.
    QSet<QListWidgetItem *>           m_autoPickMainItems;
    DownloadQueue                    *m_downloadQueue = nullptr;
    // Bulk-install throttle (drip-feed onInstallFromNexus to avoid hammering
    // the Nexus rate-limiter and stacking modal pickers) lives in
    // BulkInstallQueue.
    BulkInstallQueue                 *m_bulkInstall = nullptr;
    QTimer                           *m_animTimer    = nullptr;
    int                    m_animFrame    = 0;
    QPushButton           *m_dateSortBtn  = nullptr;
    bool                   m_dateSortAsc  = true;
    QPushButton           *m_sizeSortBtn  = nullptr;
    bool                   m_sizeSortAsc  = false; // biggest-first is more useful for "GB offenders"
    QString                m_apiKey;
    // API-key persistence - prefers QKeychain (libsecret / KWallet / DPAPI)
    // and transparently migrates from old QSettings storage on first run
    // with keychain available.  See implementations for fallback behaviour
    // when the keychain library isn't linked.
    void loadApiKey();
    void saveApiKey(const QString &key);
    // These mirror the current game profile's fields for convenience
    QString                m_modsDir;
    QString                m_openmwPath;
    QString                m_openmwLauncherPath;

    // Mod paths the user has approved for automatic groundcover= handling.
    // Persisted in QSettings so the choice survives restarts.
    QSet<QString>          m_groundcoverApproved;

    // Patch subfolders the user has chosen to keep disabled even after the
    // target mod is added.  Key = "<modPath>\t<subfolderName>", written to
    // QSettings so the choice survives restarts.  syncOpenMWConfig consults
    // this set on top of the auto-skip heuristic so a "no" answer from the
    // addModFromPath prompt sticks across sessions.
    QSet<QString>          m_declinedPatches;

    // In-flight install placeholders that have been parked across a profile
    // switch.  Key = "<gameId>__<profileName>"; value = items removed from
    // m_modList in stable order (preserves their original row positions
    // when restoreStrandedInstalls puts them back).  Items are owned here -
    // when m_modList no longer holds them, this hash does, and clear()ing
    // m_modList does NOT delete them.
    QHash<QString, QList<QListWidgetItem*>> m_strandedInstalls;

    // Stage 1 of the QListWidget→model decoupling.  Mirror of m_modList
    // as typed ModEntry rows; refreshed via modlist::refreshModelFromList
    // at strategic points (currently: after loadModList, before saveModList).
    // Future stages migrate readers to consume the model and eventually
    // flip mutation direction so this becomes the source of truth.  Owned
    // by MainWindow as a child QObject; lifetime ends with the window.
    ModlistModel         *m_model = nullptr;

    // Game profiles - owned by GameProfileRegistry.
    GameProfileRegistry   *m_profiles = nullptr;
    QToolButton           *m_gameBtn                = nullptr;
    // Modlist profile picker.  Sits next to the game button; click pops a
    // menu of "Switch to <name>", "New profile…", "Manage profiles…".
    QToolButton           *m_profileBtn             = nullptr;
    QToolButton           *m_featuredModlistsBtn    = nullptr;
    QAction               *m_actLaunchOpenMW          = nullptr;
    QAction               *m_actLaunchLauncher        = nullptr;
    QAction               *m_actLaunchGame            = nullptr;  // "▶ Start" for non-Morrowind
    QAction               *m_actLaunchSteamLauncher   = nullptr;  // "▶ Launcher" for non-Morrowind
    QAction               *m_actTuneSkyrimIni         = nullptr;  // "⚙ Tune INI" - Skyrim AE only
    QAction               *m_actSortLoot              = nullptr;  // "⇅ Sort with LOOT" - LOOT-supported profiles only
    QAction               *m_actMenuSortLoot          = nullptr;  // Mirror of m_actSortLoot under Mods menu (same profile gating)

    // User-customizable toolbar actions - registry/visibility/customize-dialog
    // logic owned by ToolbarCustomization.
    ToolbarCustomization *m_tbCustom = nullptr;


    ForbiddenModsRegistry *m_forbidden = nullptr;
    BackupManager         *m_backups   = nullptr;

    // Zoom - owned by ZoomController.
    ZoomController *m_zoom = nullptr;

    // Column header bar (owns visibility flags, per-state widths, the
    // resize-drag state machine, and the six visibility QActions).
    ColumnHeader   *m_columnHeader   = nullptr;
    bool            m_windowMaximized = false;  // mirrored from QMainWindow::isMaximized() for change-detection

    // Undo / redo - full-list snapshots, max kUndoLimit deep, owned by UndoStack.
    UndoStack                 *m_undoStack = nullptr;
    // Pre-launch sanity check: "don't warn again this session" - cleared
    // on app restart so a new install pass re-validates.
    bool                       m_suppressLaunchSanityCheck = false;

    // Conflict detection
    QTimer              *m_conflictTimer = nullptr;
    LoadOrderController *m_loadCtl       = nullptr;

    // Notification banner (shown temporarily above the mod list)
    NotifyBanner  *m_notify         = nullptr;
    QLabel        *m_modCountLabel  = nullptr;

    // First-launch reminder when LOOT isn't installed. Shows a clickable
    // banner if the current profile supports LOOT sorting and the binary
    // isn't on PATH / in a known install dir. Respects a user "don't
    // remind me" flag saved in QSettings.
    void maybeShowLootMissingBanner();

    // One-time welcome wizard (game, mods dir, API key, integrations).
    // Stored as a QSettings flag so it only runs once.
    void maybeShowFirstRunWizard();

    // Filter bar - live text filter + ★ favourites toggle, owned by FilterBar.
    FilterBar     *m_filterBar = nullptr;

    // -- Async filesystem scan state ---
    // Size scan + data-folders cache live in ScanCoordinator. The
    // missing-masters debounce timer + scan body stay here because the
    // scan is entangled with currentProfile() and m_groundcoverApproved.
    ScanCoordinator *m_scans = nullptr;
    QTimer *m_mastersScanTimer = nullptr;
    // True once loadModList has populated m_modList from disk.  Until then,
    // saveModList must NOT overwrite the on-disk file with an empty in-memory
    // list (loads-in-progress / load failures would otherwise wipe state).
    // After this flips true, an empty m_modList is a legitimate user delete
    // and gets persisted normally.
    bool m_modListLoaded = false;
    void runMissingMastersScan();  // slot wired to m_mastersScanTimer::timeout
    // Missing-master cache + in-flight tracking + actual scan live inside
    // LoadOrderController; this is just the UI-side sink that writes the
    // result map into ModRole::HasMissingMaster / MissingMasters.
    void onMissingMastersScanned(
        const QHash<QString, QPair<bool, QStringList>> &byModPath);

};

#endif // MAINWINDOW_H
