// ModlistModel + modlist profiles. QtCore only.

#include "modlist_model.h"
#include "modentry.h"
#include "game_profiles.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <iostream>

#include "test_harness.h"

// --- ModlistModel ---

static ModEntry mm_mod(const QString &name, const QString &url = {},
                       const QString &path = {})
{
    ModEntry e;
    e.itemType    = QStringLiteral("mod");
    e.displayName = name;
    e.nexusUrl    = url;
    e.modPath     = path;
    return e;
}

// installStatus == 1 is what findInstalledByModId / installedModDisplayNames filter on.
static ModEntry mm_installedMod(const QString &name, const QString &url = {})
{
    ModEntry e = mm_mod(name, url);
    e.installStatus = 1;
    return e;
}

static ModEntry mm_separator(const QString &name)
{
    ModEntry e;
    e.itemType    = QStringLiteral("separator");
    e.displayName = name;
    return e;
}

static void mm_testEmptyModelStartsEmpty()
{
    std::cout << "\n[default-constructed model is empty]\n";
    ModlistModel m;
    check("count is 0", m.count() == 0);
    check("isEmpty true", m.isEmpty());
    check("at(0) returns sentinel ModEntry",
          m.at(0).displayName.isEmpty());
}

static void mm_testAppendInsertEmitsRowsInserted()
{
    std::cout << "\n[append + insert emit rowsInserted]\n";
    ModlistModel m;
    QSignalSpy spy(&m, &ModlistModel::rowsInserted);

    const int idx0 = m.append(mm_mod("A"));
    check("append returns 0",        idx0 == 0);
    check("count is 1 after append", m.count() == 1);
    check("first append fired one signal", spy.count() == 1);

    m.insertAt(0, mm_mod("Z"));
    check("insertAt at front",         m.at(0).displayName == "Z");
    check("previous A pushed to row 1", m.at(1).displayName == "A");
    check("two signals total",          spy.count() == 2);
}

static void mm_testRemoveAtEmitsRowsRemoved()
{
    std::cout << "\n[removeAt emits rowsRemoved + clamps invalid rows]\n";
    ModlistModel m;
    m.append(mm_mod("A"));
    m.append(mm_mod("B"));

    QSignalSpy spy(&m, &ModlistModel::rowsRemoved);
    m.removeAt(0);
    check("count drops to 1",       m.count() == 1);
    check("survivor is B",          m.at(0).displayName == "B");
    check("rowsRemoved fired once", spy.count() == 1);

    m.removeAt(99);   // OOB
    check("invalid row is no-op", m.count() == 1);
    check("no extra signal",      spy.count() == 1);
}

static void mm_testMoveReorders()
{
    std::cout << "\n[move emits rowsMoved and reorders entries]\n";
    ModlistModel m;
    m.append(mm_mod("A"));
    m.append(mm_mod("B"));
    m.append(mm_mod("C"));

    QSignalSpy spy(&m, &ModlistModel::rowsMoved);
    m.move(0, 2);
    check("A moved to row 2", m.at(2).displayName == "A");
    check("B at row 0",       m.at(0).displayName == "B");
    check("C at row 1",       m.at(1).displayName == "C");
    check("rowsMoved fired",  spy.count() == 1);
}

static void mm_testUpdateEmitsRowChanged()
{
    std::cout << "\n[update emits rowChanged]\n";
    ModlistModel m;
    m.append(mm_mod("A"));

    QSignalSpy spy(&m, &ModlistModel::rowChanged);

    m.update(0, mm_mod("A renamed"));
    check("displayName updated",  m.at(0).displayName == "A renamed");
    check("rowChanged fired",     spy.count() == 1);

    m.update(99, mm_mod("ignored"));
    check("invalid row is no-op", spy.count() == 1);
}

