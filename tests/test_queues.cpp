// test_queues - the install queues and rows that vanish while a queue still
// holds a handle to them. The queues key their state by row id,
// and a row can be removed between the click and the tick; the pointer is
// then freed memory and must not be read. Under scripts/sanitize.sh a read
// through it is what fails, so these cases carry weight there.

#include "bulk_install_queue.h"
#include "downloadqueue.h"
#include "mod_row.h"
#include "modlist_model.h"
#include "modlistview.h"
#include "nexusclient.h"
#include "modentry.h"
#include "modroles.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QTimer>

#include <iostream>

#include "test_harness.h"

namespace {

ModRow addMod(ModlistModel &model, const QString &name)
{
    ModEntry e = ModEntry::named(name);
    e.installStatus = 0;
    e.nexusUrl = QStringLiteral("https://www.nexusmods.com/morrowind/mods/1");
    return ModRow(&model, model.rowId(model.append(e)));
}

void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void testBulkQueueSkipsARowDeletedBeforeItsTurn()
{
    std::cout << "testBulkQueueSkipsARowDeletedBeforeItsTurn\n";
    ModlistModel model;
    const ModRow a = addMod(model, QStringLiteral("A"));
    const ModRow b = addMod(model, QStringLiteral("B"));
    const ModRow c = addMod(model, QStringLiteral("C"));

    QStringList started;
    BulkInstallQueue queue(
        [&model](const QUuid &id) -> ModRow {
            return model.findByRowId(id) >= 0 ? ModRow(&model, id) : ModRow();
        },
        [&](const ModRow &it) { started << it->text(); });
    queue.enqueue({mod_row::rowId(a), mod_row::rowId(b), mod_row::rowId(c)});
    check("the first row starts at once", started == QStringList{QStringLiteral("A")},
          started.join(QLatin1Char(',')));

    // B leaves the list before its tick, and a new row takes its place: the
    // queue holds B's id, which nothing resolves any more - not even a row at
    // the same position.
    model.removeAt(b.row());
    const ModRow b2 = addMod(model, QStringLiteral("B2"));
    const ModEntry parked = model.takeAt(b2.row());
    model.insertAt(1, parked);

    spin(1500);   // past one 1.2 s tick
    check("the vanished row is skipped, its replacement untouched, the next starts",
          started == QStringList{QStringLiteral("A"), QStringLiteral("C")},
          started.join(QLatin1Char(',')));
}

// -- DownloadQueue: a download whose row vanished ------------------------

void testDownloadQueueOutlivesItsRow()
{
    std::cout << "testDownloadQueueOutlivesItsRow\n";
    QMainWindow window;
    ModlistModel model;
    auto *list = new ModListView(&window);
    list->setModel(&model);
    window.setCentralWidget(list);
    QNetworkAccessManager net;
    NexusClient nexus;
    DownloadQueue queue(list, &net, &nexus, &window);
    (void)queue.setup(&window);
    queue.setModsDir(QDir::tempPath());

    const ModRow a = addMod(model, QStringLiteral("A"));
    const QUuid row = mod_row::rowId(a);
    // Nothing listens on port 1; the transfer fails at once, and its finished
    // handler runs after the row is gone.
    queue.enqueueDownload(row, QUrl(QStringLiteral("http://127.0.0.1:1/never.7z")),
                          QStringLiteral("never.7z"));
    check("the row is queued under its id", queue.isQueued(row));
    list->removeRow(a.row());
    check("and the handle knows the row is gone", !a);

    spin(1500);
    check("the failed transfer left the queue", !queue.isQueued(row)
                                                 && !queue.isDownloadActive(row));
    queue.cancelQueued(row);
    check("and a cancel for the vanished row is a no-op", !queue.isQueued(row));
}

// -- row identity (mod_row::rowId) --------------------------------------

void testRowIdIsMintedOnceAndTravelsThroughSnapshots()
{
    std::cout << "testRowIdIsMintedOnceAndTravelsThroughSnapshots\n";
    ModlistModel model;
    ModEntry fresh = ModEntry::named(QStringLiteral("Fresh"));
    check("an entry has no id until the model takes it", fresh.rowId.isNull());
    const ModRow row = ModRow(&model, model.rowId(model.append(fresh)));
    const QUuid id = mod_row::rowId(row);
    check("the model mints one on insertion", !id.isNull());
    check("asking again returns the same one", mod_row::rowId(row) == id
                                               && mod_row::rowIdIfAny(row) == id);
    check("a null row has none", mod_row::rowId(nullptr).isNull()
                                 && mod_row::rowIdIfAny(nullptr).isNull());

    // An undo puts snapshots back as a whole; the id goes along.
    const QList<ModEntry> snap = model.all();
    check("the snapshot carries the id", snap.first().rowId == id);
    model.replace(snap);
    check("the restored row has the same id", model.findByRowId(id) == 0 && row);

    // A whole-row update without an id leaves the row's alone.
    ModEntry blank = ModEntry::named(QStringLiteral("Blank"));
    model.update(0, blank);
    check("a null id in an update does not erase the row's",
          model.rowId(0) == id && model.at(0).displayName == QStringLiteral("Blank"));
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    testBulkQueueSkipsARowDeletedBeforeItsTurn();
    testRowIdIsMintedOnceAndTravelsThroughSnapshots();
    testDownloadQueueOutlivesItsRow();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
