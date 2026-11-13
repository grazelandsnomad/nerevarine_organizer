#include "logging.h"

#include "build_info.h"   // NR_GIT_SHA, generated per build

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

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef Q_OS_UNIX
#  include <ctime>
#  include <execinfo.h>
#  include <fcntl.h>
#  include <link.h>       // dl_iterate_phdr, ElfW, NT_GNU_BUILD_ID
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
    // Slide back: .3->.4, .2->.3, ... live->.1
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
    // fprintf, not std::println: avoiding <print> keeps us on GCC 13 (Ubuntu
    // 22.04) instead of GCC 14 (toolchain PPA kept timing out on CI runners).
    const QByteArray utf8 = line.toUtf8();
    std::fprintf(stderr, "%s\n", utf8.constData());
    if (type == QtFatalMsg) std::abort();
}

#ifdef Q_OS_UNIX
// write(2) in full, result looked at: glibc marks it warn_unused_result under
// _FORTIFY_SOURCE (Ubuntu's default), and a crash handler has nowhere to
// report a short write anyway. Async-signal-safe, like write itself.
void writeAll(int fd, const char *s, size_t n)
{
    while (n > 0) {
        const ssize_t w = ::write(fd, s, n);
        if (w <= 0) return;
        s += size_t(w);
        n -= size_t(w);
    }
}

// Everything the handler writes is prepared here, at install time: inside a
// signal handler only write(2)-level calls are safe - no allocation, no Qt,
// not even snprintf.
char   g_crashPath[4096]  = {};
char   g_crashIdent[512]  = {};
size_t g_crashIdentLen    = 0;
// The main thread's alternate stack, so a stack overflow there still gets a
// block. Per thread by nature: a worker's overflow dies without one.
char   g_altStack[64 * 1024];

// A crash-block line under construction, in a fixed buffer.
struct LineBuf {
    char   buf[192];
    size_t n = 0;
    void add(const char *s)
    {
        while (*s && n < sizeof(buf)) buf[n++] = *s++;
    }
    void addNumber(unsigned long long v, unsigned base)
    {
        char tmp[24];
        size_t k = 0;
        do { tmp[k++] = "0123456789abcdef"[v % base]; v /= base; } while (v && k < sizeof(tmp));
        while (k > 0 && n < sizeof(buf)) buf[n++] = tmp[--k];
    }
};

void crashSignalHandler(int sig, siginfo_t *info, void *)
{
    const char *name = "SIGNAL";
    bool hasAddr = true;
    switch (sig) {
        case SIGSEGV: name = "SIGSEGV"; break;
        case SIGBUS:  name = "SIGBUS";  break;
        case SIGFPE:  name = "SIGFPE";  break;
        case SIGILL:  name = "SIGILL";  break;
        case SIGABRT: name = "SIGABRT"; hasAddr = false; break;
    }
    int fd = g_crashPath[0]
        ? ::open(g_crashPath, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644) : -1;
    const int out = fd >= 0 ? fd : STDERR_FILENO;

    // "==== CRASH: SIGSEGV at 0x10 thread 1234 time 1789000000 ===="
    LineBuf line;
    line.add("\n==== CRASH: ");
    line.add(name);
    if (info && info->si_code <= 0) {
        // Sent, not a fault (kill, raise, abort): si_addr holds nothing,
        // and who sent it is the useful part.
        line.add(" from pid ");
        line.addNumber(static_cast<unsigned long long>(info->si_pid), 10);
    } else if (hasAddr && info) {
        line.add(" at 0x");
        line.addNumber(reinterpret_cast<unsigned long long>(info->si_addr), 16);
    }
    line.add(" thread ");
    line.addNumber(static_cast<unsigned long long>(::gettid()), 10);
    line.add(" time ");
    line.addNumber(static_cast<unsigned long long>(std::time(nullptr)), 10);
    line.add(" ====\n");
    writeAll(out, line.buf, line.n);
    writeAll(out, g_crashIdent, g_crashIdentLen);

    void *frames[64];
    const int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, out);
    if (fd >= 0) ::close(fd);
    // SA_RESETHAND put the default action back: this ends the process with
    // the original signal, and a fault inside the handler cannot loop.
    ::raise(sig);
}

