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
#include "modentry.h"
#include "modroles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

#include "test_harness.h"

namespace {

// One mod row, shaped the way a snapshot of the list shapes them.
ModEntry mod(const QString &name, bool enabled = true, int installStatus = 1,
             const QString &modPath = {})
{
    ModEntry e;
    e.displayName   = name;
    e.installStatus = installStatus;
    e.modPath       = modPath;
    e.checked       = enabled;
    return e;
}

void testOnlyEnabledModsCanBlockLaunch()
{
    std::cout << "\n[a warning needs an enabled mod behind it]\n";
    ModEntry off = mod(QStringLiteral("Disabled One"), false);
    off.hasMissingDependency = true;
    off.missingDependencies  = {QStringLiteral("Some Framework - disabled")};

    ModEntry sep;
    sep.itemType    = QStringLiteral("separator");
    sep.displayName = QStringLiteral("-- Visuals --");
    sep.checked     = true;

    const auto r = launch_warnings::scan({off, sep}, nullptr, QStringLiteral("morrowind"));
    check("a disabled mod's missing deps are not launch-blocking",
          r.missingDeps.isEmpty(), r.missingDeps.join("; "));
    check("a separator is not a mod", r.total() == 0);
    check("no rows, no warnings",
          launch_warnings::scan({}, nullptr, {}).total() == 0);
}

void testMissingDependenciesAreNamedPerEntry()
{
    std::cout << "\n[missing dependencies, one row each]\n";
    ModEntry m = mod(QStringLiteral("Folder Name"));
    // The display name the user chose outranks the folder the row carries.
    m.customName           = QStringLiteral("My Nice Name");
    m.hasMissingDependency = true;
    m.missingDependencies  = {QStringLiteral("Framework A - disabled"),
                              QStringLiteral("Framework B - not installed")};

    const auto r = launch_warnings::scan({m}, nullptr, {});
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

    const QList<ModEntry> rows = {
        mod(QStringLiteral("Empty"),   true, 1, emptyPath),
        mod(QStringLiteral("HasFile"), true, 1, anyFilePath),
        // Not installed yet: nothing on disk is exactly what is expected.
        mod(QStringLiteral("Pending"), true, 0, emptyPath),
    };

    const auto r = launch_warnings::scan(rows, nullptr, {});
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

    ModEntry bad = mod(QStringLiteral("Bad Mod"));
    bad.nexusUrl = QStringLiteral("https://www.nexusmods.com/oblivion/mods/123");
    ModEntry fine = mod(QStringLiteral("Fine Mod"));
    fine.nexusUrl = QStringLiteral("https://www.nexusmods.com/oblivion/mods/999");
    ModEntry off = mod(QStringLiteral("Bad But Off"), false);
    off.nexusUrl = QStringLiteral("https://www.nexusmods.com/oblivion/mods/123");
    const QList<ModEntry> rows = {bad, fine, off};

    const auto r = launch_warnings::scan(rows, &reg, QStringLiteral("oblivion"));
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
          launch_warnings::scan(rows, &reg, QString()).forbiddenEnabled.isEmpty());
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
    QCoreApplication app(argc, argv);

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
