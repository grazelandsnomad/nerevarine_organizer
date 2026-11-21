#pragma once

// launch_watch - did the game that Start just launched actually run?
//
// Twice a launch failed with nothing on screen: Steam was closed and the
// script extender's instance was lost (2026-10-19), and a mod's broken records
// crashed the data load (2026-10-22). Both times Start returned, the game
// vanished, and the app said nothing; finding the cause took a Proton trace.
//
// Nothing has to be hooked to do better. The game shows in /proc under its own
// exe name (a Windows path, under Proton), the script extender writes a log
// whose growth says how far loading got, and a crash logger drops a file
// beside it. This module holds the parts of that with no filesystem and no
// widgets in them - what counts as the game running, and what the collected
// facts mean - so they can be tested directly. The caller does the polling.

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace launch_watch {

// Where the script extender keeps its log, derived from the loader's file
// name: "f4se_loader.exe" -> {"F4SE", "f4se.log"}, "skse64_loader.exe" ->
// {"SKSE", "skse64.log"}. The folder is the name without its trailing version
// digits, upper-cased, and sits under the prefix's "Documents/My Games/<game>".
// Empty for a name that is not a loader. A game that breaks the pattern wants
// an adapter override rather than a special case here.
struct ExtenderLog {
    QString dirName;  // "F4SE"
    QString fileName; // "f4se.log"
};
ExtenderLog extenderLogFor(const QString &loaderExeName);

// Is a process of `exeName` among these /proc command lines? Under Proton the
// command line carries a Windows path ("Z:\\...\\Fallout4.exe"), so only the
// last component is compared, and without case, as Windows would.
//
// ANY argument counts, not just the first: the game is started as an argument
// of the Proton runner ("proton run .../Fallout4.exe"), and seeing the runner
// is how a launch that is still starting up reads as alive rather than as one
// that never happened. The cost is that a process merely NAMING the exe - a
// shell command, a script being edited - reads as the game too. Nothing on
// the launch path does that, but a test driver passing the name on its own
// command line will find itself.
bool processRunning(const QStringList &cmdlines, const QString &exeName);

// Every process command line on this machine, one entry per process. The one
// impure function here; separated so the matcher above stays testable.
QStringList runningCommandLines();

// One watch's bookkeeping, shared by the Start watch and the Launcher watch so
// the two cannot drift: whether the process was ever seen alive, and when it
// was first and last seen.
struct Sighting {
    bool   seen        = false;
    qint64 firstSeenMs = -1;
    qint64 lastSeenMs  = -1;
};

// Record one poll. True when the watch is over: the process has come and gone,
// or the wait has run out - alive or not, since past that point the app stops
// knowing anything about the launch and the button has to come back.
bool concluded(Sighting &s, bool alive, qint64 nowMs, qint64 giveUpMs);

// The last `n` lines of `text` that carry anything, oldest first.
QStringList tailLines(const QString &text, int n);

// What one launch looked like. The caller fills this in as it polls.
struct Observation {
    QString gameName;
    bool    everSeen      = false; // the game appeared in /proc at all
    bool    stillAlive    = false; // and was still there when watching stopped
    int     secondsAlive  = 0;     // first sighting to last
    int     secondsWaited = 0;     // how long we watched before giving up
    // The script extender's log: whether it grew while the game ran, and its
    // last lines. A log that never grew means the extender never got going;
    // one that stops mid-load names the step that did not finish.
    bool        extenderLogFound = false;
    bool        extenderLogGrew  = false;
    QStringList extenderTail;
    // Crash logs (Buffout 4, Crash Logger) that were not there before.
    QStringList newCrashLogs;
};

enum class Verdict {
    Ran,          // nothing to say
    NeverStarted, // it never appeared
    DiedEarly,    // it appeared and was gone again very quickly
};

// A launch that lives longer than this is the user's own session, however it
// ends, and is never reported.
constexpr int kAliveSecondsThatCount = 90;

Verdict judge(const Observation &o);

// The lines under the summary in the report: what was watched and what the
// logs hold. Empty when there is nothing to add. Plain text, for a details
// box - no markup, so it can be copied into a bug report as it stands.
QString details(const Observation &o);

} // namespace launch_watch
