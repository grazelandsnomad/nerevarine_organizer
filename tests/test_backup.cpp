// BackupManager::writePreSortCheckpoint - the auto pre-sort good-state
// checkpoint behind MainWindow::checkpointBeforeSort().
//
// Verifies it (a) writes the given order as a good-state checkpoint, (b)
// dedupes when the order is byte-identical to the newest checkpoint (so the
// view-only Size/Date sorts can't pile up identical checkpoints), and (c)
// makes a fresh one once the order changes. The dedupe/change cases seed an
// old-stamped checkpoint so they test content dedupe independent of the
// same-second collision guard. Driven off a QTemporaryDir.

#include "backup_manager.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok, const QString &detail = {})
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name << "\n";
        if (!detail.isEmpty())
            std::cout << "    " << detail.toStdString() << "\n";
        ++s_failed;
    }
}

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(bytes);
    f.close();
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

static int countGood(const QString &live)
{
    QFileInfo fi(live);
    return QDir(fi.absolutePath())
             .entryList({fi.fileName() + ".good.*"}, QDir::Files)
             .size();
}

// Newest (lexically-last == latest stamp) .good snapshot's contents.
static QByteArray newestGood(const QString &live)
{
    QFileInfo fi(live);
    const QFileInfoList l = QDir(fi.absolutePath()).entryInfoList(
        {fi.fileName() + ".good.*"}, QDir::Files, QDir::Name | QDir::Reversed);
    return l.isEmpty() ? QByteArray() : readFile(l.first().absoluteFilePath());
}

static void testCreatesAndNoPileUp()
{
    std::cout << "testCreatesAndNoPileUp\n";
    QTemporaryDir dir;
    const QString live = dir.filePath("modlist_morrowind.txt");

    BackupManager bm([&]{ return live; }, []{});   // saveBeforeMark unused here

    check("no good states initially", countGood(live) == 0);

    // First sort captures the pre-sort order.
    bm.writePreSortCheckpoint("orderA\n");
    check("first sort creates exactly one checkpoint", countGood(live) == 1,
          QString("count=%1").arg(countGood(live)));
    check("checkpoint holds the pre-sort order", newestGood(live) == "orderA\n");

    // Sorting again without changing the order must not pile up checkpoints.
    bm.writePreSortCheckpoint("orderA\n");
    bm.writePreSortCheckpoint("orderA\n");
    check("repeated sorts of the same order stay at one checkpoint",
          countGood(live) == 1, QString("count=%1").arg(countGood(live)));
}

static void testContentDedupeAndChange()
{
    std::cout << "testContentDedupeAndChange\n";
    QTemporaryDir dir;
    const QString live = dir.filePath("modlist_morrowind.txt");

    // Seed an OLD-stamped checkpoint, so a fresh now-stamped snapshot would be
    // lexically newer and thus detectable. Isolates content dedupe from the
    // same-second collision guard.
    writeFile(live + ".good.20200101-000000", "orderX\n");
    check("seeded one old checkpoint", countGood(live) == 1);

    BackupManager bm([&]{ return live; }, []{});

    // Order unchanged vs the newest checkpoint -> no new snapshot.
    bm.writePreSortCheckpoint("orderX\n");
    check("unchanged order is deduped (still one checkpoint)",
          countGood(live) == 1, QString("count=%1").arg(countGood(live)));
    check("no fresh-stamped snapshot was written",
          QDir(QFileInfo(live).absolutePath())
              .entryList({QFileInfo(live).fileName() + ".good.2026*"}, QDir::Files)
              .isEmpty());

    // Changed order -> a fresh checkpoint holding the new order.
    bm.writePreSortCheckpoint("orderY\n");
    check("changed order creates a second checkpoint", countGood(live) == 2,
          QString("count=%1").arg(countGood(live)));
    check("newest checkpoint holds the changed order", newestGood(live) == "orderY\n");
}

static void testGuardsMissingDir()
{
    std::cout << "testGuardsMissingDir\n";
    QTemporaryDir dir;
    const QString live = dir.filePath("nope/modlist_morrowind.txt");   // subdir absent
    BackupManager bm([&]{ return live; }, []{});
    bm.writePreSortCheckpoint("data\n");                               // must not crash
    check("no checkpoint written into a non-existent directory", countGood(live) == 0);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "== test_backup ==\n";
    testCreatesAndNoPileUp();
    testContentDedupeAndChange();
    testGuardsMissingDir();
    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
