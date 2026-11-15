// launch_watch - what counts as the game running, and what the facts a
// launch leaves behind mean. The polling itself lives in DeployFlow.

#include "launch_watch.h"
#include "test_harness.h"

#include <QCoreApplication>

#include <iostream>

using namespace launch_watch;

namespace {

void testExtenderLogFor()
{
    std::cout << "testExtenderLogFor\n";
    const ExtenderLog f4 = extenderLogFor(QStringLiteral("f4se_loader.exe"));
    check("Fallout 4's loader names F4SE/f4se.log",
          f4.dirName == "F4SE" && f4.fileName == "f4se.log",
          f4.dirName + "/" + f4.fileName);
    const ExtenderLog se = extenderLogFor(QStringLiteral("skse64_loader.exe"));
    check("the folder drops the build digits, the file keeps them",
          se.dirName == "SKSE" && se.fileName == "skse64.log",
          se.dirName + "/" + se.fileName);
    const ExtenderLog nv = extenderLogFor(QStringLiteral("nvse_loader.exe"));
    check("New Vegas", nv.dirName == "NVSE" && nv.fileName == "nvse.log");
    const ExtenderLog path =
        extenderLogFor(QStringLiteral("/games/Fallout 4/F4SE_Loader.EXE"));
    check("a full path in any case is the same answer",
          path.dirName == "F4SE" && path.fileName == "f4se.log",
          path.dirName + "/" + path.fileName);
    check("a name that is not a loader has no log",
          extenderLogFor(QStringLiteral("Fallout4.exe")).dirName.isEmpty() &&
              extenderLogFor(QString()).dirName.isEmpty() &&
              extenderLogFor(QStringLiteral("_loader.exe")).dirName.isEmpty());
    check("digits alone are not an extender name",
          extenderLogFor(QStringLiteral("64_loader.exe")).dirName.isEmpty());
}

void testProcessRunning()
{
    std::cout << "testProcessRunning\n";
    // A command line is argv joined by NULs, as /proc gives it.
    const auto cmdline = [](const QStringList &args) {
        return args.join(QChar(QChar::Null));
    };
    const QStringList lines = {
        cmdline({QStringLiteral("/usr/bin/bash"), QStringLiteral("-l")}),
        cmdline({QStringLiteral("/usr/bin/python3"), QStringLiteral("script.py")}),
        cmdline({QStringLiteral("Z:\\mnt\\games\\Fallout 4\\Fallout4.exe")}),
    };
    check("a Proton command line with a Windows path is the game",
          processRunning(lines, QStringLiteral("Fallout4.exe")));
    check("the comparison ignores case, as Windows does",
          processRunning(lines, QStringLiteral("fallout4.EXE")));
    check("a game that is not there is not running",
          !processRunning(lines, QStringLiteral("SkyrimSE.exe")));
    check("an empty name never matches", !processRunning(lines, QString()));
    check("nothing running matches nothing",
          !processRunning({}, QStringLiteral("Fallout4.exe")));

    // The exe as one argument among several, Unix-style.
    const QString argv =
        cmdline({QStringLiteral("/usr/bin/proton"), QStringLiteral("run"),
                 QStringLiteral("/games/fnv/FalloutNV.exe")});
    check("the exe is found in any argument of the line",
          processRunning({argv}, QStringLiteral("FalloutNV.exe")));
    check("and a path that merely ends in the same letters is not it",
          !processRunning({argv}, QStringLiteral("NV.exe")));

    // Any argument counting is what makes the Proton runner read as the game
    // while it is still starting; the price is that naming the exe is enough.
    check("a command line that merely names the exe counts as the game",
          processRunning(
              {cmdline({QStringLiteral("/bin/cp"), QStringLiteral("/tmp/Fallout4.exe"),
                        QStringLiteral("/tmp/x")})},
              QStringLiteral("Fallout4.exe")));
}

void testTailLines()
{
    std::cout << "testTailLines\n";
    const QString log = QStringLiteral("first\r\nsecond\n\n   \nthird\nfourth\n");
    check("blank lines are dropped and the last n are kept, oldest first",
          tailLines(log, 2) ==
              QStringList{QStringLiteral("third"), QStringLiteral("fourth")},
          tailLines(log, 2).join(QLatin1Char('|')));
    check("asking for more than there is gives all of it",
          tailLines(log, 99).size() == 4);
    check("asking for none gives none", tailLines(log, 0).isEmpty());
    check("an empty log has no lines", tailLines(QString(), 5).isEmpty());
}

void testJudge()
{
    std::cout << "testJudge\n";
    Observation o;
    o.gameName = QStringLiteral("Fallout 4");
    check("never seen is never started", judge(o) == Verdict::NeverStarted);

    o.everSeen     = true;
    o.stillAlive   = true;
    o.secondsAlive = 4;
    check("still running says nothing, however briefly we watched",
          judge(o) == Verdict::Ran);

    o.stillAlive   = false;
    o.secondsAlive = 9;
    check("seen and gone again in seconds died early", judge(o) == Verdict::DiedEarly);

    o.secondsAlive = kAliveSecondsThatCount;
    check("a session that lasted is the user's own, however it ended",
          judge(o) == Verdict::Ran);
    o.secondsAlive = kAliveSecondsThatCount - 1;
    check("one second under the line still counts as early",
          judge(o) == Verdict::DiedEarly);
}

void testDetails()
{
    std::cout << "testDetails\n";
    Observation o;
    o.everSeen      = false;
    o.secondsWaited = 150;
    check("with no process the wait is what is reported",
          details(o).contains(QStringLiteral("150")), details(o));

    o.everSeen         = true;
    o.secondsAlive     = 7;
    o.extenderLogFound = true;
    o.extenderLogGrew  = false;
    o.extenderTail     = {QStringLiteral("RegisterPapyrusFunctions")};
    const QString d    = details(o);
    check("a log that did not grow is called out, with its last lines",
          d.contains(QStringLiteral("did not grow")) &&
              d.contains(QStringLiteral("RegisterPapyrusFunctions")) &&
              d.contains(QStringLiteral("7 second")),
          d);

    o.extenderLogGrew = true;
    check("a log that grew is not accused of anything",
          !details(o).contains(QStringLiteral("did not grow")));

    o.newCrashLogs = {QStringLiteral("/p/crash-2026-11-15.log")};
    check("a new crash log is named first",
          details(o).contains(QStringLiteral("crash-2026-11-15.log")));

    Observation empty;
    empty.everSeen = true;
    check("with no logs at all there is still the one fact we have",
          !details(empty).isEmpty() && !details(empty).contains(QStringLiteral("log")));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "== launch_watch ==\n";
    testExtenderLogFor();
    testProcessRunning();
    testTailLines();
    testJudge();
    testDetails();
    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
