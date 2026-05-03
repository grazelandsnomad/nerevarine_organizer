// tests/test_modlist_model.cpp
//
// Locks in the ModlistModel CRUD API + signal contract.  This is the
// foundation of the QListWidget → model decoupling: as readers across
// MainWindow migrate to consume the model in subsequent stages, every
// mutation path here is the contract they rely on.
//
// QtCore-only on purpose - the whole point of ModlistModel is that
// modlist business logic becomes testable without QtWidgets, so the
// build doesn't link Qt6::Widgets here.

#include "modlist_model.h"
#include "modentry.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok, const QString &hint = {})
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name;
        if (!hint.isEmpty()) std::cout << " (" << hint.toStdString() << ")";
        std::cout << "\n";
        ++s_failed;
    }
}

static ModEntry mod(const QString &name, const QString &url = {},
                     const QString &path = {})
{
    ModEntry e;
    e.itemType    = QStringLiteral("mod");
    e.displayName = name;
    e.nexusUrl    = url;
    e.modPath     = path;
    return e;
}

// -- Scenarios ---

static void testEmptyModelStartsEmpty()
{
    std::cout << "\n[default-constructed model is empty]\n";
    ModlistModel m;
    check("count is 0", m.count() == 0);
    check("isEmpty true", m.isEmpty());
    check("at(0) returns sentinel ModEntry",
          m.at(0).displayName.isEmpty());
}

static void testAppendInsertEmitsRowsInserted()
{
    std::cout << "\n[append + insert emit rowsInserted]\n";
    ModlistModel m;
    QSignalSpy spy(&m, &ModlistModel::rowsInserted);

    const int idx0 = m.append(mod("A"));
    check("append returns 0",        idx0 == 0);
    check("count is 1 after append", m.count() == 1);
    check("first append fired one signal", spy.count() == 1);

    m.insertAt(0, mod("Z"));
    check("insertAt at front",         m.at(0).displayName == "Z");
    check("previous A pushed to row 1", m.at(1).displayName == "A");
    check("two signals total",          spy.count() == 2);
}

static void testRemoveAtEmitsRowsRemoved()
{
    std::cout << "\n[removeAt emits rowsRemoved + clamps invalid rows]\n";
    ModlistModel m;
    m.append(mod("A"));
    m.append(mod("B"));

    QSignalSpy spy(&m, &ModlistModel::rowsRemoved);
    m.removeAt(0);
    check("count drops to 1",       m.count() == 1);
    check("survivor is B",          m.at(0).displayName == "B");
    check("rowsRemoved fired once", spy.count() == 1);

    m.removeAt(99);   // out of range
    check("invalid row is no-op", m.count() == 1);
    check("no extra signal",      spy.count() == 1);
}

static void testMoveReorders()
{
    std::cout << "\n[move emits rowsMoved and reorders entries]\n";
    ModlistModel m;
    m.append(mod("A"));
    m.append(mod("B"));
    m.append(mod("C"));

    QSignalSpy spy(&m, &ModlistModel::rowsMoved);
    m.move(0, 2);
    check("A moved to row 2", m.at(2).displayName == "A");
    check("B at row 0",       m.at(0).displayName == "B");
    check("C at row 1",       m.at(1).displayName == "C");
    check("rowsMoved fired",  spy.count() == 1);
}

static void testUpdateEmitsRowChanged()
{
    std::cout << "\n[update emits rowChanged]\n";
    ModlistModel m;
    m.append(mod("A"));

    QSignalSpy spy(&m, &ModlistModel::rowChanged);

    m.update(0, mod("A renamed"));
    check("displayName updated",  m.at(0).displayName == "A renamed");
    check("rowChanged fired",     spy.count() == 1);

    m.update(99, mod("ignored"));
    check("invalid row is no-op", spy.count() == 1);
}