static void mm_testReplaceEmitsModelReset()
{
    std::cout << "\n[replace emits modelReset only, not per-row signals]\n";
    ModlistModel m;
    m.append(mm_mod("A"));
    m.append(mm_mod("B"));

    QSignalSpy resetSpy(&m, &ModlistModel::modelReset);
    QSignalSpy insSpy  (&m, &ModlistModel::rowsInserted);
    QSignalSpy delSpy  (&m, &ModlistModel::rowsRemoved);

    QList<ModEntry> next;
    next << mm_mod("X") << mm_mod("Y") << mm_mod("Z");
    m.replace(next);

    check("count reflects replacement", m.count() == 3);
    check("modelReset fired once",      resetSpy.count() == 1);
    check("no per-row insert signals",  insSpy.count() == 0);
    check("no per-row remove signals",  delSpy.count() == 0);
}

static void mm_testFindByNexusUrl()
{
    std::cout << "\n[findByNexusUrl returns row index or -1]\n";
    ModlistModel m;
    m.append(mm_mod("A", "https://nexusmods.com/morrowind/mods/1"));
    m.append(mm_mod("B", "https://nexusmods.com/morrowind/mods/2"));

    check("hit returns the right row",
          m.findByNexusUrl("https://nexusmods.com/morrowind/mods/2") == 1);
    check("miss returns -1",
          m.findByNexusUrl("https://nexusmods.com/morrowind/mods/9") == -1);
    check("empty input returns -1",
          m.findByNexusUrl(QString()) == -1);
}

static void mm_testFindByModPath()
{
    std::cout << "\n[findByModPath returns row index or -1]\n";
    ModlistModel m;
    m.append(mm_mod("A", {}, "/games/mods/A_v1"));
    m.append(mm_mod("B", {}, "/games/mods/B"));

    check("hit returns the right row",
          m.findByModPath("/games/mods/B") == 1);
    check("miss returns -1",
          m.findByModPath("/games/mods/Z") == -1);
}

// modCounts(): separators don't count, unchecked mods are in total but not active.
static void mm_testModCountsExcludeSeparators()
{
    std::cout << "\n[modCounts() excludes separators, counts checked as active]\n";
    ModlistModel m;
    ModEntry sep;
    sep.itemType    = QStringLiteral("separator");
    sep.displayName = "── Visuals ──";
    m.append(sep);

    ModEntry on  = mm_mod("VanillaFix"); on.checked = true;
    ModEntry off = mm_mod("WIP-Mod");    off.checked = false;
    m.append(on);
    m.append(off);

    const auto c = m.modCounts();
    check("total counts mods only (excludes separator)", c.total == 2);
    check("active counts checked mods only",             c.active == 1);
}

static void mm_testModCountsEmptyModelReturnsZero()
{
    std::cout << "\n[modCounts() on empty model returns zeros]\n";
    ModlistModel m;
    const auto c = m.modCounts();
    check("total is 0", c.total == 0);
    check("active is 0", c.active == 0);
}

// all() is a snapshot; mutating the returned list must not touch the model.
static void mm_testAllReturnsIndependentSnapshot()
{
    std::cout << "\n[all() snapshot is independent of the model]\n";
    ModlistModel m;
    m.append(mm_mod("A"));
    m.append(mm_mod("B"));

    QList<ModEntry> snap = m.all();
    snap.removeAt(0);
    snap[0].displayName = "Mutated";

    check("model still has 2 rows",   m.count() == 2);
    check("model row 0 unchanged",    m.at(0).displayName == "A");
    check("model row 1 unchanged",    m.at(1).displayName == "B");
    check("snapshot was mutated",     snap[0].displayName == "Mutated");
}

