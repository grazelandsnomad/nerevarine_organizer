// ModlistModel + modlist profiles. QtCore only.

#include "modlist_model.h"
#include "modentry.h"
#include "game_profiles.h"
#include "settings.h"
#include "target_language.h"

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
    std::cout << "\n[update emits dataChanged]\n";
    ModlistModel m;
    m.append(mm_mod("A"));

    QSignalSpy spy(&m, &ModlistModel::dataChanged);

    m.update(0, mm_mod("A renamed"));
    check("displayName updated",  m.at(0).displayName == "A renamed");
    check("dataChanged fired",    spy.count() == 1);

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

// pendingUpdateRows: what the toolbar's Update All counts - installed mods
// whose update a check found this session, with a page to fetch it from.
static void mm_testPendingUpdateRows()
{
    std::cout << "\n[pendingUpdateRows: this session's finds, not yet started]\n";
    const QString url = QStringLiteral("https://www.nexusmods.com/fallout4/mods/%1");
    auto flagged = [&](const QString &name, int id) {
        ModEntry e = mm_installedMod(name, url.arg(id));
        e.updateAvailable = true;
        e.updateFoundThisSession = true;
        return e;
    };
    ModlistModel m;
    m.append(mm_separator("Section"));
    m.append(flagged("Found", 1));                                   // 1: counted
    ModEntry saved = mm_installedMod("Saved flag", url.arg(2));
    saved.updateAvailable = true;                                    // 2: from an earlier session
    m.append(saved);
    ModEntry started = flagged("Started", 3);
    started.updateFoundThisSession = false;                          // 3: an install began
    m.append(started);
    ModEntry installing = flagged("Installing", 4);
    installing.installStatus = 2;                                    // 4: on its way
    m.append(installing);
    ModEntry placeholder = flagged("Not installed", 5);
    placeholder.installStatus = 0;                                   // 5
    m.append(placeholder);
    ModEntry noPage = flagged("No page", 6);
    noPage.nexusUrl = QStringLiteral("not a url");                   // 6: nothing to fetch from
    m.append(noPage);
    ModEntry cleared = flagged("Checked again", 7);
    cleared.updateAvailable = false;                                 // 7: the next check cleared it
    m.append(cleared);
    m.append(flagged("Also found", 8));                              // 8: counted

    const QList<int> rows = m.pendingUpdateRows();
    check("only this session's finds that nothing started, in list order",
          rows == QList<int>({1, 8}),
          QString::number(rows.size()));

    // Undo rebuilds every row from a snapshot; the finds come back with it.
    m.replace(m.all());
    check("an undo snapshot keeps them", m.pendingUpdateRows() == QList<int>({1, 8}));
    check("an empty list has none", ModlistModel().pendingUpdateRows().isEmpty());
}

