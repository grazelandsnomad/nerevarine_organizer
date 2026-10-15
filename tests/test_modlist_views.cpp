// test_modlist_views - the typed views of the modlist, widget-free: the
// ModlistModel, the row handle and the view over it, the dependency snapshot
// built from rows, and the row orders behind Sort by Date / Size. The
// widget appears only where the bridge is the thing under test.

#include "deps_snapshot.h"
#include "mod_row.h"
#include "modentry.h"
#include "modlist_model.h"
#include "modlistview.h"
#include "modrow.h"
#include "modlist_sort.h"
#include "modroles.h"

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QPersistentModelIndex>
#include <QSet>
#include <QColor>
#include <QMetaType>
#include <QUuid>
#include <QVariant>
#include <QDateTime>
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

ModEntry mkMod(const QString &name)
{
    ModEntry e;
    e.displayName = name;
    e.checked = true;
    return e;
}

ModEntry mkSep(const QString &name)
{
    ModEntry e;
    e.itemType = QStringLiteral("separator");
    e.displayName = name;
    return e;
}

void testModelIsAWellBehavedItemModel()
{
    std::cout << "testModelIsAWellBehavedItemModel\n";
    ModlistModel m;
    // Qt's own conformance checker; a protocol slip (a begin without its end,
    // a bad index) aborts the test.
    QAbstractItemModelTester tester(&m, QAbstractItemModelTester::FailureReportingMode::Fatal);

    m.append(mkMod(QStringLiteral("A")));
    m.append(mkMod(QStringLiteral("B")));
    m.append(mkSep(QStringLiteral("S")));
    m.append(mkMod(QStringLiteral("C")));
    check("ids are minted on insertion", !m.rowId(0).isNull() && m.rowId(0) != m.rowId(1));
    const QUuid idA = m.rowId(0), idC = m.rowId(3);
    check("a row is found by its id", m.findByRowId(idC) == 3 && m.findByRowId(QUuid()) == -1
                                       && m.indexOfRowId(idC).row() == 3);

    // moveRows as the view's drag calls it: A (row 0) to past the end.
    check("moveRows past the end", m.moveRows(QModelIndex(), 0, 1, QModelIndex(), 4));
    check("A is last now", m.at(3).displayName == QStringLiteral("A") && m.findByRowId(idA) == 3);
    check("a move onto itself is refused",
          !m.moveRows(QModelIndex(), 1, 1, QModelIndex(), 1)
          && !m.moveRows(QModelIndex(), 1, 1, QModelIndex(), 2));
    m.move(3, 0);   // QList semantics: A back to row 0 -> A B S C
    check("move(from, to) lands the row at to",
          m.at(0).displayName == QStringLiteral("A") && m.at(3).displayName == QStringLiteral("C"));

    QPersistentModelIndex pc = m.index(3);   // C
    m.reorder({3, 2, 1, 0});                 // C S B A
    check("reorder applies the permutation",
          m.at(0).displayName == QStringLiteral("C") && m.at(3).displayName == QStringLiteral("A"));
    check("a persistent index follows its row", pc.row() == 0);
    m.reorder({0, 0, 1, 2});
    check("a non-permutation is ignored", m.at(0).displayName == QStringLiteral("C")
                                          && m.at(3).displayName == QStringLiteral("A"));

    const QUuid idS = m.rowId(1);
    const ModEntry s = m.takeAt(1);
    check("takeAt returns the entry with its id",
          s.rowId == idS && m.count() == 3 && m.findByRowId(idS) == -1);
    m.insertAt(1, s);
    check("and re-inserting keeps it", m.findByRowId(idS) == 1);

    const QModelIndex iA = m.index(3);
    check("data goes through the role table",
          m.data(iA, Qt::DisplayRole).toString() == QStringLiteral("A"));
    check("setData too", m.setData(iA, QStringLiteral("note"), ModRole::Annotation)
                         && m.at(3).annotation == QStringLiteral("note"));
    check("an unknown role is refused", !m.setData(iA, 1, Qt::UserRole + 999));

    check("a separator is not checkable", !(m.flags(m.index(1)) & Qt::ItemIsUserCheckable));
    check("a mod is", bool(m.flags(iA) & Qt::ItemIsUserCheckable));
    check("the root takes drops", bool(m.flags(QModelIndex()) & Qt::ItemIsDropEnabled));
    check("a row does not", !(m.flags(iA) & Qt::ItemIsDropEnabled));
    check("the mime fallback is refused",
          !m.dropMimeData(nullptr, Qt::MoveAction, 0, 0, QModelIndex()));

    ModEntry a2 = m.at(3);
    a2.rowId = QUuid();
    a2.displayName = QStringLiteral("A2");
    m.update(3, a2);
    check("update with a null id keeps the row's id",
          m.rowId(3) == idA && m.at(3).displayName == QStringLiteral("A2"));

    m.replace(m.all());
    check("replace keeps the ids it is given", m.findByRowId(idA) == 3 && m.count() == 4);

    int hiddenRow = -1;
    bool hiddenVal = false;
    QObject::connect(&m, &ModlistModel::rowHiddenChanged, &m,
                     [&](int r, bool h) { hiddenRow = r; hiddenVal = h; });
    m.setHidden(2, true);
    check("hidden has its own notice", hiddenRow == 2 && hiddenVal && m.at(2).hidden);
    m.clear();
    check("clear empties the model", m.count() == 0 && m.findByRowId(idA) == -1);
}

