// The launch-blocking rules. These decide what stands between "play" and the
// game starting, and until now not one of them was under test - the largest of
// the zero-refactor gaps found in the 2026-09 coverage census: the module was
// already free functions with a header, it just had no target.
//
// scan() itself is judgement over row data, so every rule here is a table of
// rows in and a list of rows out. The dialog half (showDialog) stays untested
// for the usual reason: it ends in exec().

#include "launch_warnings.h"

#include "forbidden_mods.h"
#include "modroles.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QTemporaryDir>

#include <iostream>

#include "test_harness.h"

namespace {

// One mod row, shaped the way MainWindow shapes them.
QListWidgetItem *addMod(QListWidget *list, const QString &name,
                        bool enabled = true, int installStatus = 1,
                        const QString &modPath = {})
{
    auto *it = new QListWidgetItem(name);
    it->setData(ModRole::ItemType,      ItemType::Mod);
    it->setData(ModRole::InstallStatus, installStatus);
    it->setData(ModRole::ModPath,       modPath);
    it->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    list->addItem(it);
    return it;
}

void testOnlyEnabledModsCanBlockLaunch()
{
    std::cout << "\n[a warning needs an enabled mod behind it]\n";
    QListWidget list;

    auto *off = addMod(&list, QStringLiteral("Disabled One"), false);
    off->setData(ModRole::HasMissingDependency, true);
    off->setData(ModRole::MissingDependencies,
                 QStringList{QStringLiteral("Some Framework - disabled")});

    auto *sep = new QListWidgetItem(QStringLiteral("-- Visuals --"));
    sep->setData(ModRole::ItemType, ItemType::Separator);
    sep->setCheckState(Qt::Checked);
    list.addItem(sep);

    const auto r = launch_warnings::scan(&list, nullptr, QStringLiteral("morrowind"));
    check("a disabled mod's missing deps are not launch-blocking",
          r.missingDeps.isEmpty(), r.missingDeps.join("; "));
    check("a separator is not a mod", r.total() == 0);
    check("no list, no warnings",
          launch_warnings::scan(nullptr, nullptr, {}).total() == 0);
}

void testMissingDependenciesAreNamedPerEntry()
{
    std::cout << "\n[missing dependencies, one row each]\n";
    QListWidget list;
    auto *m = addMod(&list, QStringLiteral("Folder Name"));
    // The display name the user chose outranks the folder the row carries.
    m->setData(ModRole::CustomName, QStringLiteral("My Nice Name"));
    m->setData(ModRole::HasMissingDependency, true);
    m->setData(ModRole::MissingDependencies,
               QStringList{QStringLiteral("Framework A - disabled"),
                           QStringLiteral("Framework B - not installed")});

    const auto r = launch_warnings::scan(&list, nullptr, {});
    check("both entries become rows", r.missingDeps.size() == 2,
          QString::number(r.missingDeps.size()));
    check("named by the custom name, not the folder",
          r.missingDeps.first().startsWith(QStringLiteral("My Nice Name:")),
          r.missingDeps.first());
    check("they count toward the total", r.total() == 2);
}

void testAnEmptyInstallIsFlaggedAndAnyContentClearsIt()
{
    std::cout << "\n[an installed mod with nothing in it]\n";
    QTemporaryDir dir;

    // Truly empty folder: marked installed, nothing on disk.
    const QString emptyPath = dir.filePath(QStringLiteral("empty"));
    QDir().mkpath(emptyPath);

    // Any file at all is accepted - the format may simply be one this app
    // does not know, and flagging somebody's working mod is worse than
    // missing a broken one.
    const QString anyFilePath = dir.filePath(QStringLiteral("anyfile"));
    QDir().mkpath(anyFilePath);
    { QFile f(anyFilePath + QStringLiteral("/readme.txt"));
      check("fixture file opens", f.open(QIODevice::WriteOnly)); f.write("hi"); }

    QListWidget list;
    addMod(&list, QStringLiteral("Empty"),   true, 1, emptyPath);
    addMod(&list, QStringLiteral("HasFile"), true, 1, anyFilePath);
    // Not installed yet: nothing on disk is exactly what is expected.
    addMod(&list, QStringLiteral("Pending"), true, 0, emptyPath);

    const auto r = launch_warnings::scan(&list, nullptr, {});
    check("the empty install is flagged", r.emptyInstalls.size() == 1,
          r.emptyInstalls.join("; "));
    check("and it is the right one",
          r.emptyInstalls.first().startsWith(QStringLiteral("Empty:")));
}

void testForbiddenModsAreCaughtByModPage()
{
    std::cout << "\n[an enabled mod on the forbidden list]\n";
    QTemporaryDir dir;

    // The registry's own on-disk shape: name \t url \t annotation. A
    // non-Morrowind game id, so the legacy-migration path stays out of the
    // fixture's way.
    const QString path = dir.filePath(QStringLiteral("forbidden_mods_oblivion.txt"));
    { QFile f(path);
      check("fixture list opens", f.open(QIODevice::WriteOnly | QIODevice::Text));
      f.write("Bad Mod\thttps://www.nexusmods.com/oblivion/mods/123\tbreaks saves\n"); }
    ForbiddenModsRegistry reg;
    reg.reload(path, QStringLiteral("oblivion"), QString());
    check("the fixture entry loaded", reg.size() == 1, QString::number(reg.size()));

    QListWidget list;
    auto *bad = addMod(&list, QStringLiteral("Bad Mod"));
    bad->setData(ModRole::NexusUrl,
                 QStringLiteral("https://www.nexusmods.com/oblivion/mods/123"));
    auto *fine = addMod(&list, QStringLiteral("Fine Mod"));
    fine->setData(ModRole::NexusUrl,
                  QStringLiteral("https://www.nexusmods.com/oblivion/mods/999"));
    auto *off = addMod(&list, QStringLiteral("Bad But Off"), false);
    off->setData(ModRole::NexusUrl,
                 QStringLiteral("https://www.nexusmods.com/oblivion/mods/123"));

    const auto r = launch_warnings::scan(&list, &reg, QStringLiteral("oblivion"));
    check("the enabled forbidden mod is one row", r.forbiddenEnabled.size() == 1,
          r.forbiddenEnabled.join("; "));
    check("with the reason quoted",
          r.forbiddenEnabled.first().contains(QStringLiteral("breaks saves")),
          r.forbiddenEnabled.first());
    check("an unlisted mod is not touched",
          !r.forbiddenEnabled.first().startsWith(QStringLiteral("Fine Mod")));

    // No game id means the check cannot know which list applies - silence,
    // not a guess against the wrong game's list.
    check("no game id, no forbidden rows",
          launch_warnings::scan(&list, &reg, QString()).forbiddenEnabled.isEmpty());
}

void testScriptExtenderRowsOnlyExistWithFindings()
{
    std::cout << "\n[script-extender rows come from findings alone]\n";
    launch_warnings::Result r;
    launch_warnings::addScriptExtender(r, skse_check::Findings{});
    check("empty findings add nothing", r.total() == 0);
    check("and leave no explainer", r.scriptExtenderExplain.isEmpty());
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    testOnlyEnabledModsCanBlockLaunch();
    testMissingDependenciesAreNamedPerEntry();
    testAnEmptyInstallIsFlaggedAndAnyContentClearsIt();
    testForbiddenModsAreCaughtByModPage();
    testScriptExtenderRowsOnlyExistWithFindings();

    std::cout << "\n";
    if (s_failed == 0)
        std::cout << "\033[32m" << s_passed << " / " << s_passed
                  << " tests passed\033[0m\n";
    else
        std::cout << s_passed << " passed, \033[31m" << s_failed
                  << " failed\033[0m\n";
    return s_failed == 0 ? 0 : 1;
}