// The running executable's build id and load base, from its program headers.
struct ExeInfo {
    QByteArray buildId;   // hex
    quintptr   base = 0;
};

int readExeInfo(dl_phdr_info *info, size_t, void *data)
{
    auto *exe = static_cast<ExeInfo *>(data);
    // The program itself comes first; one call is all this needs.
    exe->base = info->dlpi_addr;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) &ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_NOTE) continue;
        const size_t align = ph.p_align >= 8 ? 8 : 4;
        const auto pad = [align](size_t v) { return (v + align - 1) & ~(align - 1); };
        const char *p   = reinterpret_cast<const char *>(info->dlpi_addr + ph.p_vaddr);
        const char *end = p + ph.p_memsz;
        while (p + sizeof(ElfW(Nhdr)) <= end) {
            const auto *note = reinterpret_cast<const ElfW(Nhdr) *>(p);
            const char *noteName = p + sizeof(ElfW(Nhdr));
            const char *desc     = noteName + pad(note->n_namesz);
            const char *next     = desc + pad(note->n_descsz);
            if (next > end) break;
            if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4
                && std::memcmp(noteName, "GNU", 4) == 0) {
                exe->buildId = QByteArray(desc, int(note->n_descsz)).toHex();
                return 1;
            }
            p = next;
        }
    }
    return 1;
}

ExeInfo exeInfo()
{
    static const ExeInfo info = [] {
        ExeInfo e;
        dl_iterate_phdr(readExeInfo, &e);
        return e;
    }();
    return info;
}
#endif

} // namespace

QString currentLogPath() { return g_logPath; }
QString logDirectory()   { return g_logDir; }

QString buildId()
{
#ifdef Q_OS_UNIX
    return QString::fromLatin1(exeInfo().buildId);
#else
    return {};
#endif
}

QString buildIdentity(const QString &appVersion)
{
    QString s = QStringLiteral("v%1 %2").arg(appVersion, QStringLiteral(NR_GIT_SHA));
#ifdef Q_OS_UNIX
    const ExeInfo exe = exeInfo();
    s += QStringLiteral(" build-id %1 base 0x%2")
             .arg(exe.buildId.isEmpty() ? QStringLiteral("none")
                                        : QString::fromLatin1(exe.buildId))
             .arg(QString::number(quint64(exe.base), 16));
#endif
    return s;
}

void installCrashHandler(const QString &logPath, const QString &identity)
{
#ifdef Q_OS_UNIX
    const QByteArray path = logPath.toLocal8Bit();
    const size_t pathLen = std::min(size_t(path.size()), sizeof(g_crashPath) - 1);
    std::memcpy(g_crashPath, path.constData(), pathLen);
    g_crashPath[pathLen] = '\0';

    const QByteArray ident = ("binary: " + identity + '\n').toUtf8();
    g_crashIdentLen = std::min(size_t(ident.size()), sizeof(g_crashIdent));
    std::memcpy(g_crashIdent, ident.constData(), g_crashIdentLen);

    // backtrace() loads libgcc_s on its first call, which allocates; do that
    // now rather than inside a handler.
    void *warm[2];
    (void)backtrace(warm, 2);

    stack_t ss{};
    ss.ss_sp    = g_altStack;
    ss.ss_size  = sizeof(g_altStack);
    ss.ss_flags = 0;
    ::sigaltstack(&ss, nullptr);

    struct sigaction sa{};
    sa.sa_sigaction = crashSignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (int s : {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL})
        ::sigaction(s, &sa, nullptr);
#else
    Q_UNUSED(logPath);
    Q_UNUSED(identity);
#endif
}

QString initialize(const QString &appVersion)
{
    // AppImage: applicationDirPath is a read-only squashfs mount, so log to
    // AppDataLocation/logs/ instead.
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

    const QString identity = buildIdentity(appVersion);
    writeLogLine(QString("==== session start (%1, pid %2) ====")
                     .arg(identity)
                     .arg(QCoreApplication::applicationPid()));

    installCrashHandler(g_logPath, identity);

    return g_logPath;
}

} // namespace logging
