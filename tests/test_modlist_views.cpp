// test_modlist_views - the typed views of the modlist, widget-free: the
// ModlistModel bridge that follows the QListWidget, the dependency snapshot
// built from rows, and the row orders behind Sort by Date / Size. The
// widget appears only where the bridge is the thing under test.

#include "deps_snapshot.h"
#include "mod_row.h"
#include "modentry.h"
#include "modlist_model.h"
#include "modlist_model_widget_bridge.h"
#include "modlist_sort.h"
#include "modroles.h"

#include <QApplication>
#include <QColor>
#include <QMetaType>
#include <QUuid>
#include <QVariant>
#include <QDateTime>
#include <QListWidget>
#include <QListWidgetItem>
#include <QSignalBlocker>
#include <QStringList>

#include <iostream>

#include "test_harness.h"

namespace {

ModEntry mod(const QString &name, bool enabled = true,
             InstallStatus status = InstallStatus::Installed)
{
    ModEntry e;
    e.displayName   = name;
    e.checked       = enabled;
    e.installStatus = int(status);
    return e;
}

ModEntry sep(const QString &name)
{
    ModEntry e;
    e.itemType    = QStringLiteral("separator");
    e.displayName = name;
    return e;
}

QListWidgetItem *addRow(QListWidget &list, const QString &name)
{
    auto *it = new QListWidgetItem(name, &list);
    it->setData(ModRole::ItemType, ItemType::Mod);
    it->setCheckState(Qt::Checked);
    return it;
}

// -- the model bridge ------------------------------------------------------

// A value of the field's own type for a role, judged from what a blank entry
// reads back; the view sort's SortAnchor reads as nothing on a blank row.
QVariant sampleFor(int role, const QVariant &blankRead)
{
    if (role == Qt::CheckStateRole) return int(Qt::Checked);
    switch (blankRead.typeId()) {
    case QMetaType::QString:     return QStringLiteral("sample");
    case QMetaType::Int:         return 7;
    case QMetaType::Bool:        return true;
    case QMetaType::LongLong:    return QVariant::fromValue<qint64>(12345678901LL);
    case QMetaType::QStringList: return QStringList{QStringLiteral("a"), QStringLiteral("b")};
    case QMetaType::QDateTime:   return QDateTime(QDate(2024, 5, 6), QTime(7, 8, 9));
    case QMetaType::QUuid:       return QUuid::createUuid();
    case QMetaType::QColor:      return QColor(1, 2, 3);
    default:
        return role == ModRole::SortAnchor ? QVariant::fromValue<qint64>(5) : QVariant();
    }
}

void testRoleTableRoundTrips()
{
    std::cout << "testRoleTableRoundTrips\n";
    const ModEntry blank;
    check("the table knows the whole row", ModEntry::roles().size() >= 65,
          QString::number(ModEntry::roles().size()));

    int wrong = 0;
    QStringList wrongRoles;
    for (int role : ModEntry::roles()) {
        ModEntry e;
        const QVariant sample = sampleFor(role, blank.roleData(role));
        const bool took = e.setRoleData(role, sample);
        const bool back = took && e.roleData(role) == sample;
        e.setRoleData(role, QVariant());
        const bool reset = (e == blank);
        if (!back || !reset) { ++wrong; wrongRoles << QString::number(role - Qt::UserRole); }
    }
    check("every role takes its value, gives it back and clears to the default",
          wrong == 0, QStringLiteral("UserRole+") + wrongRoles.join(QStringLiteral(",")));

    ModEntry m;
    m.displayName = QStringLiteral("Mod");
    m.checked = true;
    check("the display text is the display role",
          m.roleData(Qt::DisplayRole).toString() == QStringLiteral("Mod")
          && m.roleData(Qt::EditRole).toString() == QStringLiteral("Mod"));
    check("a mod's check state is Checked", m.roleData(Qt::CheckStateRole).toInt() == Qt::Checked);
    m.setRoleData(Qt::CheckStateRole, Qt::Unchecked);
    check("and unchecking lands in the field", !m.checked);

    ModEntry sep;
    sep.itemType = QStringLiteral("separator");
    sep.checked  = true;
    check("a separator has no check state at all", !sep.roleData(Qt::CheckStateRole).isValid());

    ModEntry a;
    check("an unstamped row has no sort anchor", !a.roleData(ModRole::SortAnchor).isValid());
    a.setRoleData(ModRole::SortAnchor, QVariant::fromValue<qint64>(0));
    check("a stamp of zero is a stamp", a.roleData(ModRole::SortAnchor).isValid()
                                        && a.sortAnchor == 0);

    const int unknown = Qt::UserRole + 999;
    check("an unknown role is refused", !a.setRoleData(unknown, 1)
                                        && !a.roleData(unknown).isValid());
}

void testBridgeFollowsTheWidgetRowByRow()
{
    std::cout << "testBridgeFollowsTheWidgetRowByRow\n";
    QListWidget list;
    ModlistModel model;
    modlist::connectAutoSync(&model, &list);

    auto *a = addRow(list, QStringLiteral("A"));
    auto *b = addRow(list, QStringLiteral("B"));
    check("rows added reach the model", model.count() == 2
                                        && model.at(1).displayName == QStringLiteral("B"));

    int changedRow = -1, changes = 0, resets = 0;
    QObject::connect(&model, &ModlistModel::rowChanged, &model,
                     [&](int row) { changedRow = row; ++changes; });
    QObject::connect(&model, &ModlistModel::modelReset, &model, [&] { ++resets; });

    b->setData(ModRole::Annotation, QStringLiteral("note"));
    check("a role write updates that row only", changes == 1 && changedRow == 1
          && resets == 0, QStringLiteral("changes=%1 row=%2 resets=%3")
                             .arg(changes).arg(changedRow).arg(resets));
    check("with the new value", model.at(1).annotation == QStringLiteral("note"));
    check("and the other row untouched", model.at(0).annotation.isEmpty());

    a->setCheckState(Qt::Unchecked);
    check("a checkbox toggle too", !model.at(0).checked && model.at(1).checked);

    // The window's own itemChanged slots read the model, and they are
    // connected after the bridge, so the row must already be updated when
    // they run - and updated once, not once per path the bridge listens on.
    bool seenInTime = false;
    QObject::connect(&list, &QListWidget::itemChanged, &model,
                     [&](QListWidgetItem *item) {
        seenInTime = model.at(list.row(item)).checked;
    });
    changes = 0;
    a->setCheckState(Qt::Checked);
    check("an itemChanged slot wired after the bridge sees the new value", seenInTime);
    check("and the model reports the change once", changes == 1,
          QString::number(changes));

    const QUuid id = mod_row::rowId(b);
    check("a minted row id is visible through the model",
          model.findByRowId(id) == 1 && model.findByRowId(QUuid()) == -1);

    delete list.takeItem(0);
    check("a removed row leaves the model", model.count() == 1
          && model.at(0).displayName == QStringLiteral("B")
          && model.findByRowId(id) == 0);

    // The window edits rows in bulk with the widget's signals blocked. The
    // bridge listens to the widget's item model, which the blocker does not
    // silence, so the model keeps up anyway.
    {
        const QSignalBlocker blocker(&list);
        b->setData(ModRole::Annotation, QStringLiteral("blocked edit"));
        auto *c = new QListWidgetItem(QStringLiteral("C"));
        c->setData(ModRole::ItemType, ItemType::Mod);
        list.insertItem(0, c);
        delete list.takeItem(1);   // B
    }
    check("an edit under a signal blocker still reaches the model",
          model.count() == 1 && model.at(0).displayName == QStringLiteral("C"),
          QStringLiteral("count=%1 first=%2").arg(model.count())
              .arg(model.count() ? model.at(0).displayName : QStringLiteral("-")));

    // A wiring that arrives late, or a drift, is repaired on the next change
    // rather than applied as a delta to the wrong shape.
    ModlistModel late;
    addRow(list, QStringLiteral("D"));
    modlist::connectAutoSync(&late, &list);
    check("a model connected late is empty until something changes", late.count() == 0);
    addRow(list, QStringLiteral("E"));
    check("and whole after the first change", late.count() == 3
          && late.at(2).displayName == QStringLiteral("E"), QString::number(late.count()));
}

// -- deps::snapshot from rows ---------------------------------------------

void testDependencySnapshotKeepsRowIndicesAndSections()
{
    std::cout << "testDependencySnapshotKeepsRowIndicesAndSections\n";
    ModEntry lib = mod(QStringLiteral("lib-folder"));
    lib.customName = QStringLiteral("  Big Library ");
    lib.nexusUrl   = QStringLiteral("https://www.nexusmods.com/morrowind/mods/1");
    lib.isUtility  = true;
    ModEntry patch = mod(QStringLiteral("Patch"), false, InstallStatus::NotInstalled);
    patch.dependsOn = {lib.nexusUrl};

    const auto snap = deps::snapshot({sep(QStringLiteral(" Core ")), lib,
                                      sep(QStringLiteral("Patches")), patch});
    check("one entry per row, separators as blanks", snap.size() == 4
          && snap[0].nexusUrl.isEmpty() && snap[0].dependsOn.isEmpty());
    check("idx is the row number", snap[1].idx == 1 && snap[3].idx == 3);
    check("the custom name wins, trimmed",
          snap[1].displayName == QStringLiteral("Big Library"), snap[1].displayName);
    check("the section is the separator above, trimmed",
          snap[1].section == QStringLiteral("Core")
          && snap[3].section == QStringLiteral("Patches"), snap[3].section);
    check("flags come through",
          snap[1].enabled && snap[1].installed && snap[1].isUtility
          && !snap[3].enabled && !snap[3].installed
          && snap[3].dependsOn == QStringList{lib.nexusUrl});
}

// -- sort orders -------------------------------------------------------------

void testDateOrderSendsSeparatorsAndDatelessToTheEnd()
{
    std::cout << "testDateOrderSendsSeparatorsAndDatelessToTheEnd\n";
    ModEntry old = mod(QStringLiteral("old"));
    old.dateAdded = QDateTime(QDate(2026, 1, 1), QTime(0, 0));
    ModEntry recent = mod(QStringLiteral("recent"));
    recent.dateAdded = QDateTime(QDate(2026, 6, 1), QTime(0, 0));
    ModEntry undated = mod(QStringLiteral("undated"));
    const QList<ModEntry> rows = {sep(QStringLiteral("S1")), recent, undated,
                                  sep(QStringLiteral("S2")), old};

    check("ascending: oldest first, dateless after the dated, separators last in order",
          modlist_sort::orderByDate(rows, true) == QList<int>{4, 1, 2, 0, 3});
    check("descending flips the dated ones only",
          modlist_sort::orderByDate(rows, false) == QList<int>{1, 4, 2, 0, 3});
}

void testSizeOrderKeepsSeparatorsAsDividers()
{
    std::cout << "testSizeOrderKeepsSeparatorsAsDividers\n";
    ModEntry small = mod(QStringLiteral("small"));  small.modSize = 10;
    ModEntry big   = mod(QStringLiteral("big"));    big.modSize   = 500;
    ModEntry mid   = mod(QStringLiteral("mid"));    mid.modSize   = 100;
    ModEntry none  = mod(QStringLiteral("none"));   // size unknown
    const QList<ModEntry> rows = {small, sep(QStringLiteral("S")), big, none, mid};

    check("largest first, unknown last, the separator where it was",
          modlist_sort::orderBySize(rows, false) == QList<int>{2, 1, 4, 0, 3});
    check("smallest first keeps the unknown last",
          modlist_sort::orderBySize(rows, true) == QList<int>{0, 1, 4, 2, 3});
    check("an empty list sorts to an empty order",
          modlist_sort::orderBySize({}, true).isEmpty()
          && modlist_sort::orderByDate({}, true).isEmpty());
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    testRoleTableRoundTrips();
    testBridgeFollowsTheWidgetRowByRow();
    testDependencySnapshotKeepsRowIndicesAndSections();
    testDateOrderSendsSeparatorsAndDatelessToTheEnd();
    testSizeOrderKeepsSeparatorsAsDividers();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
