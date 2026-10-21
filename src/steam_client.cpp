#include "steam_client.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <unistd.h>

namespace steam_client {
namespace {

QString defaultDotSteam()
{
    const QString home    = QDir::homePath();
    const QString native  = home + QStringLiteral("/.steam");
    const QString flatpak = home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.steam");
    if (!QFileInfo::exists(native + QStringLiteral("/registry.vdf"))
        && QFileInfo::exists(flatpak + QStringLiteral("/registry.vdf")))
        return flatpak;
    return native;
}

bool isSteamProcess(qint64 pid)
{
    if (pid <= 0) return false;
    QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!comm.open(QIODevice::ReadOnly)) return false;
    return QString::fromLatin1(comm.readAll()).trimmed()
        .startsWith(QLatin1String("steam"), Qt::CaseInsensitive);
}

// Milliseconds since the process started: /proc/<pid>/stat's start time (in
// clock ticks since boot) against /proc/uptime. -1 when either cannot be read.
qint64 processAgeMs(qint64 pid)
{
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    QFile uptime(QStringLiteral("/proc/uptime"));
    if (!stat.open(QIODevice::ReadOnly) || !uptime.open(QIODevice::ReadOnly)) return -1;
    // The command name in parentheses may hold spaces: split after it.
    const QString line  = QString::fromLatin1(stat.readAll());
    const int     after = line.lastIndexOf(QLatin1Char(')'));
    const QStringList f = line.mid(after + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // Fields 3.. follow the name; starttime is field 22 overall.
    if (f.size() < 20) return -1;
    bool ok = false;
    const double startTicks = f[19].toDouble(&ok);
    if (!ok) return -1;
    const double up = QString::fromLatin1(uptime.readAll()).section(QLatin1Char(' '), 0, 0).toDouble();
    const long ticks = sysconf(_SC_CLK_TCK);
    if (ticks <= 0) return -1;
    return qint64((up - startTicks / double(ticks)) * 1000.0);
}

} // namespace

State state(const QString &dotSteam, const PidCheck &isSteam, const AgeCheck &ageOf)
{
    const QString dot = dotSteam.isEmpty() ? defaultDotSteam() : dotSteam;
    const PidCheck alive = isSteam ? isSteam : PidCheck(isSteamProcess);
    const AgeCheck age   = ageOf ? ageOf : AgeCheck(processAgeMs);

    QString registry;
    if (QFile f(dot + QStringLiteral("/registry.vdf")); f.open(QIODevice::ReadOnly))
        registry = QString::fromUtf8(f.readAll());

    static const QRegularExpression kPid(QStringLiteral(R"RX("SteamPID"\s+"(\d+)")RX"));
    static const QRegularExpression kUser(QStringLiteral(R"RX("ActiveUser"\s+"(\d+)")RX"));

    qint64 pid = 0;
    if (const auto m = kPid.match(registry); m.hasMatch()) pid = m.captured(1).toLongLong();
    if (!alive(pid)) {
        // The bootstrap writes steam.pid before the client fills in the
        // registry, so a Steam still starting up shows here first.
        if (QFile f(dot + QStringLiteral("/steam.pid")); f.open(QIODevice::ReadOnly))
            pid = QString::fromLatin1(f.readAll()).trimmed().toLongLong();
    }

    State s;
    s.running = alive(pid);
    if (s.running) s.runningForMs = std::max<qint64>(0, age(pid));
    const auto user = kUser.match(registry);
    s.knowsLogin = user.hasMatch();
    s.loggedIn   = s.running && s.knowsLogin && user.captured(1).toLongLong() > 0;
    return s;
}

Next next(const State &s, qint64 waitedMs)
{
    if (s.loggedIn) return Next::Go;
    if (s.running && !s.knowsLogin && s.runningForMs >= kNoLoginGraceMs) return Next::Go;
    if (waitedMs >= kTimeoutMs) return Next::GiveUp;
    return Next::Wait;
}

} // namespace steam_client
