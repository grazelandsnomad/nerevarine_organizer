// test_queues - the install queues and rows that vanish while a queue still
// holds a pointer to them. The queues key their state by QListWidgetItem*,
// and a row can be removed between the click and the tick; the pointer is
// then freed memory and must not be read. Under scripts/sanitize.sh a read
// through it is what fails, so these cases carry weight there.

#include "bulk_install_queue.h"
#include "downloadqueue.h"
#include "mod_row.h"
#include "nexusclient.h"
#include "modentry.h"
#include "modroles.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QTimer>

#include <iostream>

#include "test_harness.h"

namespace {

QListWidgetItem *addMod(QListWidget &list, const QString &name)
{
    auto *it = new QListWidgetItem(name, &list);
    it->setData(ModRole::ItemType, ItemType::Mod);
    it->setData(ModRole::InstallStatus, 0);
    it->setData(ModRole::NexusUrl,
                QStringLiteral("https://www.nexusmods.com/morrowind/mods/1"));
    return it;
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
    QListWidget list;
    auto *a = addMod(list, QStringLiteral("A"));
    auto *b = addMod(list, QStringLiteral("B"));
    auto *c = addMod(list, QStringLiteral("C"));

    QStringList started;
    BulkInstallQueue queue(&list, [&](QListWidgetItem *it) { started << it->text(); });
    queue.enqueue({a, b, c});
    check("the first row starts at once", started == QStringList{QStringLiteral("A")},
          started.join(QLatin1Char(',')));

    // B leaves the list before its tick. The queue still holds its address;
    // it must skip it without reading it.
    delete list.takeItem(list.row(b));

    spin(1500);   // past one 1.2 s tick
    check("the deleted row is skipped and the next one starts",
          started == QStringList{QStringLiteral("A"), QStringLiteral("C")},
          started.join(QLatin1Char(',')));
}

// -- DownloadQueue: a download whose row vanished ------------------------

void testDownloadQueueOutlivesItsRow()
{
    std::cout << "testDownloadQueueOutlivesItsRow\n";
    QMainWindow window;
    auto *list = new QListWidget(&window);
    window.setCentralWidget(list);
    QNetworkAccessManager net;
    NexusClient nexus;
    DownloadQueue queue(list, &net, &nexus, &window);
    (void)queue.setup(&window);
    queue.setModsDir(QDir::tempPath());

    auto *a = addMod(*list, QStringLiteral("A"));
    const QUuid row = mod_row::rowId(a);
    // Nothing listens on port 1; the transfer fails at once, and its finished
    // handler runs after the row is gone.
    queue.enqueueDownload(row, QUrl(QStringLiteral("http://127.0.0.1:1/never.7z")),
                          QStringLiteral("never.7z"));
    check("the row is queued under its id", queue.isQueued(row));
    delete list->takeItem(list->row(a));

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
    QListWidgetItem item;
    check("a fresh row has no id until asked", mod_row::rowIdIfAny(&item).isNull());
    const QUuid id = mod_row::rowId(&item);
    check("asking mints one", !id.isNull());
    check("asking again returns the same one", mod_row::rowId(&item) == id);
    check("it is now the row's id", mod_row::rowIdIfAny(&item) == id);
    check("a null item has none", mod_row::rowId(nullptr).isNull()
                                  && mod_row::rowIdIfAny(nullptr).isNull());

    // An undo rebuilds every item from ModEntry snapshots; the id goes along.
    item.setData(ModRole::ItemType, ItemType::Mod);
    const ModEntry snap = ModEntry::fromItem(&item);
    check("the snapshot carries the id", snap.rowId == id);
    QListWidgetItem rebuilt;
    snap.applyToItem(&rebuilt);
    check("the rebuilt row has the same id", mod_row::rowIdIfAny(&rebuilt) == id);

    // A snapshot without one leaves a row's existing id alone.
    ModEntry blank;
    blank.itemType = QStringLiteral("mod");
    blank.applyToItem(&rebuilt);
    check("a null id in a snapshot does not erase the row's",
          mod_row::rowIdIfAny(&rebuilt) == id);
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
