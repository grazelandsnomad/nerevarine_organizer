// test_loadorder_controller - the scan workers' lifecycle.
//
// The app's most frequent crash, in the log at least eleven times since April:
// "[FATAL] QThread: Destroyed while thread '' is still running". A core dump
// pinned it: the UI thread running a deferred delete of a ConflictScanWorker
// while that worker's own thread was still inside run(), listing a mod folder.
//
// The cause was the gap between a worker's run() returning - isRunning() goes
// false - and its queued finished handler running on the UI thread. A scan
// requested in that gap deleted the old worker and started a new one, and the
// old handler, which read whichever worker the controller held, then took the
// NEW one's half-written results and deleteLater()'d it mid-run. These tests
// open that gap on purpose: without the fix they abort.
//
// Mod folders are built in a QTemporaryDir; no game, no plugins needed.

#include "loadordercontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>

#include "test_harness.h"

namespace {

void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write("x");
}

// A mod folder with `files` in it, plus `filler` more so its walk takes real
// time - long enough that a worker is certainly still inside run() when the
// UI thread next turns.
conflict_direction::Mod makeMod(const QTemporaryDir &t, const QString &name,
                                const QStringList &files, int filler = 0)
{
    const QString dir = t.filePath(name);
    for (const QString &f : files) touch(dir + QLatin1Char('/') + f);
    for (int i = 0; i < filler; ++i)
        touch(dir + QStringLiteral("/filler/%1/%2.dds").arg(i % 50).arg(i));
    return {dir, name};
}

bool pumpUntil(const std::function<bool()> &done, int ms = 20000)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

using Results = QHash<QString, conflict_direction::Directions>;

void testRequestInTheGap()
{
    std::cout << "\n[a scan requested after the last one returned, before it was collected]\n";
    QTemporaryDir t;
    const auto a  = makeMod(t, "A",  {"a.txt"});
    const auto b1 = makeMod(t, "B1", {"textures/shared.dds"}, 2000);
    const auto b2 = makeMod(t, "B2", {"textures/shared.dds"});

    LoadOrderController c;
    QList<Results> got;
    QObject::connect(&c, &LoadOrderController::conflictsScanned,
                     [&](const Results &r, const auto &) { got << r; });

    c.scanConflicts({a});
    // A's run() returns; its finished handler is queued but, with no event
    // loop turning, has not run. This is the gap.
    QThread::msleep(300);
    c.scanConflicts({b1, b2});

    check("both scans are delivered", pumpUntil([&] { return got.size() >= 2; }),
          QString::number(got.size()));
    check("the first is A's, which has nothing to conflict with",
          !got.isEmpty() && got[0].isEmpty());
    check("the second is B's, whole",
          got.size() >= 2 && got[1].contains(b1.path) && got[1].contains(b2.path));
    check("and nothing more", got.size() == 2, QString::number(got.size()));
}

void testRequestMidScanIsServed()
{
    std::cout << "\n[a scan requested while one is running is served, not dropped]\n";
    QTemporaryDir t;
    const auto slow = makeMod(t, "Slow", {"a.txt"}, 4000);
    const auto b1   = makeMod(t, "B1", {"textures/shared.dds"});
    const auto b2   = makeMod(t, "B2", {"textures/shared.dds"});

    LoadOrderController c;
    QList<Results> got;
    QObject::connect(&c, &LoadOrderController::conflictsScanned,
                     [&](const Results &r, const auto &) { got << r; });

    c.scanConflicts({slow});
    c.scanConflicts({b1, b2});   // the list changed while the first was scanning

    check("the newer list is scanned too", pumpUntil([&] { return got.size() >= 2; }),
          QString::number(got.size()));
    check("and it is the last word",
          !got.isEmpty() && got.last().contains(b1.path) && got.last().contains(b2.path));
}

void testTranslationRequestInTheGap()
{
    std::cout << "\n[translation scan: the same gap]\n";
    QTemporaryDir t;
    const auto a = makeMod(t, "A", {"readme.txt"});
    const auto b = makeMod(t, "B", {"readme.txt"}, 2000);

    LoadOrderController c;
    int delivered = 0;
    QObject::connect(&c, &LoadOrderController::translationsScanned,
                     [&](const auto &, int, const auto &) { ++delivered; });

    c.scanTranslations({a}, QStringLiteral("spanish"), {}, {});
    QThread::msleep(300);
    c.scanTranslations({b}, QStringLiteral("spanish"), {}, {});

    check("both scans are delivered", pumpUntil([&] { return delivered >= 2; }),
          QString::number(delivered));
    check("and nothing more", delivered == 2, QString::number(delivered));
}

void testShutdownDuringScan()
{
    std::cout << "\n[closing while a scan runs]\n";
    QTemporaryDir t;
    const auto slow = makeMod(t, "Slow", {"a.txt"}, 4000);
    auto *c = new LoadOrderController;
    c->scanConflicts({slow});
    QThread::msleep(2);
    delete c;   // must stop the worker, not destroy it running
    check("the controller goes away without taking the process with it", true);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "=== LoadOrderController scan lifecycle ===\n";
    testRequestInTheGap();
    testRequestMidScanIsServed();
    testTranslationRequestInTheGap();
    testShutdownDuringScan();
    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
