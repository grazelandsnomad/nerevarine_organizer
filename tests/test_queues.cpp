// test_queues - the install queues and rows that vanish while a queue still
// holds a pointer to them. The queues key their state by QListWidgetItem*,
// and a row can be removed between the click and the tick; the pointer is
// then freed memory and must not be read. Under scripts/sanitize.sh a read
// through it is what fails, so these cases carry weight there.

#include "bulk_install_queue.h"
#include "modroles.h"

#include <QApplication>
#include <QEventLoop>
#include <QListWidget>
#include <QListWidgetItem>
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

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    testBulkQueueSkipsARowDeletedBeforeItsTurn();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
