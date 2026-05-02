// tests/test_modlist_profiles.cpp
//
// Regression tests for GameProfileRegistry's modlist-profile machinery.
//
// What's locked in:
//   · First-run migration auto-creates a "Default" profile that adopts the
//     legacy modlist_<gameId>.txt + per-game modsDir, with NO file moves.
//   · Adding a profile picks the canonical filename scheme and starts with
//     an empty modsDir (the install path is responsible for prompting).
//   · Cloning duplicates the source's state files on disk so the clone
//     starts with the same modlist + load order as the source.
//   · Setting a profile's modsDir writes through to BOTH the active
//     ModlistProfile and the legacy GameProfile::modsDir mirror.
//   · Removing a profile drops the QSettings group + (optionally) the
//     state files; refuses to remove the last remaining profile.
//   · Renaming preserves the legacy filename for the migrated Default
//     so a process crash mid-rename can't lose the user's data.
//
// Each test runs against an isolated QSettings INI under a QTemporaryDir
// so the user's real settings are never touched.

#include "game_profiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
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

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(bytes);
    f.close();
}

static QString stateRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

// One-time setup per process: redirect QSettings to a temp INI and force the
// AppDataLocation to a writable temp dir, so neither QSettings nor
// resolveStateFile inside game_profiles.cpp ever touches the user's files.
static QTemporaryDir &harness()
{
    static QTemporaryDir d;
    static bool          initialised = false;
    if (!initialised) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, d.path());
        qputenv("XDG_DATA_HOME", d.path().toUtf8());
        // Pretend we're in an AppImage so resolveStateFile inside
        // game_profiles.cpp routes to AppDataLocation unconditionally,
        // matching what stateRoot() returns here.  Without this, the
        // function prefers the binary-adjacent build/tests/ path and the
        // file lookups in this test miss.
        qputenv("APPIMAGE", "/fake.AppImage");
        QStandardPaths::setTestModeEnabled(true);
        initialised = true;
    }
    return d;
}

// Reset all state between tests so settings from one don't leak into another.
static void clearHarness()
{
    QSettings().clear();
    QDir(stateRoot()).removeRecursively();
    QDir().mkpath(stateRoot());
}

// -- Scenarios ---

