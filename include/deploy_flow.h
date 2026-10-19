#pragma once

// DeployFlow - the Bethesda deploy and undeploy, the "never deployed" hint,
// the script-extender check, the Skyrim ini tuner and every game launch,
// over the WindowShell. The Bethesda path resolution and the deploy and
// undeploy workers are file-statics in deploy_flow.cpp, where they always
// were; the window reaches the flow through the few entry points below.

#include "game_runtime.h"

#include <QDialog>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

#include <functional>

class QTimer;
class WindowShell;

class DeployFlow : public QObject {
    Q_OBJECT
public:
    explicit DeployFlow(WindowShell &shell, QObject *parent = nullptr);

public slots:
    void onDeployBethesda();        // experimental: link enabled mods into a Bethesda game's Data/
    void onUndeployBethesda();      // revert a Bethesda deployment, restoring the original game files
    void onInspectDeployment();     // read-only diagnostics: resolved paths + manifest state
    void onTuneSkyrimIni();         // BethINI-style INI tweaks for Skyrim SE
    void onLaunchOpenMW();
    void onLaunchOpenMWLauncher();
    void onLaunchGame();            // generic launch for non-Morrowind games
    void onLaunchSteamLauncher();   // launches the game's official launcher

public:
    // Bethesda profiles only load what is physically in the game's Data
    // folder, so a modlist that was never deployed does nothing at all - and
    // nothing said so. Shows a clickable sticky banner while the active
    // profile is deploy-capable, has enabled installed mods, and has never
    // been deployed, or was deployed from a list that has changed since.
    void updateDeployHint();
    // The same, once per turn of the event loop: a save fires one per row,
    // and a switch must finish before its list is judged.
    void scheduleDeployHint();
    // What the active profile would deploy right now, as the digest the last
    // deploy recorded beside its manifest (bethesda_deploy::sourceFingerprint):
    // enabled installed mods in load order, each as path + install date, and
    // the plugin load order. Different from the recorded one = stale.
    QString deployFingerprint() const;
    // A mod's files changed on disk without the list changing - a reinstall
    // into the same folder, a variant swapped in. The fingerprint cannot see
    // that, so the deploy recorded for `modlistFile` is marked stale by hand.
    void markDeployStale(const QString &modlistFile);
    // Re-sync Data/ before launching, if deployed before. Best-effort, silent.
    void maybeDeployBeforeLaunch(const QString &id);
    // Sums the warning signals (missing deps, empty installs, forbidden
    // enabled, stale script extenders); summary dialog if any fire. False on
    // Cancel. Missing masters are left out of the game instead (see the .cpp).
    bool confirmLaunchIfWarnings();
    // Mark the rows whose script-extender DLL predates the installed game.
    // Called when Data/ changes (deploy, and the pre-launch re-sync), never
    // from a paint path: it reads every plugin DLL in the game folder.
    void refreshScriptExtenderFlags();
    // What the installed game says about itself, cached per profile: the
    // executable's version and whether a script extender sits beside it.
    const game_runtime::Probe &gameRuntimeProbe();
    // The game's Data/ and what this manager deployed into it, for telling
    // the game's own files (masters, DLC, Creation Club) from deployed ones -
    // the FOMOD wizard's file conditions ask (fomod_facts.h). `deployed` is
    // relative to Data/, lower-cased, '/'-separated. Empty when the game has
    // no Data/ or it cannot be found; never prompts.
    struct GameData {
        QString       dataDir;
        QSet<QString> deployed;
    };
    GameData gameData();

private:
    // Block launch on a pending kernel reboot (true = abort). Arch pacman wipes
    // /usr/lib/modules/<running-kernel> on kernel upgrade, so DRM/input module
    // loads fail and OpenMW dies with no clear error.
    bool refuseLaunchIfRebootPending();
    // savePath fires when the resolved path changes (auto-detected via PATH or
    // picked in the dialog); the call site owns the persisted-key choice via its
    // typed Settings:: accessor.
    void launchProgram(QString &storedPath,
                       std::function<void(const QString &)> savePath,
                       const QString &execName, const QString &locateTitle,
                       bool monitored = false);
    // Run a script-extender loader through Proton once Steam is up - starting
    // Steam and waiting for it, behind a small dialog, when it is not
    // (steam_client.h says why it has to be).
    void launchWhenSteamReady(const QString &appId, const QString &loader);
    QPointer<QDialog> m_steamWait;   // the wait in progress, if any

    WindowShell &m_shell;
    QTimer      *m_deployProgressTimer = nullptr;
    QTimer      *m_deployHintTimer     = nullptr;
    // Pre-launch sanity check "don't warn again this session" - cleared on
    // restart so a new install pass re-validates.
    bool m_suppressLaunchSanityCheck = false;
    game_runtime::Probe m_gameRuntime;      // cached; see gameRuntimeProbe()
    QString             m_gameRuntimeFor;   // profile id the cache is for
};
