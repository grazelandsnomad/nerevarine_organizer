#include "launch_watch.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QRegularExpression>

namespace launch_watch {

ExtenderLog extenderLogFor(const QString &loaderExeName)
{
    QString base = QFileInfo(loaderExeName).fileName();
    if (base.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive)) base.chop(4);
    if (!base.endsWith(QLatin1String("_loader"), Qt::CaseInsensitive)) return {};
    base.chop(7);
    if (base.isEmpty()) return {};

    // "skse64" -> "SKSE": the folder is named for the extender, the file for
    // the build of it that runs.
    QString dir = base;
    while (!dir.isEmpty() && dir.back().isDigit()) dir.chop(1);
    if (dir.isEmpty()) return {};
    return {dir.toUpper(), base.toLower() + QStringLiteral(".log")};
}

bool processRunning(const QStringList &cmdlines, const QString &exeName)
{
    if (exeName.isEmpty()) return false;
    const QString want =
        exeName.section(QRegularExpression(QStringLiteral("[\\\\/]")), -1);
    if (want.isEmpty()) return false;
    for (const QString &line : cmdlines) {
        // A command line is argv joined by NULs; any argument may be the exe
        // (Proton passes it as one), so each is checked on its own.
        const QStringList args = line.split(QLatin1Char('\0'), Qt::SkipEmptyParts);
        for (const QString &arg : args) {
            const QString leaf =
                arg.section(QRegularExpression(QStringLiteral("[\\\\/]")), -1);
            if (leaf.compare(want, Qt::CaseInsensitive) == 0) return true;
        }
    }
    return false;
}

QStringList runningCommandLines()
{
    QStringList       out;
    const QDir        proc(QStringLiteral("/proc"));
    const QStringList pids = proc.entryList(QStringList{QStringLiteral("[0-9]*")},
                                            QDir::Dirs | QDir::NoDotAndDotDot);
    out.reserve(pids.size());
    for (const QString &pid : pids) {
        QFile f(proc.filePath(pid + QStringLiteral("/cmdline")));
        // A process that exits between the listing and the read is normal.
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = f.readAll();
        if (!bytes.isEmpty()) out << QString::fromLocal8Bit(bytes);
    }
    return out;
}

QStringList tailLines(const QString &text, int n)
{
    if (n <= 0) return {};
    QStringList lines;
    const auto  all = text.split(QRegularExpression(QStringLiteral("\r?\n")));
    for (const QString &l : all)
        if (!l.trimmed().isEmpty()) lines << l.trimmed();
    if (lines.size() > n) lines = lines.mid(lines.size() - n);
    return lines;
}

Verdict judge(const Observation &o)
{
    if (!o.everSeen) return Verdict::NeverStarted;
    if (o.stillAlive) return Verdict::Ran;
    // However a session that lasted ends, it is the user's own.
    if (o.secondsAlive >= kAliveSecondsThatCount) return Verdict::Ran;
    return Verdict::DiedEarly;
}

QString details(const Observation &o)
{
    QStringList out;
    if (o.everSeen)
        out << QStringLiteral("The game ran for about %1 second(s).").arg(o.secondsAlive);
    else
        out << QStringLiteral(
                   "No process of the game appeared in %1 second(s) of watching.")
                   .arg(o.secondsWaited);

    if (!o.newCrashLogs.isEmpty()) {
        out << QString();
        out << QStringLiteral("A crash log was written:");
        for (const QString &c : o.newCrashLogs) out << QStringLiteral("  ") + c;
    }

    if (o.extenderLogFound) {
        out << QString();
        out << (o.extenderLogGrew
                    ? QStringLiteral("The script extender's log, last lines:")
                    : QStringLiteral("The script extender's log did not grow during this "
                                     "launch, so it never got going. Its last lines:"));
        if (o.extenderTail.isEmpty())
            out << QStringLiteral("  (the log is empty)");
        else
            for (const QString &l : o.extenderTail) out << QStringLiteral("  ") + l;
    }
    return out.join(QLatin1Char('\n'));
}

} // namespace launch_watch