static void testFirstRunMigrationCreatesDefault()
{
    std::cout << "\n[first run on a clean install → Default profile auto-created]\n";
    clearHarness();

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

static void testAddProfileUsesCanonicalScheme()
{
    std::cout << "\n[addModlistProfile → canonical modlist_<game>__<name>.txt]\n";
    clearHarness();

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

    // Collision rejected.
    const int collision = reg.addModlistProfile("wabbajack TEST");
    check("case-insensitive collision rejected", collision < 0);
}

static void testCloneCopiesStateFiles()
{
    std::cout << "\n[cloneModlistProfile → state files duplicated, modsDir empty]\n";
    clearHarness();

    GameProfileRegistry reg;
    reg.load();

    // Pre-populate the source profile's state files.
    writeFile(stateRoot() + "/modlist_morrowind.txt",   "ModA\nModB\n");
    writeFile(stateRoot() + "/loadorder_morrowind.txt", "ModA.esp\nModB.esp\n");

    const int idx = reg.cloneModlistProfile(0, "Cloned");
    check("clone returned a valid index", idx >= 1);

    const GameProfile &gp = reg.current();
    if (idx < 0) return;
    const ModlistProfile &mp = gp.modlistProfiles[idx];
    check("clone has empty modsDir (clone-empty semantics)",
          mp.modsDir.isEmpty());
    check("clone modlist filename uses canonical scheme",
          mp.modlistFilename == "modlist_morrowind__Cloned.txt");

    QFile mlist(stateRoot() + "/" + mp.modlistFilename);
    check("clone duplicated the source's modlist file",
          mlist.exists());
    if (mlist.open(QIODevice::ReadOnly)) {
        check("clone modlist contents match source",
              mlist.readAll() == "ModA\nModB\n");
    }

    QFile lord(stateRoot() + "/" + mp.loadOrderFilename);
    check("clone duplicated the source's load-order file",
          lord.exists());
}

static void testSetActiveModsDirSyncsBoth()
{
    std::cout << "\n[setActiveModsDir → updates active profile + GameProfile mirror]\n";
    clearHarness();

    GameProfileRegistry reg;
    reg.load();

    const QString picked = stateRoot() + "/picked_mods";
    reg.setActiveModsDir(picked);

    const GameProfile &gp = reg.current();
    check("GameProfile mirror updated", gp.modsDir == picked);
    check("active ModlistProfile mods_dir updated",
          gp.activeModlist().modsDir == picked);

    // Reload from QSettings - persistence covered.
    GameProfileRegistry fresh;
    fresh.load();
    check("modsDir survives a save/load cycle",
          fresh.current().activeModlist().modsDir == picked);
}

static void testRemoveProfileDropsSettings()
{
    std::cout << "\n[removeModlistProfile → settings group removed, file optional]\n";
    clearHarness();

    GameProfileRegistry reg;
    reg.load();
    const int idx = reg.addModlistProfile("ToRemove");
    if (idx < 0) { check("setup add", false); return; }

    // Touch the new profile's would-be modlist file so we can verify the
    // delete-state-files branch.
    const QString fn = stateRoot() + "/modlist_morrowind__ToRemove.txt";
    writeFile(fn, "x\n");

    const bool ok = reg.removeModlistProfile(idx, /*deleteStateFiles=*/true);
    check("remove succeeded", ok);
    check("profile gone from list",
          reg.current().modlistProfiles.size() == 1);
    check("state file deleted from disk", !QFile::exists(fn));

    // Last-remaining profile cannot be removed.
    const bool refused = reg.removeModlistProfile(0, true);
    check("last remaining profile is not removable", !refused);
}

static void testRenameKeepsLegacyFilenameForDefault()
{
    std::cout << "\n[rename Default profile → keeps legacy modlist_<game>.txt]\n";
    clearHarness();

    GameProfileRegistry reg;
    reg.load();
    // Default is at index 0, with the legacy filename.
    const bool ok = reg.renameModlistProfile(0, "Vanilla++");
    check("rename succeeded", ok);

    const ModlistProfile &mp = reg.current().modlistProfiles[0];
    check("name updated",                 mp.name == "Vanilla++");
    check("legacy filename PRESERVED",    mp.modlistFilename   == "modlist_morrowind.txt");
    check("legacy load-order PRESERVED",  mp.loadOrderFilename == "loadorder_morrowind.txt");
}

static void testRenameNonDefaultMovesFiles()
{
    std::cout << "\n[rename canonically-named profile → state files renamed on disk]\n";
    clearHarness();

    GameProfileRegistry reg;
    reg.load();
    const int idx = reg.addModlistProfile("Old");
    if (idx < 0) { check("setup add", false); return; }
    writeFile(stateRoot() + "/modlist_morrowind__Old.txt", "x\n");
    writeFile(stateRoot() + "/loadorder_morrowind__Old.txt", "y\n");

    const bool ok = reg.renameModlistProfile(idx, "New");
    check("rename succeeded", ok);

    const ModlistProfile &mp = reg.current().modlistProfiles[idx];
    check("name updated",
          mp.name == "New");
    check("modlist filename changed",
          mp.modlistFilename == "modlist_morrowind__New.txt");
    check("source modlist file gone from disk",
          !QFile::exists(stateRoot() + "/modlist_morrowind__Old.txt"));
    check("destination modlist file exists",
          QFile::exists(stateRoot() + "/modlist_morrowind__New.txt"));
}

int main(int argc, char **argv)
{
    QCoreApplication::setOrganizationName("nerevarine_test");
    QCoreApplication::setApplicationName("modlist_profiles_test");
    QCoreApplication app(argc, argv);
    harness();   // initialise the temp INI / state root

    std::cout << "=== modlist profiles ===\n";
    testFirstRunMigrationCreatesDefault();
    testAddProfileUsesCanonicalScheme();
    testCloneCopiesStateFiles();
    testSetActiveModsDirSyncsBoth();
    testRemoveProfileDropsSettings();
    testRenameKeepsLegacyFilenameForDefault();
    testRenameNonDefaultMovesFiles();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