// findInstalledByModId: only installed rows, case-insensitive game, exceptRow
// skipped, same id on a different game doesn't match.
static void mm_testFindInstalledByModId()
{
    std::cout << "\n[findInstalledByModId matches installed rows by game+modId]\n";
    ModlistModel m;
    m.append(mm_installedMod("OAAB Data", "https://www.nexusmods.com/morrowind/mods/49042")); // 0
    m.append(mm_mod("Pending DL",         "https://www.nexusmods.com/morrowind/mods/55555")); // 1 (status 0)
    m.append(mm_separator("── Section ──"));                                                  // 2
    m.append(mm_installedMod("Same id, other game", "https://www.nexusmods.com/skyrim/mods/49042")); // 3

    check("finds the installed row",
          m.findInstalledByModId("morrowind", 49042) == 0);
    check("game match is case-insensitive",
          m.findInstalledByModId("Morrowind", 49042) == 0);
    check("not-installed (status 0) row is ignored",
          m.findInstalledByModId("morrowind", 55555) == -1);
    check("same modId on a different game does not match",
          m.findInstalledByModId("oblivion", 49042) == -1);
    check("unknown modId returns -1",
          m.findInstalledByModId("morrowind", 1) == -1);
    check("exceptRow is skipped",
          m.findInstalledByModId("morrowind", 49042, /*exceptRow=*/0) == -1);
}

static void mm_testModDisplayNames()
{
    std::cout << "\n[modDisplayNames lists mod names, excludes separators + blanks]\n";
    ModlistModel m;
    m.append(mm_mod("Alpha"));
    m.append(mm_separator("── Section ──"));
    m.append(mm_mod(""));   // blank name, excluded
    m.append(mm_installedMod("Beta", "https://www.nexusmods.com/morrowind/mods/2"));

    const QStringList names = m.modDisplayNames();
    check("includes both named mods in order",
          names == QStringList({"Alpha", "Beta"}),
          names.join(','));
    check("separator excluded", !names.contains("── Section ──"));
}

static void mm_testInstalledModDisplayNames()
{
    std::cout << "\n[installedModDisplayNames narrows to installStatus == 1]\n";
    ModlistModel m;
    m.append(mm_installedMod("Installed One", "https://www.nexusmods.com/morrowind/mods/1"));
    m.append(mm_mod("Not Installed"));   // status 0
    m.append(mm_installedMod("Installed Two", "https://www.nexusmods.com/morrowind/mods/2"));

    const QStringList names = m.installedModDisplayNames();
    check("only installed mods listed",
          names == QStringList({"Installed One", "Installed Two"}),
          names.join(','));
}

static void run_modlist_model()
{
    std::cout << "=== ModlistModel ===\n";
    mm_testEmptyModelStartsEmpty();
    mm_testAppendInsertEmitsRowsInserted();
    mm_testRemoveAtEmitsRowsRemoved();
    mm_testMoveReorders();
    mm_testUpdateEmitsRowChanged();
    mm_testReplaceEmitsModelReset();
    mm_testFindByNexusUrl();
    mm_testFindByModPath();
    mm_testFindInstalledByModId();
    mm_testModDisplayNames();
    mm_testInstalledModDisplayNames();
    mm_testModCountsExcludeSeparators();
    mm_testModCountsEmptyModelReturnsZero();
    mm_testAllReturnsIndependentSnapshot();
}

// --- modlist profiles ---

static void mp_writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(bytes);
    f.close();
}

static QString mp_stateRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

// Point QSettings + AppDataLocation at a temp dir so neither QSettings nor
// resolveStateFile touch real files.
static QTemporaryDir &mp_harness()
{
    static QTemporaryDir d;
    static bool          initialised = false;
    if (!initialised) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, d.path());
        qputenv("XDG_DATA_HOME", d.path().toUtf8());
        // Fake an AppImage so resolveStateFile routes to AppDataLocation
        // (matching stateRoot()); otherwise it uses the binary-adjacent
        // build/tests/ path and our lookups miss.
        qputenv("APPIMAGE", "/fake.AppImage");
        QStandardPaths::setTestModeEnabled(true);
        initialised = true;
    }
    return d;
}

// Reset state between tests.
static void mp_clearHarness()
{
    QSettings().clear();
    QDir(mp_stateRoot()).removeRecursively();
    QDir().mkpath(mp_stateRoot());
}

