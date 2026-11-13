// The crash handler (logging::installCrashHandler): a forked child installs
// it on a temp log and takes a real SIGSEGV; the parent reads the block it
// left. Plus the build identity the session line and every block carry.

#include "logging.h"

#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <csignal>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

#include "test_harness.h"

namespace {

// A real fault, at a known address, that the compiler cannot prove away.
[[noreturn]] void faultAt(quintptr address)
{
    // Through a volatile, so the optimiser cannot see the constant address
    // (GCC's -Warray-bounds flags a store it can prove lands at 0x10).
    volatile quintptr where = address;
    *reinterpret_cast<volatile int *>(where) = 1;
    _exit(0);   // not reached
}

void testBuildIdentity()
{
    std::cout << "\n[the binary's identity]\n";
    const QString id = logging::buildId();
    check("the build id is hex", QRegularExpression(QStringLiteral("^[0-9a-f]{16,}$"))
                                    .match(id).hasMatch(), id);
    const QString line = logging::buildIdentity(QStringLiteral("9.99"));
    check("the identity line names version, commit, build id and base",
          line.startsWith(QStringLiteral("v9.99 ")) && line.contains(QStringLiteral(" build-id ") + id)
          && line.contains(QStringLiteral(" base 0x")), line);
}

void testCrashBlock()
{
    std::cout << "\n[a crash leaves a block]\n";
    QTemporaryDir tmp;
    const QString log = tmp.filePath(QStringLiteral("log.txt"));
    const pid_t pid = fork();
    if (pid == 0) {
        logging::installCrashHandler(log, QStringLiteral("test-identity"));
        faultAt(0x10);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    check("the child ends with the original signal",
          WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
          QString::number(status));

    QFile f(log);
    const QString text = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    check("one CRASH line naming the signal, the fault address and the thread",
          QRegularExpression(QStringLiteral(
              "==== CRASH: SIGSEGV at 0x10 thread \\d+ time \\d+ ====")).match(text).hasMatch()
          && text.count(QStringLiteral("==== CRASH")) == 1, text.left(300));
    check("then the binary's identity", text.contains(QStringLiteral("binary: test-identity\n")));
    check("then the frames", text.contains(QStringLiteral("test_crash_report(")) ,
          text.right(400));
}

void testSentSignal()
{
    std::cout << "\n[a signal sent, not a fault]\n";
    QTemporaryDir tmp;
    const QString log = tmp.filePath(QStringLiteral("log.txt"));
    const pid_t pid = fork();
    if (pid == 0) {
        logging::installCrashHandler(log, QStringLiteral("test-identity"));
        ::kill(getpid(), SIGSEGV);
        _exit(0);   // not reached
    }
    int status = 0;
    waitpid(pid, &status, 0);
    QFile f(log);
    const QString text = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    check("names the sender instead of an address that means nothing",
          text.contains(QStringLiteral("==== CRASH: SIGSEGV from pid %1 thread ").arg(pid))
          && WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV, text.left(200));
}

} // namespace

int main()
{
    std::cout << "crash report\n";
    testBuildIdentity();
    testCrashBlock();
    testSentSignal();
    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