void testRowHandlesAndTheView()
{
    std::cout << "testRowHandlesAndTheView\n";
    ModlistModel m;
    ModListView view;
    view.setModel(&m);
    view.setSelectionMode(QAbstractItemView::ExtendedSelection);

    ModRow none;
    check("a default handle is null", !none && none == nullptr);
    ModRow a = view.addItem(mkMod(QStringLiteral("A")));
    ModRow b = view.addItem(mkMod(QStringLiteral("B")));
    ModRow s = view.insertItem(1, mkSep(QStringLiteral("S")));   // A S B
    check("the view counts the model's rows", view.count() == 3 && m.count() == 3);
    check("a handle knows its row", a.row() == 0 && s.row() == 1 && b.row() == 2
                                    && view.row(b) == 2 && view.item(2) == b);
    check("reads go through the role table",
          a->text() == QStringLiteral("A") && a->checkState() == Qt::Checked
          && s->checkState() == Qt::Unchecked && s->isSeparator() && a->isMod());

    int changed = 0;
    ModRow changedRow;
    QObject::connect(&view, &ModListView::itemChanged, &view,
                     [&](const ModRow &r) { ++changed; changedRow = r; });
    a->setData(ModRole::Annotation, QStringLiteral("note"));
    check("a write lands in the model and the view says which row",
          m.at(0).annotation == QStringLiteral("note") && changed == 1 && changedRow == a);
    a->setCheckState(Qt::Unchecked);
    check("so does a check state", !m.at(0).checked && a->checkState() == Qt::Unchecked);
    a->setToolTip(QStringLiteral("tip"));
    a->setFlags(Qt::ItemIsEnabled);
    check("tool tip and flags too", a->toolTip() == QStringLiteral("tip")
                                    && a->flags() == Qt::ItemIsEnabled
                                    && m.index(0).data(Qt::ToolTipRole).toString() == QStringLiteral("tip"));

    a->setHidden(true);
    check("hidden reaches the view", view.isRowHidden(0) && a->isHidden());
    m.replace(m.all());
    check("and survives a reset", view.isRowHidden(0));

    a->setFlags(ModEntry::kDefaultItemFlags);   // selectable again
    ModRow current, previous;
    QObject::connect(&view, &ModListView::currentItemChanged, &view,
                     [&](const ModRow &c, const ModRow &p) { current = c; previous = p; });
    view.setCurrentItem(b);
    check("the current row is a handle", view.currentItem() == b && view.currentRow() == 2
                                         && current == b && previous == nullptr);
    view.setCurrentItem(a);
    // QListWidget's SelectCurrent: the selection made by the previous
    // setCurrentItem is replaced, not added to.
    check("moving the current row moves its selection, like QListWidget",
          view.selectedItems().size() == 1 && view.selectedItems().first() == a
          && previous == b,
          QStringLiteral("n=%1 firstIsA=%2 prevIsB=%3")
              .arg(view.selectedItems().size())
              .arg(!view.selectedItems().isEmpty() && view.selectedItems().first() == a)
              .arg(previous == b));
    view.clearSelection();
    view.setRowSelected(s, true);
    check("selecting by handle", view.selectedItems().size() == 1 && view.selectedItems().first() == s);

    const ModEntry taken = view.takeItem(0);
    check("a taken row keeps its id and the handle goes blank",
          taken.rowId == a.id() && !a && a == nullptr && a->text().isEmpty()
          && m.count() == 2);
    a->setData(ModRole::Annotation, QStringLiteral("lost"));
    check("a write to a gone row is dropped", m.count() == 2 && taken.annotation == QStringLiteral("note"));
    ModRow back = view.insertItem(0, taken);
    check("re-inserting revives the same identity", back == a && a && a.row() == 0);

    QSet<ModRow> set{a, b, a};
    check("handles hash by id", set.size() == 2 && set.contains(view.item(0)));
    check("rowById resolves a live id and refuses a dead one",
          view.rowById(b.id()) == b && view.rowById(QUuid::createUuid()) == nullptr);

    // A block move: rows named in any order land together, in their own
    // order, at a position counted in the list without them.
    view.addItem(mkMod(QStringLiteral("C")));
    view.addItem(mkMod(QStringLiteral("D")));          // A S B C D
    const QList<ModRow> moved = view.moveRowsTo({3, 0}, 2);
    check("moveRowsTo lands the block, in order, where asked",
          moved.size() == 2 && moved.at(0) == a
          && m.at(0).displayName == QStringLiteral("S")
          && m.at(1).displayName == QStringLiteral("B")
          && m.at(2).displayName == QStringLiteral("A")
          && m.at(3).displayName == QStringLiteral("C")
          && m.at(4).displayName == QStringLiteral("D"));
    view.moveRowsTo({2, 3}, 0);                        // A C S B D
    check("and to the front", m.at(0).displayName == QStringLiteral("A")
                              && m.at(1).displayName == QStringLiteral("C")
                              && m.at(2).displayName == QStringLiteral("S"));
    view.moveRowsTo({0}, 99);                          // C S B D A
    check("past the end is the end", m.at(4).displayName == QStringLiteral("A"));
    int before = 0, after = 0;
    QObject::connect(&view, &ModListView::aboutToReorderRows, &view, [&] { ++before; });
    QObject::connect(&view, &ModListView::rowsReordered, &view, [&] { ++after; });
    m.moveRows(QModelIndex(), 0, 1, QModelIndex(), 3);
    check("a model move is reported as one reorder", before == 1 && after == 1,
          QStringLiteral("%1/%2").arg(before).arg(after));
    m.reorder({0, 1, 2, 3, 4});
    check("and a no-op reorder is not", before == 1 && after == 1);
    view.moveRowsTo({0}, 0);
    check("nor a block move that changes nothing", before == 1 && after == 1);
    m.moveRows(QModelIndex(), 2, 1, QModelIndex(), 0);  // back to C S B D A
    view.removeRow(4);                                 // A goes
    view.removeRow(3);                                 // D goes
    view.removeRow(1);                                 // S goes
    view.removeRow(0);                                 // C goes; B is left
    check("removeRow drops the row", m.count() == 1 && !a
                                     && m.at(0).displayName == QStringLiteral("B"));
    view.clear();
    check("clear empties everything", view.count() == 0 && !b);
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
    testModelIsAWellBehavedItemModel();
    testRowHandlesAndTheView();
    testDependencySnapshotKeepsRowIndicesAndSections();
    testDateOrderSendsSeparatorsAndDatelessToTheEnd();
    testSizeOrderKeepsSeparatorsAsDividers();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
