#include "steam_client.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

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

} // namespace

State state(const QString &dotSteam, const PidCheck &isSteam)
{
    const QString dot = dotSteam.isEmpty() ? defaultDotSteam() : dotSteam;
    const PidCheck alive = isSteam ? isSteam : PidCheck(isSteamProcess);

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
    const auto user = kUser.match(registry);
    s.knowsLogin = user.hasMatch();
    s.loggedIn   = s.running && s.knowsLogin && user.captured(1).toLongLong() > 0;
    return s;
}

Next next(const State &s, qint64 waitedMs, qint64 runningForMs)
{
    if (s.loggedIn) return Next::Go;
    if (s.running && !s.knowsLogin && runningForMs >= kNoLoginGraceMs) return Next::Go;
    if (waitedMs >= kTimeoutMs) return Next::GiveUp;
    return Next::Wait;
}

} // namespace steam_client