static void testReplaceEmitsModelReset()
{
    std::cout << "\n[replace emits modelReset only, not per-row signals]\n";
    ModlistModel m;
    m.append(mod("A"));
    m.append(mod("B"));

    QSignalSpy resetSpy(&m, &ModlistModel::modelReset);
    QSignalSpy insSpy  (&m, &ModlistModel::rowsInserted);
    QSignalSpy delSpy  (&m, &ModlistModel::rowsRemoved);

    QList<ModEntry> next;
    next << mod("X") << mod("Y") << mod("Z");
    m.replace(next);

    check("count reflects replacement", m.count() == 3);
    check("modelReset fired once",      resetSpy.count() == 1);
    check("no per-row insert signals",  insSpy.count() == 0);
    check("no per-row remove signals",  delSpy.count() == 0);
}

static void testFindByNexusUrl()
{
    std::cout << "\n[findByNexusUrl returns row index or -1]\n";
    ModlistModel m;
    m.append(mod("A", "https://nexusmods.com/morrowind/mods/1"));
    m.append(mod("B", "https://nexusmods.com/morrowind/mods/2"));

    check("hit returns the right row",
          m.findByNexusUrl("https://nexusmods.com/morrowind/mods/2") == 1);
    check("miss returns -1",
          m.findByNexusUrl("https://nexusmods.com/morrowind/mods/9") == -1);
    check("empty input returns -1",
          m.findByNexusUrl(QString()) == -1);
}

static void testFindByModPath()
{
    std::cout << "\n[findByModPath returns row index or -1]\n";
    ModlistModel m;
    m.append(mod("A", {}, "/games/mods/A_v1"));
    m.append(mod("B", {}, "/games/mods/B"));

    check("hit returns the right row",
          m.findByModPath("/games/mods/B") == 1);
    check("miss returns -1",
          m.findByModPath("/games/mods/Z") == -1);
}

// First reader migration: MainWindow::updateModCount used to walk
// m_modList row-by-row counting mods + checked-mods.  Now it consumes
// modCounts() instead.  This locks in the contract: separators don't
// count, unchecked mods are in `total` but not `active`.
static void testModCountsExcludeSeparators()
{
    std::cout << "\n[modCounts() excludes separators, counts checked as active]\n";
    ModlistModel m;
    ModEntry sep;
    sep.itemType    = QStringLiteral("separator");
    sep.displayName = "── Visuals ──";
    m.append(sep);

    ModEntry on  = mod("VanillaFix"); on.checked = true;
    ModEntry off = mod("WIP-Mod");    off.checked = false;
    m.append(on);
    m.append(off);

    const auto c = m.modCounts();
    check("total counts mods only (excludes separator)", c.total == 2);
    check("active counts checked mods only",             c.active == 1);
}

static void testModCountsEmptyModelReturnsZero()
{
    std::cout << "\n[modCounts() on empty model returns zeros]\n";
    ModlistModel m;
    const auto c = m.modCounts();
    check("total is 0", c.total == 0);
    check("active is 0", c.active == 0);
}

// `all()` is the bridge function that every controller will rely on
// once it consumes the model rather than the QListWidget.  Lock in
// that it's a true snapshot - mutating the returned list does NOT
// reach back into the model.
static void testAllReturnsIndependentSnapshot()
{
    std::cout << "\n[all() snapshot is independent of the model]\n";
    ModlistModel m;
    m.append(mod("A"));
    m.append(mod("B"));

    QList<ModEntry> snap = m.all();
    snap.removeAt(0);
    snap[0].displayName = "Mutated";

    check("model still has 2 rows",   m.count() == 2);
    check("model row 0 unchanged",    m.at(0).displayName == "A");
    check("model row 1 unchanged",    m.at(1).displayName == "B");
    check("snapshot was mutated",     snap[0].displayName == "Mutated");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::cout << "=== ModlistModel ===\n";
    testEmptyModelStartsEmpty();
    testAppendInsertEmitsRowsInserted();
    testRemoveAtEmitsRowsRemoved();
    testMoveReorders();
    testUpdateEmitsRowChanged();
    testReplaceEmitsModelReset();
    testFindByNexusUrl();
    testFindByModPath();
    testModCountsExcludeSeparators();
    testModCountsEmptyModelReturnsZero();
    testAllReturnsIndependentSnapshot();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
