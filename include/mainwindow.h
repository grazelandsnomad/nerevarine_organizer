#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QColor>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QPersistentModelIndex>
#include <QMainWindow>
#include <QHash>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QUuid>

#include <expected>
#include <functional>

#include "conflict_direction.h"  // Directions complete type: by-value slot arg below
#include "plugin_records.h"      // RecordClash likewise
#include "loadordercontroller.h" // TranslationCoverage likewise
#include "downloadqueue.h"
#include "installcontroller.h"   // for InstallController::VerifyFailKind in slot
#include "save_queue.h"          // serialized off-thread save writes (by-value member)
#include "nexusclient.h"
#include "game_profiles.h"
#include "download_watch.h"   // Watcher* member, and StickyKind routing
#include "modentry.h"
#include "deps_resolver.h"            // ModEntry complete type: QList<ModEntry> by value below

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
class ScanOverlay;
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
    // Drag-drop entry point for archives. Public so the nested QListWidget
    // subclass in the .cpp can reach it without a friend.
    // `nexusUrlHint` is stamped onto the new row when the caller could work
    // out which mod page the archive came from (a manual download's file name
    // carries its mod id), so a hand-installed mod still gets update checks.
    void installLocalArchive(const QString &archivePath,
                             const QString &nexusUrlHint = {});
    void handleDroppedImportFile(const QString &path);