static void mp_testFirstRunMigrationCreatesDefault()
{
    std::cout << "\n[first run on a clean install → Default profile auto-created]\n";
    mp_clearHarness();

    GameProfileRegistry reg;
    reg.load();

    check("registry has at least one game", reg.size() > 0);
    if (reg.size() == 0) return;

    const GameProfile &gp = reg.current();
    check("morrowind profile is current", gp.id == "morrowind");
    check("exactly one modlist profile after migration",
          gp.modlistProfiles.size() == 1);
    if (gp.modlistProfiles.isEmpty()) return;
    check("migrated profile is named Default",
          gp.modlistProfiles[0].name == "Default");
    check("migrated profile uses LEGACY modlist filename (no file moves)",
          gp.modlistProfiles[0].modlistFilename == "modlist_morrowind.txt");
    check("migrated profile uses LEGACY load-order filename",
          gp.modlistProfiles[0].loadOrderFilename == "loadorder_morrowind.txt");
    check("Default profile inherits the per-game modsDir",
          gp.modlistProfiles[0].modsDir == gp.modsDir);
}

static void mp_testAddProfileUsesCanonicalScheme()
{
    std::cout << "\n[addModlistProfile → modlist_<game>__<name>.txt]\n";
    mp_clearHarness();

    GameProfileRegistry reg;
    reg.load();

    const int idx = reg.addModlistProfile("Wabbajack Test");
    check("add returned a valid index", idx >= 1);

    const GameProfile &gp = reg.current();
    check("two profiles after add", gp.modlistProfiles.size() == 2);
    if (gp.modlistProfiles.size() < 2) return;
    const ModlistProfile &mp = gp.modlistProfiles[idx];
    check("new profile has empty modsDir (first install will prompt)",
          mp.modsDir.isEmpty());
    check("new profile uses canonical modlist filename",
          mp.modlistFilename == "modlist_morrowind__Wabbajack_Test.txt");
    check("new profile uses canonical load-order filename",
          mp.loadOrderFilename == "loadorder_morrowind__Wabbajack_Test.txt");

    const int collision = reg.addModlistProfile("wabbajack TEST");
    check("case-insensitive collision rejected", collision < 0);
}

static void mp_testCloneCopiesStateFiles()
{
    std::cout << "\n[cloneModlistProfile → state files duplicated, modsDir empty]\n";
    mp_clearHarness();

    GameProfileRegistry reg;
    reg.load();

    // Seed the source profile's state files.
    mp_writeFile(mp_stateRoot() + "/modlist_morrowind.txt",   "ModA\nModB\n");
    mp_writeFile(mp_stateRoot() + "/loadorder_morrowind.txt", "ModA.esp\nModB.esp\n");

    const int idx = reg.cloneModlistProfile(0, "Cloned");
    check("clone returned a valid index", idx >= 1);

    const GameProfile &gp = reg.current();
    if (idx < 0) return;
    const ModlistProfile &mp = gp.modlistProfiles[idx];
    check("clone has empty modsDir (clone-empty semantics)",
          mp.modsDir.isEmpty());
    check("clone modlist filename uses canonical scheme",
          mp.modlistFilename == "modlist_morrowind__Cloned.txt");

    QFile mlist(mp_stateRoot() + "/" + mp.modlistFilename);
    check("clone duplicated the source's modlist file",
          mlist.exists());
    if (mlist.open(QIODevice::ReadOnly)) {
        check("clone modlist contents match source",
              mlist.readAll() == "ModA\nModB\n");
    }

    QFile lord(mp_stateRoot() + "/" + mp.loadOrderFilename);
    check("clone duplicated the source's load-order file",
          lord.exists());
}

static void mp_testSetActiveModsDirSyncsBoth()
{
    std::cout << "\n[setActiveModsDir → updates active profile + GameProfile mirror]\n";
    mp_clearHarness();

    GameProfileRegistry reg;
    reg.load();

    const QString picked = mp_stateRoot() + "/picked_mods";
    reg.setActiveModsDir(picked);

    const GameProfile &gp = reg.current();
    check("GameProfile mirror updated", gp.modsDir == picked);
    check("active ModlistProfile mods_dir updated",
          gp.activeModlist().modsDir == picked);

    // Reload to cover persistence.
    GameProfileRegistry fresh;
    fresh.load();
    check("modsDir survives a save/load cycle",
          fresh.current().activeModlist().modsDir == picked);
}