// The two questions an update without a picker asks about the rest of the
// list before it downloads anything.
static void mm_testRowsOfOnePage()
{
    std::cout << "\n[hasOtherRowOfPage / findRowWithFile]\n";
    const QString page = QStringLiteral("https://www.nexusmods.com/fallout4/mods/100");
    ModlistModel m;
    ModEntry main = mm_installedMod("Mod", page);
    main.nexusFileId = 1001;
    m.append(main);                                                  // 0
    ModEntry optional = mm_installedMod("Mod - Patch", page);
    optional.nexusFileId = 1002;
    m.append(optional);                                              // 1
    ModEntry fetching = mm_installedMod("Mod - Extra", page);
    fetching.installStatus = 2;
    fetching.nexusFileId   = 1003;
    fetching.pendingFileId = 1004;
    m.append(fetching);                                              // 2
    ModEntry otherGame = mm_installedMod("Same id", QStringLiteral(
        "https://www.nexusmods.com/skyrimspecialedition/mods/100"));
    otherGame.nexusFileId = 1005;
    m.append(otherGame);                                             // 3
    ModEntry lone = mm_installedMod("Lone", QStringLiteral(
        "https://www.nexusmods.com/fallout4/mods/200"));
    m.append(lone);                                                  // 4
    ModEntry placeholder = mm_mod("Queued", QStringLiteral(
        "https://www.nexusmods.com/fallout4/mods/300"));
    m.append(placeholder);                                           // 5, not installed
    m.append(mm_installedMod("Lone too", QStringLiteral(
        "https://www.nexusmods.com/fallout4/mods/300")));            // 6

    check("the main row has a sibling on its page", m.hasOtherRowOfPage("fallout4", 100, 0));
    check("a lone row has none",                    !m.hasOtherRowOfPage("fallout4", 200, 4));
    check("a not-installed placeholder is not a sibling",
          !m.hasOtherRowOfPage("fallout4", 300, 6));
    check("the same id on another game is another page",
          !m.hasOtherRowOfPage("skyrimspecialedition", 100, 3));
    check("game names match in any case", m.hasOtherRowOfPage("Fallout4", 100, 0));

    check("a file another row has installed is found", m.findRowWithFile("fallout4", 100, 1002, 0) == 1);
    check("so is one another row is fetching",         m.findRowWithFile("fallout4", 100, 1004, 0) == 2);
    check("and the file an installing row still has",  m.findRowWithFile("fallout4", 100, 1003, 0) == 2);
    check("the row asking is skipped",                 m.findRowWithFile("fallout4", 100, 1001, 0) == -1);
    check("another page with the same id is not looked at",
          m.findRowWithFile("fallout4", 100, 1005, 0) == -1);
    check("no file id, no match", m.findRowWithFile("fallout4", 100, 0, 0) == -1);
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
    mm_testPendingUpdateRows();
    mm_testRowsOfOnePage();
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


// The per-profile mod-translation language. Every ModlistProfile field is
// mirrored: save() rewrites all of them from the struct, so a field added to
// save() but not to load() is silently blanked on the next save. This is the
// regression guard for exactly that.
static void mp_testTranslationLanguageSurvivesSaveLoad()
{
    std::cout << "\n[a profile's translation language survives save + load]\n";
    mp_clearHarness();

    QString gameId, profileName;
    {
        GameProfileRegistry reg;
        reg.load();
        if (reg.size() == 0) { check("registry loaded", false); return; }
        gameId      = reg.current().id;
        profileName = reg.current().activeModlist().name;
        reg.current().activeModlist().translationLanguage = QStringLiteral("spanish");
        reg.save();
    }

    // Round trip through a completely fresh registry - the save/load lockstep.
    {
        GameProfileRegistry fresh;
        fresh.load();
        check("the override came back",
              fresh.current().activeModlist().translationLanguage
                  == QStringLiteral("spanish"),
              fresh.current().activeModlist().translationLanguage);
    }
    check("and it is under the profile's own settings key",
          Settings::modlistTranslationLanguage(gameId, profileName)
              == QStringLiteral("spanish"));

    // A second save must not blank it: that is the failure mode where load()
    // was forgotten, and it only shows up on the save AFTER the one that
    // wrote the value.
    {
        GameProfileRegistry again;
        again.load();
        again.save();
        GameProfileRegistry third;
        third.load();
        check("a second save round trip does not blank it",
              third.current().activeModlist().translationLanguage
                  == QStringLiteral("spanish"),
              third.current().activeModlist().translationLanguage);
    }
}

static void mp_testTranslationLanguageDefaultsEmpty()
{
    std::cout << "\n[an untouched profile inherits rather than guessing]\n";
    mp_clearHarness();

    GameProfileRegistry reg;
    reg.load();
    if (reg.size() == 0) { check("registry loaded", false); return; }
    // Empty means "inherit the shared default". It must never come back as
    // "english", which is the assumption this whole setting replaced.
    check("no override by default",
          reg.current().activeModlist().translationLanguage.isEmpty(),
          reg.current().activeModlist().translationLanguage);
    check("and the shared default is unset until asked",
          Settings::translationLanguage().isEmpty());

    // The shared default is what the one-time prompt writes, so every other
    // game and profile inherits it and is never asked again.
    Settings::setTranslationLanguage(QStringLiteral("spanish"));
    check("the shared default round-trips",
          Settings::translationLanguage() == QStringLiteral("spanish"));
    check("an untouched profile now inherits it",
          target_language::resolve(
              reg.current().activeModlist().translationLanguage,
              Settings::translationLanguage()) == QStringLiteral("spanish"));
}

static void mp_testRenameCarriesTranslationLanguage()
{
    std::cout << "\n[renaming a profile keeps its translation language]\n";
    mp_clearHarness();

    GameProfileRegistry reg;
    reg.load();
    if (reg.size() == 0) { check("registry loaded", false); return; }

    const int idx = reg.addModlistProfile(QStringLiteral("Castellano"));
    if (idx < 0) { check("profile added", false); return; }
    reg.current().modlistProfiles[idx].translationLanguage = QStringLiteral("spanish");
    reg.save();

    check("renamed", reg.renameModlistProfile(idx, QStringLiteral("Espanol")));
    check("the language followed the new name",
          reg.current().modlistProfiles[idx].translationLanguage
              == QStringLiteral("spanish"));
    check("stored under the new name",
          Settings::modlistTranslationLanguage(reg.current().id,
                                               QStringLiteral("Espanol"))
              == QStringLiteral("spanish"));
    check("and the old key is gone",
          Settings::modlistTranslationLanguage(reg.current().id,
                                               QStringLiteral("Castellano"))
              .isEmpty());
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
    mp_testTranslationLanguageDefaultsEmpty();
    mp_testTranslationLanguageSurvivesSaveLoad();
    mp_testRenameCarriesTranslationLanguage();
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
