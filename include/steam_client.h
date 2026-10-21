#pragma once

// steam_client - is the Steam client running, and logged in, as its own files
// say.
//
// A Steam game started outside Steam needs a Steam client that is already up.
// On 2026-10-19 Fallout 4 was started through F4SE under Proton with Steam
// closed: the game found no Steam, started it with steam://run/377160 so that
// Steam would relaunch it, and the F4SE instance was gone before F4SE loaded a
// single plugin. Steam then waited for that instance to go away and started
// nothing. On 10-10 the same command had worked - Steam had been open since
// 10-08. So a launch through Proton waits for this first.
//
// Steam keeps its state in ~/.steam: steam.pid, written by the bootstrap, and
// registry.vdf, where HKLM/.../Steam/SteamPID is the running client (0 once it
// has exited) and HKCU/.../Steam/ActiveProcess/ActiveUser the account logged
// in. Two keys, read with regular expressions the way store_scan reads
// libraryfolders.vdf.
//
// Qt Core only; Linux (/proc).

#include <QString>
#include <QtGlobal>

#include <functional>

namespace steam_client {

struct State {
    bool   running      = false;   // the client process is alive
    bool   loggedIn     = false;   // ...and an account is logged in
    bool   knowsLogin   = false;   // registry.vdf names an ActiveUser at all
    qint64 runningForMs = 0;       // how long that process has been up
};

// Whether `pid` is the Steam client, and for how long it has run (ms; -1 for
// not known). The defaults ask /proc/<pid>/comm and /proc/<pid>/stat, so a pid
// file left behind by a Steam that has exited, its number since reused, does
// not pass for one.
using PidCheck = std::function<bool(qint64 pid)>;
using AgeCheck = std::function<qint64(qint64 pid)>;

// `dotSteam`: the folder holding registry.vdf and steam.pid. Empty picks
// ~/.steam, or Flatpak's when only that one has a registry.
State state(const QString &dotSteam = {}, const PidCheck &isSteam = {},
            const AgeCheck &ageOf = {});

// What a launch waiting for Steam does after each look: go once an account is
// logged in; give up after kTimeoutMs. The Steam on this machine writes no
// ActiveUser at all, so without the key a client that has run for
// kNoLoginGraceMs counts as signed in - one already open when Start is
// pressed goes at once, one just started gets its sign-in time.
enum class Next { Wait, Go, GiveUp };
constexpr qint64 kTimeoutMs      = 90000;
constexpr qint64 kNoLoginGraceMs = 15000;
Next next(const State &s, qint64 waitedMs);

} // namespace steam_client