static void mp_testRemoveProfileDropsSettings()
{
    std::cout << "\n[removeModlistProfile → settings group removed, file optional]\n";
    mp_clearHarness();

    GameProfileRegistry reg;
    reg.load();
    const int idx = reg.addModlistProfile("ToRemove");
    if (idx < 0) { check("setup add", false); return; }

    // Touch the modlist file so the delete-state-files branch runs.
    const QString fn = mp_stateRoot() + "/modlist_morrowind__ToRemove.txt";
    mp_writeFile(fn, "x\n");

    const bool ok = reg.removeModlistProfile(idx, /*deleteStateFiles=*/true);
    check("remove succeeded", ok);
    check("profile gone from list",
          reg.current().modlistProfiles.size() == 1);
    check("state file deleted from disk", !QFile::exists(fn));

    // Last remaining profile can't be removed.
    const bool refused = reg.removeModlistProfile(0, true);
    check("last remaining profile is not removable", !refused);
}

static void mp_testRenameKeepsLegacyFilenameForDefault()
{
    std::cout << "\n[rename Default profile → keeps legacy modlist_<game>.txt]\n";
    mp_clearHarness();

    GameProfileRegistry reg;
    reg.load();
    // Default sits at index 0 with the legacy filename.
    const bool ok = reg.renameModlistProfile(0, "Vanilla++");
    check("rename succeeded", ok);

    const ModlistProfile &mp = reg.current().modlistProfiles[0];
    check("name updated",                 mp.name == "Vanilla++");
    check("legacy filename PRESERVED",    mp.modlistFilename   == "modlist_morrowind.txt");
    check("legacy load-order PRESERVED",  mp.loadOrderFilename == "loadorder_morrowind.txt");
}

static void mp_testRenameNonDefaultMovesFiles()
{
    std::cout << "\n[rename canonically-named profile → state files renamed on disk]\n";
    mp_clearHarness();

    GameProfileRegistry reg;
    reg.load();
    const int idx = reg.addModlistProfile("Old");
    if (idx < 0) { check("setup add", false); return; }
    mp_writeFile(mp_stateRoot() + "/modlist_morrowind__Old.txt", "x\n");
    mp_writeFile(mp_stateRoot() + "/loadorder_morrowind__Old.txt", "y\n");

    const bool ok = reg.renameModlistProfile(idx, "New");
    check("rename succeeded", ok);

    const ModlistProfile &mp = reg.current().modlistProfiles[idx];
    check("name updated",
          mp.name == "New");
    check("modlist filename changed",
          mp.modlistFilename == "modlist_morrowind__New.txt");
    check("source modlist file gone from disk",
          !QFile::exists(mp_stateRoot() + "/modlist_morrowind__Old.txt"));
    check("destination modlist file exists",
          QFile::exists(mp_stateRoot() + "/modlist_morrowind__New.txt"));
}

static void run_modlist_profiles()
{
    mp_harness();   // set up the temp INI / state root

    std::cout << "=== modlist profiles ===\n";
    mp_testFirstRunMigrationCreatesDefault();
    mp_testAddProfileUsesCanonicalScheme();
    mp_testCloneCopiesStateFiles();
    mp_testSetActiveModsDirSyncsBoth();
    mp_testRemoveProfileDropsSettings();
    mp_testRenameKeepsLegacyFilenameForDefault();
    mp_testRenameNonDefaultMovesFiles();
}

int main(int argc, char **argv)
{
    QCoreApplication::setOrganizationName("nerevarine_test");
    QCoreApplication::setApplicationName("modlist_profiles_test");
    QCoreApplication app(argc, argv);

    run_modlist_model();
    run_modlist_profiles();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