private slots:
    void onAddSeparator();
    void onAddMod();
    // Shared insert for onAddSeparator (append) and "Add separator above".
    // targetRow clamped to [0, count]. Never spawn collapsed - it'd land between
    // a collapsed neighbour and its hidden children and swallow that section.
    void addSeparatorAtRow(int targetRow);
    void onRemoveSelected();
    void onMoveUp();
    void onMoveDown();
    void onCheckUpdates();
    // The same check narrowed to named mods, for the launch dialog's "check
    // these for updates". Labels are matched against each row's display name,
    // which is what the deploy manifest recorded them as. Returns how many
    // rows were actually checked, which is not always how many were asked
    // for: a label with no matching row is skipped rather than counted.
    int  checkUpdatesForMods(const QStringList &labels);
    void onCheckUpdatesFinished(int foundCount);
    void onTitleFetched(QListWidgetItem *item, const QString &name);
    // A download is under way for a file whose name says it is built for the
    // other Skyrim runtime. Warns and names the file that fits this profile.
    void onModFileSiblings(QListWidgetItem *item, const QString &chosenName,
                           const QStringList &siblingNames);
    void onExpectedChecksumFetched(QListWidgetItem *item, const QString &fileName,
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
    void onExtractionCancelled(const QString &archivePath,
                               const QString &extractDir,
                               const QUuid &installToken);
    void onReviewUpdates();
    void onContextMenu(const QPoint &pos);
    void onItemDoubleClicked(QListWidgetItem *item);
    void onSetApiKey();
    void onSetModsDir();
    void onSetLanguage(const QString &language);
    void onLaunchOpenMW();
    void onLaunchOpenMWLauncher();
    // Sums the four warning signals (missing masters/deps, empty installs,
    // forbidden enabled); summary dialog if any fire. False on Cancel.
    bool confirmLaunchIfWarnings();
    // Mark the rows whose script-extender DLL predates the installed game.
    // Called when Data/ changes (deploy, and the pre-launch re-sync), never
    // from a paint path: it reads every plugin DLL in the game folder.
    void refreshScriptExtenderFlags();
    // Block launch on a pending kernel reboot (true = abort). Arch pacman wipes
    // /usr/lib/modules/<running-kernel> on kernel upgrade, so DRM/input module
    // loads fail and OpenMW dies with no clear error.
    bool refuseLaunchIfRebootPending();
    void onLaunchGame();            // generic launch for non-Morrowind games
    void onLaunchSteamLauncher();   // launches the game's official launcher (e.g. SkyrimSELauncher.exe)
    void onTuneSkyrimIni();         // BethINI-style INI tweaks for Skyrim SE
    // Dependency canvas: draws the DependsOn web and applies edits back
    // through the same write path the "Required mods" dialog uses.
    void onDependencyGraph();
    void onAnimTick();
    void onSortByDate();
    void onSortBySize();
    void onSortSeparators();
    // Sort by Size / Date is a TEMPORARY view sort: it reorders only the
    // display, never the saved load order. enterViewSort() stamps each row's
    // current (saved) position into ModRole::SortAnchor once and flags the lens
    // active; every persistence walk then goes through rowOrderForPersist() so
    // what's written to disk stays in the saved order. The lens ends via
    // resetToSavedOrder() (restore the saved order to the display) or
    // dropViewSortKeepingOrder() (a deliberate reorder commits the current
    // display as the new saved order).
    void enterViewSort();
    void resetToSavedOrder();
    void dropViewSortKeepingOrder();
    void clearViewSortState();   // cosmetic reset on list rebuild (banner + flag)
    QList<int> rowOrderForPersist() const;
    QList<ModEntry> snapshotEntriesForPersist() const;
    // Rotation-proof good-state checkpoint of the current saved order, taken
    // right before any sort so a scrambling sort is always recoverable. Cheap
    // on purpose (no syncGameConfig/scans, unlike saveModList): call before the
    // view-only Size/Date sorts too, so it must stay light.
    void checkpointBeforeSort();
    void onInspectOpenMWSetup();
    void onInspectConflicts();
    void onDeployBethesda();        // experimental: link enabled mods into a Bethesda game's Data/
    void onUndeployBethesda();      // revert a Bethesda deployment, restoring the original game files
    void maybeDeployBeforeLaunch(const QString &id);  // re-sync Data/ before launching, if deployed before
    void onInspectDeployment();     // read-only diagnostics: resolved paths + manifest state
    void onTriageOpenMWLog();
    void onModlistSummary();
    void onToggleTheme();           // flip light <-> dark, persist, update button
    void onMoveModsDir();
    // Zip modlist + load-order files, openmw.cfg, the tail of OpenMW.log, and a
    // system summary into nerev_diagnostics_<timestamp>.zip for bug reports.
    // Asks where to save, reveals when done. No network, no PII beyond modlist.
    void onCreateDiagnosticBundle();

    // Cheap entry points: just (re)arm a debounce timer. Real work runs on a
    // QtConcurrent worker and posts results back to the UI.
    void scanMissingMasters();   // schedule a missing-master rescan
    void scanMissingDependencies(); // in-memory DependsOn -> warning-icon sweep

    void switchToGame(int idx);
    // Switch modlist profile within the current game: save current modlist +
    // load order to the OLD profile, flip the active index, point m_modsDir at
    // the new modsDir, reload from the NEW profile, re-sync openmw.cfg.
    void switchToModlistProfile(int idx);
    // Create/clone/rename/delete dialog for the current game.
    void onManageModlistProfiles();
    // New empty profile (profile menu, Wabbajack-into-new-profile flow).
    // Returns the new index, or -1 if cancelled / bad name. Does NOT switch.
    int  createNewModlistProfile(const QString &suggestedName = QString());
    // Make sure the active profile has a modsDir before installing into it;
    // fires the "where to store this profile's mods?" prompt when empty. True if
    // a dir exists after, false on cancel. No-op when set.
    bool ensureModsDirForActiveProfile();
    // Profile/game switches used to destroy in-flight install placeholders
    // (status==2 items) with the rest of the rows, leaving InstallController's
    // handlers with a dangling pointer and an "aborted" mod. These park the
    // items per-profile before the clear and restore them after loadModList,
    // so an FNV->Morrowind->FNV trip mid-extract finds the install still
    // running. Token = "<gameId>__<profileName>".
    QString currentProfileKey() const;
    void    strandInflightInstalls();
    void    restoreStrandedInstalls();
    // Pull any of this profile's mods living outside its modsDir into it. Main
    // case: a cloned profile still points at the source's mods on disk; copy/
    // move them in to decouple the two profiles. No-op when all mods are already
    // inside m_modsDir. Refuses mid-download or with m_modsDir unset.
    void onConsolidateModsIntoActiveProfile();
    // Reclaim mod folders nothing points at any more: older builds left behind
    // by upgrades, extract dirs from installs that never finished, leftovers
    // from "remove but keep files". Previews everything with sizes and deletes
    // only on explicit confirm. Refuses mid-download or mid-install.
    void onCleanUpModFolders();
    void onAddGame();
    // Detect an external (non-OpenMW) game in Steam/Heroic/Lutris, prompt for
    // the exe if not found, create a profile and switch. From pinned-game menu.
    void addAndDetectGame(const QString &gameId, const QString &displayName);
    // Offer a mod archive that just landed in the download folder. Only runs
    // for a profile whose game has no mod-manager download route at all.
    void onDownloadCaught(const QString &path);
    // Start or stop that watcher for the current profile.
    void updateDownloadWatch();
    // The tail of addAndDetectGame: mods dir + profile creation, once the game
    // has been located (by .exe, or by folder for OpenGothic).
    void createGameProfile(const QString &gameId, const QString &displayName);
    void onCurrentModChanged(QListWidgetItem *current, QListWidgetItem *previous);

    bool eventFilter(QObject *obj, QEvent *event) override;

    // Drop installable archives (.7z/.zip/.rar/.fomod) onto non-list areas;
    // ModListWidget::dropEvent forwards external file drops here.
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent  *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // -- Breaking someone else's declared dependency ------------------
    //
    // What the user chose when told an action leaves declared dependencies
    // unmet. ProceedAndCascade means they asked for the dependents to be
    // dealt with too, and `cascadeRows` says which.
    enum class DepBreak { Cancel, Proceed, ProceedAndCascade };

    // Front an action that would newly break dependents. `after` is the
    // hypothetical modlist the action would produce. Returns Proceed
    // immediately when nothing breaks, so callers need no guard of their own.
    //
    // `disable` applies the same action to a row when computing the cascade
    // closure; pass {} for actions with no in-place equivalent.
    DepBreak confirmDependencyBreakage(
        const QList<deps::ModEntry> &before,
        const QList<deps::ModEntry> &after,
        const QString &body,
        const QString &cascadeLabel,
        const QString &leaveLabel,
        bool includeDisabled,
        const std::function<void(deps::ModEntry &)> &disable,
        QList<int> *cascadeRows);

    // Rows unticked since the last event-loop turn, checked in one batch.
    void checkPendingDisables();

    void setupMenuBar();
    void setupToolbar();
    void setupCentralWidget();

    void saveModList();
    // Debounced save (150ms) for high-traffic mutation sites (drag reorder,
    // hold-to-repeat move, rapid favorite/collapse toggles); calls within the
    // window collapse to one saveModList(). Sync saveModList() stops this timer
    // at entry so a sync caller (closeEvent, profile switch, undo apply) can't
    // be raced by a stale schedule.
    void scheduleSaveModList();
    // Surface a worker-thread file-write failure on the UI. The async save chain
    // (saveModList + syncOpenMWConfig) hits disk on QtConcurrent to kill the grey
    // freeze, but a QFile::open failure there (read-only mount, AppImage wrong
    // dir, full disk) used to vanish into log.txt with no user signal. Workers
    // invokeMethod this via QPointer<MainWindow> so it runs on the UI thread,
    // then NotifyBanner flashes a 7s red banner naming the failed file.
    void onAsyncWriteFailed(const QString &filePath, const QString &reason);
    // Persist one placeholder row's state into a SPECIFIC profile's modlist file
    // - for when an InstallController signal lands while the user is on another
    // profile. Replaces or appends the entry matching `placeholder`'s
    // installToken. Touches nothing else (not m_modList, load order, openmw.cfg,
    // or saveModList's side effects). No-op when the token is null or the path
    // won't resolve.
    void saveModListFor(const QString &profileKey, QListWidgetItem *placeholder);
    // Absolute modlist path for a profile key (the <gameId>__<profileName> form
    // currentProfileKey emits). Empty when no profile matches.
    QString modlistPathFor(const QString &profileKey) const;
    // Share a mod into another profile WITHOUT copying files: append a row for
    // `source` (same ModPath) to `targetProfileKey`'s modlist, deduping if it
    // already references the mod. copyConfig carries enabled/FOMOD/annotation/
    // deps; else the row starts disabled at defaults. Writes the foreign file
    // synchronously; never targets the active profile. False on resolve/write
    // failure.
    bool shareModIntoProfile(const ModEntry &source,
                             const QString &targetProfileKey, bool copyConfig);
    // True when a non-active profile references this exact (cleaned) mod folder
    // - keeps a disk delete from wiping files a shared-into profile points at.
    bool modPathReferencedByOtherProfile(const QString &cleanPath) const;
    // Parse the on-disk modlist of every profile except the active one. The
    // active profile is excluded because callers hold it live in m_modList,
    // where it may already differ from the file.
    QList<QPair<QString, QList<ModEntry>>> otherProfileModlists() const;
    // Find an in-flight placeholder by InstallToken: m_modList first, then
    // m_strandedInstalls (parked across profile switches). When found in a
    // stranded bucket, `outProfileKey` (if non-null) gets the bucket key for
    // saveModListFor. nullptr on no match.
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
    // Seed the modlist from an existing openmw.cfg, for users coming from plain
    // openmw-launcher who'd otherwise re-add every mod by hand. Reads data=/
    // content=/groundcover=, skips the vanilla Data Files folder, one managed
    // row per remaining data= path, load order from content=/groundcover= in
    // file order.
    void onImportFromOpenMWConfig();
    // Helpers for menu slots and the drop handler.
    void doImportMO2ModList(const QString &path);
    // onImportFromOpenMWConfig body taking the cfg path - split out so future
    // drag-drop / CLI entry points can skip the file picker.
    void doImportOpenMWConfig(const QString &cfgPath);
    void doImportWabbajack(const QString &path);
    void finishWabbajackImport(const QJsonObject &root);
    void doImportNerevarineModList(const QString &path);
    static QString detectModsBaseFromFile(const QString &path);
    bool confirmReplaceModList();
    void onViewChangelog(QListWidgetItem *item);
    void onNewModList();
    // savePath fires when the resolved path changes (auto-detected via PATH or
    // picked in the dialog); the call site owns the persisted-key choice via its
    // typed Settings:: accessor.
    void launchProgram(QString &storedPath,
                       std::function<void(const QString&)> savePath,
                       const QString &execName, const QString &locateTitle,
                       bool monitored = false);
    void checkNxmHandlerRegistration();
    void checkDesktopShortcut();
    void collapseSection(QListWidgetItem *sep, bool collapse);
    void onEditSeparator(QListWidgetItem *item);

    // (installLocalArchive is public: so the inline ModListWidget drop handler
    // can queue-dispatch to it without a friend.)
    void sendSelectedToSeparator(QListWidgetItem *sep);
    void openSendToDialog();
    // Bulk-move selection to row 0 or past the last row, keeping relative order.
    // "Send to -> Top / Bottom".
    void sendSelectedToEdge(bool toBeginning);
    void updateModCount();
    void addModFromPath(const QString &dirPath, QListWidgetItem *placeholder = nullptr);
    void purgeDuplicatePlaceholders(QListWidgetItem *installed);

    // Optional prompts addModFromPath fires once a mod is registered. Each is
    // self-guarding (no-op when N/A) so the call site stays three plain calls;
    // the "what counts as X" checks live in post_install:: to stay testable.
    // modRoot is the installed mod's folder.
    //   groundcover:   offer to manage a grass mod via groundcover= lines.
    //   splash:        offer to clear the base-game splash so the mod's shows.
    //   bundled-patch: re-enable "<N> ... for <ThisMod>" subfolders in other
    //                  mods that were auto-skipped while this mod was absent.
    void runGroundcoverHelper(QListWidgetItem *item, const QString &modRoot);
    void runSplashScreenHelper(const QString &modRoot);
    void offerBundledPatchReenable(QListWidgetItem *item);

    // Kick off extraction. Returns immediately - the extract runs in
    // InstallController::extractArchive (QProcess) and reports back via
    // extractionSucceeded/Failed. The return carries synchronous preconditions
    // only; on unexpected(reason) the controller is never called, so callers
    // must not resume the install. When preconditions pass, the outcome arrives
    // via the signal path regardless of the return.
    //   "null-placeholder" - placeholder was nullptr
    //   "archive-missing"  - archivePath missing or empty
    //   "mods-dir-unset"   - m_modsDir empty (first run misconfigured)
    [[nodiscard]] std::expected<void, QString>
    extractAndAdd(const QString &archivePath, QListWidgetItem *placeholder);

    // Verify the finished archive's size & md5 against Nexus's declared values
    // before extracting. Size check is synchronous; md5 runs on a QtConcurrent
    // worker. On failure, delete the archive and reset the placeholder to
    // not-installed. No expected values recorded (local-archive drops, older
    // installs) -> skip verify, run extractAndAdd as-is.
    // Return carries synchronous preconditions only, same as extractAndAdd:
    //   "null-placeholder" - placeholder was nullptr
    //   "archive-missing"  - archivePath missing or empty
    [[nodiscard]] std::expected<void, QString>
    verifyAndExtract(const QString &archivePath, QListWidgetItem *placeholder);

    // Thin wrapper; real logic in DownloadQueue.
    void setupDownloadQueue();
    void onInstallFromNexus(QListWidgetItem *item);
    // Fetch the Nexus mod page `name` and cache it as ModRole::NexusTitle.
    // Silent - network/API errors leave the role unset. setAsCustomName also
    // writes it to CustomName + updates display text (for when the installed
    // folder has a generic name like "scripts").
    void fetchNexusTitle(const QString &game, int modId, QListWidgetItem *item,
                         bool setAsCustomName = false);
    // Outcome of the "this mod page is already installed" prompt before a new
    // download. Distinct intents share one trigger (modId already installed):
    //   Replace    - update of the existing mod; old folder removed once the new
    //                one lands (Nexus "Update" flow).
    //   Separate   - a different file from the same page (e.g. Sage's Backgrounds
    //                vs The Wretched And The Weird, mod 58704); own folder,
    //                existing entry untouched.
    //   Merge      - an optional file overriding the main download (e.g. OAAB
    //                Data optionals); overlay onto the existing folder
    //                (last-writer-wins), one entry. MO2 "merge".
    //   Cancel     - abort.
    //   NotInstalled - no match; caller continues.
    enum class ReinstallChoice {
        NotInstalled,
        Replace,
        Separate,
        Merge,
        Cancel,
    };
    // Look up an installed mod with the same (game, modId); if found, prompt
    // Replace/Merge/Separate/Cancel. NotInstalled when there's nothing to
    // disambiguate. `except` is an item to skip (the placeholder being
    // installed). allowMerge=false hides Merge for callers that can't honour it
    // (Search-on-Nexus installs into `item` itself - no folder to overlay).
    ReinstallChoice confirmReinstallIfInstalled(const QString &game, int modId,
                                                 QListWidgetItem *except = nullptr,
                                                 bool allowMerge = true);
    // Hard block for forbidden mods. True when (game, modId) is clear; false
    // (after the "forbidden" dialog, with a shortcut to the manager) when
    // blocked. Call as `if (!confirmNotForbidden(...)) return;`. No
    // install-anyway escape.
    bool confirmNotForbidden(const QString &game, int modId);
    // Guards against a download landing in the wrong game's profile: an
    // nxm:// link names its game, so a Starfield mod clicked while a Skyrim
    // profile is active is a mistake the manager can see coming. Returns true
    // when the install should proceed - either because the active profile
    // already serves that game (the silent path), because the user let us
    // switchToGame() the right one first, or because they chose to install it
    // here anyway. False means cancel. See game_match.h for the lookup.
    //
    // Wired to handleNxmUrl only, and deliberately: it is the one entry point
    // that starts from a URL rather than from a row. Every other download
    // path (updates, dependency resolution, Search-on-Nexus) is handed a
    // QListWidgetItem that lives in m_modList, and switchToGame() clears
    // m_modList - so calling this there would hand the caller a dangling item.
    // Those paths take their game from a row that is already in this profile,
    // so there is nothing for them to mismatch against anyway.
    bool confirmGameForDownload(const QString &game);
    // "Translate mod...": collects every player-visible string the mod's
    // plugins carry, opens the editor, and turns what the user types into a
    // separate translation mod inserted directly below this one. The original
    // mod folder is never written to - see translation_mod.h for why that
    // shape was chosen over patching the plugin in place.
    void onTranslateMod(QListWidgetItem *item);
    // "Package as archive...": zips the mod's folder contents ready for a mod
    // page. Offered ONLY on translations this app generated
    // (ModRole::IsGeneratedTranslation) - it exists to publish your own work,
    // not to repackage someone else's mod. Runs off the UI thread.
    void onPackageMod(QListWidgetItem *item);
    // The language mods should be in for the ACTIVE modlist profile: its own
    // override when it has one, else the shared default. Empty means the user
    // has never been asked - callers must not read that as English, which is
    // the bug this replaced (see target_language.h).
    QString translationLanguage() const;
    // Where this mod's half-finished translation lives, and whether any of it
    // exists yet.
    //
    // One helper because THREE places need the same answer - the context menu,
    // the Edit Mod button and the untranslated scan - and if they ever disagree
    // the marker says there is no work while the editor happily resumes it,
    // which is the hardest kind of wrong to notice.
    //
    // Keyed on the Nexus mod page, so a rename or a reinstall does not orphan
    // the file. A name-keyed file from before that change is still found.
    QString translationProgressPathFor(const QListWidgetItem *item) const;
    // What a mod's saved translation work amounts to: how many answers are
    // stored, how many of those a human has actually vouched for, how big the
    // job is, and whether it has already been built into a translation mod.
    //
    // `saved` and `done` are different questions and the difference is the
    // point. A machine-translate run fills every row at once, so `saved` hits
    // its ceiling before the user has read a word and then never moves again -
    // it is fit to answer "is there work here at all" and unfit to be shown as
    // progress. `done` is what the user is actually doing.
    //
    // One helper for all three readers - the scan that paints the row, the
    // context menu and the Edit Mod button - because they used to stat the
    // same file separately and could disagree about it.
    struct TranslationProgressState {
        int  saved = 0;         // answers on file - the gate: is there work here
        int  done  = 0;         // of those, ones a human has vouched for
        int  total = 0;         // strings the mod offered at the last sitting
        bool built = false;
    };
    TranslationProgressState translationProgressStateFor(
        const QListWidgetItem *item) const;
    // Same, but asks once and stores the answer as the shared default, so no
    // other game or profile is ever asked again. Empty return means the user
    // cancelled and the caller must abort.
    QString ensureTranslationLanguage();
    // Rebuilds Settings > Mod translation language for the active profile.
    // The Settings menu is built once and never rebuilt, so the checkmark has
    // to be re-pointed whenever the profile changes.
    void refreshTranslationLanguageMenu();
    // Sets (or clears, on empty) the ACTIVE profile's override, persists it,
    // and re-runs the scan when the notices are on.
    void setProfileTranslationLanguage(const QString &token);
    void checkModDependencies(const QString &game, int modId, QListWidgetItem *item);
    // Fetch the Nexus file list for (game, modId). autoPickMain=true skips the
    // per-mod picker and takes the first MAIN/UPDATE file; used by batch update,
    // where 10+ stacked pickers are worse than letting the user cancel the few
    // that need it.
    void fetchModFiles(const QString &game, int modId, QListWidgetItem *item,
                       bool autoPickMain = false);
    void prepareItemForInstall(QListWidgetItem *item);
    // Roll a placeholder back to not-installed after a mid-install cancel (FOMOD
    // cancel today). Tolerates a null/removed placeholder.
    void resetPlaceholderAfterInstallCancel(QListWidgetItem *placeholder,
                                            const QString &archivePath);
    // Cross-profile counterpart of addModFromPath: applies the "install
    // completed" role updates to a stranded placeholder (parked in
    // m_strandedInstalls after a profile switch mid-extract) WITHOUT touching
    // m_modList, load order, or openmw.cfg - those belong to the active profile
    // and would corrupt the wrong list. Caller persists via saveModListFor.
    void applyInstalledStateToStrandedPlaceholder(QListWidgetItem *placeholder,
                                                  const QString &modPath);
    // "Merge into existing" follow-through. When `placeholder` carries a pending
    // ModRole::MergeTargetPath (set in handleNxmUrl), overlay every file from
    // `contentPath` onto that folder (last-writer-wins, optional overrides
    // main), delete the redundant `discardDir`, and return the merge target as
    // the row's path. No pending merge (or vanished target) -> returns
    // `contentPath` unchanged, leaves `discardDir`. Consumes the role.
    QString applyPendingMerge(QListWidgetItem *placeholder,
                              const QString &contentPath,
                              const QString &discardDir);
    // Copy-on-write fork for a folder shared with another profile: verified-copy
    // `sharedPath` into a fresh folder under m_modsDir, return its path (empty
    // on failure, after warning). Used before an in-place mutation (merge
    // overlay) so the other profile's files stay put. Download-based mutations
    // (reinstall/update) extract fresh and just repoint the row - skip this.
    QString forkSharedModFolder(const QString &sharedPath);
    // When a new install shares a Nexus mod page with an existing entry, point
    // their DependsOn lists at each other so missing-dep warnings fire when the
    // patch is enabled but the base isn't.
    void autoLinkSameModpage(QListWidgetItem *item, const QString &categoryHint);

    // Conflict detection (run by LoadOrderController). schedule() arms the
    // debounce; the timer slot snapshots the modlist and hands it to the
    // controller; onConflictsScanned writes ModRole results back on finish.
    void scheduleConflictScan();
    void runConflictScan();
    void onConflictsScanned(
        const QHash<QString, conflict_direction::Directions> &byModPath,
        const QHash<QString, QList<plugin_records::RecordClash>> &recordClashes);
    // Stamps ModRole::ConflictHighlight on every row from `current`'s conflict
    // partners. Called on selection change and after each scan lands.
    void applyConflictHighlights(QListWidgetItem *current);
    // Move `mod` to sit directly below (after=true) or above the row whose
    // ModPath is `targetPath`, which is how a conflict gets flipped: the lower
    // mod wins. No-op if either row is gone.
    void moveModAdjacentTo(QListWidgetItem *mod, const QString &targetPath,
                           bool after);
    // Conflict entries for the row's context menu: jump to each counterpart,
    // or flip which one overwrites the other.
    void buildConflictMenu(QMenu *menu, QListWidgetItem *item);

    // Translation coverage. Unlike the conflict scan this is opt-in: it reads
    // and decompresses record bodies, so it only runs while the toolbar toggle
    // is on. schedule() no-ops when the toggle is off.
    // Bethesda profiles only load what is physically in the game's Data
    // folder, so a modlist that was never deployed does nothing at all - and
    // nothing said so. Shows a clickable sticky banner while the active
    // profile is deploy-capable, has enabled installed mods, and has never
    // been deployed. Clicking it deploys.
    void updateDeployHint();

    void scheduleTranslationScan();
    void runTranslationScan();
    // modsWithoutPlugins: enabled mods the scan never opened, because they
    // carry no plugin. Shown in the summary so it reports what it checked
    // rather than implying it checked everything.
    void onTranslationsScanned(const QHash<QString, TranslationCoverage> &byModPath,
                               int modsWithoutPlugins);
    // Wipe the roles when the toggle goes off, so a stale verdict can't be
    // left painted on a row.
    void clearTranslationMarks();

    // Plugin load-order, decoupled from the visual mod list. m_loadOrder is the
    // ordered plugin filenames written as `content=` lines to openmw.cfg,
    // independent of the on-screen mod order. Lifecycle:
    //   loadLoadOrder()      - read from disk on startup (file per game)
    //   reconcileLoadOrder() - add new / drop removed plugins; from saveModList()
    //                          so it tracks installed mods (enabled or not)
    //   autoSortLoadOrder()  - topo sort masters above dependents, stable
    //                          tiebreaks; after every install
    //   saveLoadOrder()      - persist
    //   onEditLoadOrder()    - manual view / drag-reorder dialog
    QStringList m_loadOrder;
    QString     loadOrderPath() const;
    void        loadLoadOrder();
    void        saveLoadOrder();
    void        reconcileLoadOrder();
    // Pull plugin order from openmw.cfg if it changed externally since our last
    // save (e.g. reordered in the OpenMW Launcher).
    void        absorbExternalLoadOrder();
    void        autoSortLoadOrder();
    void        onEditLoadOrder();

    // Per-game config sync, called on every modlist change. Dispatches to
    // engine-specific writers (only OpenMW/Morrowind today; rest stubbed).
    void syncGameConfig();

    // Self-heal crash-interrupted installs: wipe any placeholder stuck at
    // status=2 with no active network reply.
    void cleanStaleDownloads();

    // Self-heal ModPaths pointing at an empty fomod_install or wrong subdir:
    // rebind to the parent if the plugins live there. Runs once at startup.
    void repairEmptyModPaths();

    // Per-section active/total counts on separators.
    void updateSectionCounts();

    // Game profile management
    void updateGameButton();
    // Repaint m_profileBtn text + menu from the registry after a
    // switch/add/rename/delete.
    void updateProfileButton();
    // Flip the conflict arrows/captions/highlight on or off, persist it, and
    // repaint. m_conflictNoticesBtn names the state the click switches TO.
    void onToggleConflictNotices();
    void updateConflictNoticesButton();
    // Same for the untranslated-mods captions. Switching it ON kicks off the
    // scan; switching it OFF wipes the roles so nothing stale stays painted.
    void onToggleUntranslatedNotices();
    // Put the untranslated notices back to off (game/profile switch).
    void resetUntranslatedNotices();
    void updateUntranslatedButton();
    // Repaint m_themeBtn to name the theme it switches TO.
    void updateThemeButton();
    // Re-bake text colours into the transparent toolbar buttons (Profile, theme
    // toggle) from the live palette after a theme swap.
    void restyleToolbarTextButtons();
    QString modlistPath() const;
    QString forbiddenModsPath() const;
    // Rebind m_forbidden to the current game's forbidden-mods file.
    void    reloadForbiddenMods();
    GameProfile       &currentProfile();
    const GameProfile &currentProfile() const;
    // Pull the current profile's modsDir/openmwPath/openmwLauncherPath into the
    // mirror members and poke m_downloadQueue when ready.
    void applyCurrentProfileToMirrors();

    QListWidget                      *m_modList      = nullptr;
    ModListDelegate                  *m_delegate     = nullptr;
    QNetworkAccessManager            *m_net          = nullptr;
    NexusClient                      *m_nexus        = nullptr;
    NexusController                  *m_nexusCtl     = nullptr;
    InstallController                *m_installCtl   = nullptr;
    // Items whose pending title fetch should also promote the name into
    // CustomName/display text on arrival. Set by fetchNexusTitle(...,
    // setAsCustomName=true), consumed by onTitleFetched. Keeps the controller
    // from needing to know about ModRole::CustomName.
    QSet<QListWidgetItem *>           m_titleSetsCustomName;
    // Items whose fetchFileList asked to auto-pick the first MAIN/UPDATE file
    // (batch update). Consumed by onFileListFetched; same idea as above.
    QSet<QListWidgetItem *>           m_autoPickMainItems;
    DownloadQueue                    *m_downloadQueue = nullptr;
    // Bulk-install throttle: drip-feeds onInstallFromNexus to avoid hammering
    // the Nexus rate-limiter and stacking modal pickers.
    BulkInstallQueue                 *m_bulkInstall = nullptr;
    QTimer                           *m_animTimer    = nullptr;
    int                    m_animFrame    = 0;
    QPushButton           *m_dateSortBtn  = nullptr;
    bool                   m_dateSortAsc  = true;
    QPushButton           *m_sizeSortBtn  = nullptr;
    bool                   m_sizeSortAsc  = false; // biggest-first finds "GB offenders"
    // True while a temporary Size/Date view sort is layered over the saved
    // order. Drives rowOrderForPersist() so saves never write the sorted order.
    bool                   m_viewSortActive = false;
    QString                m_apiKey;
    // API-key persistence - prefers QKeychain (libsecret/KWallet/DPAPI),
    // migrates from old QSettings storage on first run when keychain is
    // available. See impls for the fallback when keychain isn't linked.
    void loadApiKey();
    void saveApiKey(const QString &key);
    void deleteApiKey();            // scrub the key from keychain + QSettings
    void validateApiKeyAndReport(); // GET validate.json; report premium/free/rejected
    void checkExtractorsAvailable();// one-time startup nag if 7z/unzip/unrar absent
    // Convenience mirrors of the current game profile's fields.
    QString                m_modsDir;
    QString                m_openmwPath;
    QString                m_openmwLauncherPath;

    // Mod paths approved for automatic groundcover= handling. In QSettings so
    // it survives restarts.
    QSet<QString>          m_groundcoverApproved;

    // Patch subfolders kept disabled even after the target mod is added.
    // Key = "<modPath>\t<subfolderName>", in QSettings. syncOpenMWConfig checks
    // this on top of the auto-skip heuristic so a "no" from the addModFromPath
    // prompt sticks across sessions.
    QSet<QString>          m_declinedPatches;

    // In-flight install placeholders parked across a profile switch.
    // Key = "<gameId>__<profileName>"; value = items removed from m_modList in
    // stable order (so restoreStrandedInstalls restores row positions). Owned
    // here once m_modList drops them, so clear()ing m_modList won't delete them.
    QHash<QString, QList<QListWidgetItem*>> m_strandedInstalls;

    // Stage 1 of QListWidget->model decoupling: a typed-ModEntry copy of
    // m_modList, refreshed via modlist::refreshModelFromList at key points
    // (after loadModList, before saveModList). Later stages move readers onto
    // the model and flip mutation direction so this becomes the source of truth.
    // Child QObject; dies with the window.
    ModlistModel         *m_model = nullptr;

    // Game profiles - owned by GameProfileRegistry.
    GameProfileRegistry   *m_profiles = nullptr;
    QToolButton           *m_gameBtn                = nullptr;
    // Modlist profile picker, next to the game button; menu of "Switch to
    // <name>", "New profile...", "Manage profiles...".
    QToolButton           *m_profileBtn             = nullptr;
    QToolButton           *m_featuredModlistsBtn    = nullptr;
    QToolButton           *m_themeBtn                = nullptr;  // light/dark toggle
    QToolButton           *m_conflictNoticesBtn      = nullptr;  // show/hide overwrite notices
    QToolButton           *m_untranslatedBtn         = nullptr;  // show/hide untranslated notices
    // Whether those notices are showing. Deliberately not persisted: the
    // button starts off on every launch and resets on every game or profile
    // switch, because turning it on runs a scan that reads and decompresses
    // plugin record bodies, and inheriting that from a click made last week
    // is not something the user asked for.
    bool                   m_untranslatedNotices      = false;
    // Settings > Mod translation language. Held because the Settings menu is
    // built once in setupMenuBar() and never rebuilt, while the checkmark it
    // shows belongs to whichever modlist profile is active.
    QMenu                 *m_translationLangMenu     = nullptr;
    QLabel                *m_profileLbl              = nullptr;  // "Profile:" prefix
    QAction               *m_actLaunchOpenMW          = nullptr;
    QAction               *m_actLaunchLauncher        = nullptr;
    QAction               *m_actLaunchGame            = nullptr;  // "▶ Start" for non-Morrowind
    QAction               *m_actLaunchSteamLauncher   = nullptr;  // "▶ Launcher" for non-Morrowind
    QAction               *m_actTuneSkyrimIni         = nullptr;  // "⚙ Tune INI" - Skyrim AE only
    QAction               *m_actDepGraph              = nullptr;  // dependency canvas
    QAction               *m_actSortLoot              = nullptr;  // "⇅ Sort with LOOT" - LOOT-supported profiles only
    QAction               *m_actMenuSortLoot          = nullptr;
    QAction               *m_actWatchDownloads        = nullptr;  // Settings menu; manual-download games only  // Mirror of m_actSortLoot under Mods menu (same profile gating)
    QAction               *m_actDeployBethesda        = nullptr;  // "Deploy to game" - Bethesda titles only (Mods menu)
    QAction               *m_actUndeployBethesda      = nullptr;  // "Remove deployed mods" - Bethesda titles only (Mods menu)
    QAction               *m_actInspectDeployment     = nullptr;  // "Inspect deployment" diagnostics - Bethesda titles only

    // User-customizable toolbar actions - registry/visibility/customize-dialog
    // logic in ToolbarCustomization.
    ToolbarCustomization *m_tbCustom = nullptr;


    ForbiddenModsRegistry *m_forbidden = nullptr;
    BackupManager         *m_backups   = nullptr;

    // Zoom - owned by ZoomController.
    ZoomController *m_zoom = nullptr;

    // Column header bar (owns visibility flags, per-state widths, the
    // resize-drag state machine, and the six visibility QActions).
    ColumnHeader   *m_columnHeader   = nullptr;
    bool            m_windowMaximized = false;  // mirrored from QMainWindow::isMaximized() for change-detection

    // Undo / redo - full-list snapshots, max kUndoLimit deep, in UndoStack.
    UndoStack                 *m_undoStack = nullptr;
    // Pre-launch sanity check "don't warn again this session" - cleared on
    // restart so a new install pass re-validates.
    bool                       m_suppressLaunchSanityCheck = false;
    // One shot: the next finished update check opens the review screen rather
    // than only writing to the status bar. Set when the user asked the
    // question from the launch dialog, where a list they can install from is
    // the answer they were after.
    bool                       m_reviewAfterCheck = false;

    // Conflict detection
    QTimer              *m_conflictTimer = nullptr;
    // Disable-warning batching. NEVER exec a dialog straight from
    // itemChanged: that signal is emitted from inside QListModel::setData,
    // and re-entering the event loop there lets a reorder move rows out from
    // under the item pointers. A zero-shot timer also coalesces "disable all
    // in section" into one dialog instead of one per row.
    QTimer                     *m_depWarnTimer = nullptr;
    QList<QPersistentModelIndex> m_pendingDisables;
    bool                        m_depWarnSuspended = false;
    LoadOrderController *m_loadCtl       = nullptr;
    // Last scan's directed conflicts, keyed by mod path. Counterparts carry
    // their own path, so the row lookup survives duplicate and edited display
    // names. Drives ModRole::ConflictHighlight and the context menu's
    // reorder entries.
    QHash<QString, conflict_direction::Directions> m_conflicts;
    // Record-level clashes from the same scan: mods sharing no file whose
    // plugins still rewrite the same records.
    QHash<QString, QList<plugin_records::RecordClash>> m_recordClashes;

    // Translation coverage. Longer debounce than the conflict scan (this one
    // reads record bodies), and only armed while the toggle is on.
    QTimer      *m_translationTimer   = nullptr;
    // Progress panel over the mod list while that scan runs. Reading every
    // plugin takes long enough that a still window reads as a hung one, and a
    // status-bar line in the far corner does not fix that.
    ScanOverlay *m_translationOverlay = nullptr;
    // Progress panel for the Bethesda deploy, which runs off-thread. Its own
    // overlay rather than sharing the translation one: a translation scan
    // finishing mid-deploy would otherwise call finish() and take the deploy's
    // panel down with it.
    ScanOverlay *m_deployOverlay       = nullptr;
    QTimer      *m_deployProgressTimer = nullptr;
    // Set when the user turns the toggle on, so that scan ends on a result
    // panel they have to acknowledge. The scan also re-runs after any list
    // edit, and a dialog on every checkbox click would be intolerable - those
    // runs just update the rows and close the panel.
    bool         m_translationResultWanted = false;

    // Which sticky banner is currently up, so stickyClicked routes to the
    // right action. Was a bool for the deploy hint; a third kind arrived and a
    // second bool would have made "which one is up" a question with two
    // answers.
    enum class StickyKind { ViewSort, DeployHint, CaughtDownload };
    StickyKind     m_stickyKind = StickyKind::ViewSort;
    // The file the CaughtDownload banner is offering.
    QString        m_caughtDownload;

    // Notification banner (shown temporarily above the mod list)
    NotifyBanner  *m_notify         = nullptr;
    // Watches the browser's download folder, for games Nexus offers no
    // mod-manager download for (Gothic 2). Null until such a profile is active.
    download_watch::Watcher *m_dlWatch = nullptr;
    QLabel        *m_modCountLabel  = nullptr;

    // First-launch reminder when LOOT isn't installed. Clickable banner when the
    // current profile supports LOOT sorting and the binary isn't on PATH / in a
    // known install dir. Respects a "don't remind me" flag in QSettings.
    void maybeShowLootMissingBanner();

    // One-time welcome wizard (game, mods dir, API key, integrations); gated by
    // a QSettings flag.
    void maybeShowFirstRunWizard();

    // Filter bar - live text filter + ★ favourites toggle, owned by FilterBar.
    FilterBar     *m_filterBar = nullptr;

    // Async filesystem scan state. Size scan + data-folders cache live in
    // ScanCoordinator. The missing-masters debounce timer + scan body stay here
    // because the scan is tangled up with currentProfile() and
    // m_groundcoverApproved.
    ScanCoordinator *m_scans = nullptr;
    QTimer *m_mastersScanTimer = nullptr;
    QTimer *m_saveModListTimer = nullptr;  // debounce - see scheduleSaveModList()
    // True once loadModList has populated m_modList from disk. Until then,
    // saveModList must NOT overwrite the on-disk file with an empty in-memory
    // list, or a load in progress / load failure wipes state. Once true, an
    // empty m_modList is a real user delete and gets persisted.
    bool m_modListLoaded = false;

    // Serialized off-thread save writes (snapshot backup copies +
    // openmw.cfg/launcher.cfg/modlist writes offloaded off the UI thread to keep
    // Add/Edit-mod from greying out). Writes run one at a time in submission
    // order on a single background thread; the UI thread only ever enqueues.
    // closeEvent drains it so the work has landed on disk before exit.
    SaveQueue m_saveQueue;
    void runMissingMastersScan();  // slot wired to m_mastersScanTimer::timeout
    // Missing-master cache + in-flight tracking + scan live in
    // LoadOrderController; this is the UI-side sink that writes the result map
    // into ModRole::HasMissingMaster / MissingMasters.
    void onMissingMastersScanned(
        const QHash<QString, QPair<bool, QStringList>> &byModPath);

};

#endif // MAINWINDOW_H
