#ifndef HEROIC_LAUNCH_H
#define HEROIC_LAUNCH_H

// heroic_launch - the command that runs a Heroic game's own exe.
//
// Asking Heroic to start a game (heroic://launch/gog/<id>) runs whatever
// Heroic is configured to run, which for Cyberpunk 2077 is REDprelauncher.
// The Start button is meant to start the GAME and leave the bundled launcher
// to the Launcher button, which is what it already does for Steam titles, so
// it has to run the exe itself - in the prefix Heroic made, with the runner
// Heroic recorded, or the game sees an empty prefix and no DLL overrides.
//
// Pure: runner, prefix and exe in, program/arguments/environment out. Nothing
// here touches the disk or starts anything, so the shape of the command is
// pinned by a test instead of discovered by a user whose game did not start.
//
// The environment is the one Heroic itself sets (setupWineEnvVars, Heroic
// 2.22), and what each variable does was established by running the runner
// in the real Cyberpunk prefix with `cmd /c exit` as the payload, not by
// reading the script:
//
//   - STEAM_COMPAT_DATA_PATH is the directory that CONTAINS pfx, which is
//     what Heroic's prefix directory is. Without it the script exits at once.
//   - STEAM_COMPAT_CLIENT_INSTALL_PATH has to be PRESENT: setup_prefix()
//     reads it straight out of the environment and dies with a KeyError when
//     it is missing. A first attempt left it out on the strength of the
//     script's own try_get_steam_dir() treating it as optional, and got exit
//     1. Its VALUE need not exist - the script only stats under it and copies
//     what it finds. Heroic passes $HOME/.steam/steam without checking, umu
//     (which Heroic runs Proton through) passes "", and the payload returned
//     0 with the real Steam directory, with /nonexistent/steam and with the
//     empty string alike. So a machine without Steam is not a reason to
//     refuse.
//   - STEAM_COMPAT_APP_ID=0, SteamAppId=0 and SteamGameId=heroic-<appName>
//     are what Heroic sets for every Proton game. SteamGameId is what lets a
//     later PROTON_LOG=1 write ~/steam-heroic-<id>.log at all: without it,
//     Proton forces its own logging off.
//
// Not reproduced: umu's Steam Linux Runtime container, and Heroic's per-game
// wrapper and environment options. The direct command is the one that has
// been proven; if a game turns out to need the container, this is the one
// function to change (umu-run <exe> with WINEPREFIX, PROTONPATH, GAMEID).

#include "store_scan.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace heroic_launch {

struct Command {
    QString                        program;      // the runner to execute
    QStringList                    args;         // including the exe
    QList<QPair<QString, QString>> env;          // layered on the child's
    QString                        workingDir;   // the exe's own folder
    [[nodiscard]] bool valid() const { return !program.isEmpty(); }
};

// Empty program when anything needed is missing - no runner, no prefix, no
// exe - which the caller must treat as "fall back to asking Heroic", never as
// a command to run.
Command build(const store_scan::HeroicRunner &runner, const QString &winePrefix,
              const QString &exePath, const QStringList &exeArgs = {});

} // namespace heroic_launch

#endif // HEROIC_LAUNCH_H
