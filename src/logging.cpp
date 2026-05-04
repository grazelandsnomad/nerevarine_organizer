#include "logging.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef Q_OS_UNIX
#  include <execinfo.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

namespace logging {

Q_LOGGING_CATEGORY(lcApp,        "nerev.app")
Q_LOGGING_CATEGORY(lcInstall,    "nerev.install")
Q_LOGGING_CATEGORY(lcNexus,      "nerev.nexus")
Q_LOGGING_CATEGORY(lcOpenMW,     "nerev.openmw")
Q_LOGGING_CATEGORY(lcLoadOrder,  "nerev.loadorder")
Q_LOGGING_CATEGORY(lcScan,       "nerev.scan")
Q_LOGGING_CATEGORY(lcModList,    "nerev.modlist")
Q_LOGGING_CATEGORY(lcLaunch,     "nerev.launch")
Q_LOGGING_CATEGORY(lcFomod,      "nerev.fomod")
Q_LOGGING_CATEGORY(lcUi,         "nerev.ui")

namespace {

constexpr qint64 kLogMaxBytes      = 4 * 1024 * 1024; // 4 MB per file
constexpr int    kRotatedBackups   = 4;               // keep .1, .2, .3, .4

QString  g_logPath;
QString  g_logDir;
QMutex   g_logMutex;

void rotateLogs(const QString &basePath)
{
    // Slide back: .3 → .4, .2 → .3, ... live → .1
    QFile::remove(basePath + "." + QString::number(kRotatedBackups));
    for (int i = kRotatedBackups; i > 1; --i) {
        const QString from = basePath + "." + QString::number(i - 1);
        const QString to   = basePath + "." + QString::number(i);
        if (QFile::exists(from)) QFile::rename(from, to);
    }
    if (QFile::exists(basePath))
        QFile::rename(basePath, basePath + ".1");
}

void writeLogLine(const QString &line)
{
    QMutexLocker lock(&g_logMutex);
    if (g_logPath.isEmpty()) return;

    QFileInfo fi(g_logPath);
    if (fi.exists() && fi.size() > kLogMaxBytes)
        rotateLogs(g_logPath);

    QFile f(g_logPath);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")
       << ' ' << line << '\n';
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext &ctx,
                      const QString &msg)
{
    const char *lvl;
    switch (type) {
    case QtDebugMsg:    lvl = "DEBUG"; break;
    case QtInfoMsg:     lvl = "INFO "; break;
    case QtWarningMsg:  lvl = "WARN "; break;
    case QtCriticalMsg: lvl = "CRIT "; break;
    case QtFatalMsg:    lvl = "FATAL"; break;
    default:            lvl = "INFO "; break; // QtSystemMsg / future kinds
    }
    QString line = QString("[%1] ").arg(lvl);
    if (ctx.category && *ctx.category && std::strcmp(ctx.category, "default") != 0)
        line += QString("[%1] ").arg(ctx.category);
    line += msg;
    if (ctx.file && *ctx.file)
        line += QString(" (%1:%2)").arg(ctx.file).arg(ctx.line);
    writeLogLine(line);
    // std::fprintf rather than std::println: dropping <print> lets the
    // build target GCC 13 (Ubuntu 22.04's regular repos) instead of GCC 14
    // (only in the ubuntu-toolchain-r/test PPA, which has been timing out
    // from Azure-hosted GitHub runners).  The line is already a complete
    // formatted string at this point - no formatting features are lost.
    const QByteArray utf8 = line.toUtf8();
    std::fprintf(stderr, "%s\n", utf8.constData());
    if (type == QtFatalMsg) std::abort();
}

#ifdef Q_OS_UNIX
// Signal-safe crash handler. Cannot use Qt inside a signal handler, so we
// write directly to the log fd with backtrace_symbols_fd and re-raise.
void crashSignalHandler(int sig)
{
    const char *name = "SIGNAL";
    switch (sig) {
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGABRT: name = "SIGABRT"; break;
        case SIGFPE:  name = "SIGFPE";  break;
        case SIGILL:  name = "SIGILL";  break;
        case SIGBUS:  name = "SIGBUS";  break;
    }
    const QByteArray path = g_logPath.toLocal8Bit();
    int fd = ::open(path.constData(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd >= 0) {
        const char hdr1[] = "\n==== CRASH: ";
        ::write(fd, hdr1, sizeof(hdr1) - 1);
        ::write(fd, name, std::strlen(name));
        ::write(fd, " ====\n", 6);
        void *frames[64];
        int n = backtrace(frames, 64);
        backtrace_symbols_fd(frames, n, fd);
        ::close(fd);
    }
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}
#endif

} // namespace

QString currentLogPath() { return g_logPath; }
QString logDirectory()   { return g_logDir; }

QString initialize(const QString &appVersion)
{
    // Under AppImage, applicationDirPath is a read-only squashfs mount.
    // Route the log to AppDataLocation/logs/ instead.
    if (!qEnvironmentVariableIsEmpty("APPIMAGE")) {
        g_logDir = QStandardPaths::writableLocation(
                       QStandardPaths::AppDataLocation) + "/logs";
    } else {
        g_logDir = QDir(QCoreApplication::applicationDirPath()).filePath("logs");
    }
    QDir().mkpath(g_logDir);
    g_logPath = QDir(g_logDir).filePath("log.txt");

    QFileInfo fi(g_logPath);
    if (fi.exists() && fi.size() > kLogMaxBytes)
        rotateLogs(g_logPath);

    qInstallMessageHandler(qtMessageHandler);

    writeLogLine(QString("==== session start (v%1, pid %2) ====")
                     .arg(appVersion)
                     .arg(QCoreApplication::applicationPid()));

#ifdef Q_OS_UNIX
    for (int s : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS})
        ::signal(s, crashSignalHandler);
#endif

    return g_logPath;
}

} // namespace logging
