// tests/test_modlist_sync_guard.cpp
//
// Golden-file tests for openmw::findModlistPathDrift.
//
// Scenario: modlist_morrowind.txt is committed to git and synced across
// machines.  On machine A, mods live at /home/jalcazo/Games/nerevarine_mods/.
// On machine B, they live at /mnt/big/modding/mw/mods/.  Without this
// guard, the synced file quietly points openmw.cfg at paths that don't
// exist on the current host.

#include "modlist_sync_guard.h"

#include <QCoreApplication>

#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok,
                  const QString &got = {}, const QString &want = {})
{
    if (ok) {
        std::cout << "  \033[32m\xE2\x9C\x93\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m\xE2\x9C\x97\033[0m " << name << "\n";
        if (!want.isEmpty() || !got.isEmpty()) {
            std::cout << "    --- want ---\n" << want.toStdString() << "\n";
            std::cout << "    ---  got ---\n" << got.toStdString()  << "\n";
        }
        ++s_failed;
    }
}

using openmw::SyncGuardInput;
using openmw::canonicalizePathText;
using openmw::findModlistPathDrift;

static SyncGuardInput mod(const QString &label, const QString &path)
{
    return {label, path};
}

// -- canonicalizePathText ---

static void testCanonicalizePreservesAbsolute()
{
    std::cout << "testCanonicalizePreservesAbsolute\n";
    check("single leading / preserved",
          canonicalizePathText("/home/x/mods") == "/home/x/mods");
    check("backslashes → forward slashes",
          canonicalizePathText("C:\\Users\\x\\mods") == "C:/Users/x/mods");
    check("duplicate slashes collapsed",
          canonicalizePathText("/home//x///mods") == "/home/x/mods");
    check("./ collapsed",
          canonicalizePathText("/home/x/./mods/./Foo") == "/home/x/mods/Foo");
    check("trailing slash stripped (non-root)",
          canonicalizePathText("/home/x/mods/") == "/home/x/mods");
    check("root / preserved",
          canonicalizePathText("/") == "/");
}

// -- findModlistPathDrift ---

// All mods under the canonical root → no drift.
static void testAllUnderCanonical()
{
    std::cout << "testAllUnderCanonical\n";
    QList<SyncGuardInput> mods = {
        mod("A", "/home/jalcazo/Games/nerevarine_mods/Foo"),
        mod("B", "/home/jalcazo/Games/nerevarine_mods/Bar/Data"),
    };
    auto r = findModlistPathDrift(mods, {"/home/jalcazo/Games/nerevarine_mods"});
    check("no drift when every mod is under the canonical root",
          r.driftEntries.isEmpty() && r.totalModsChecked == 2);
    check("canonicalRoots echoed back",
          r.canonicalRoots.size() == 1
          && r.canonicalRoots.first() == "/home/jalcazo/Games/nerevarine_mods");
}

// A mod pointing outside the canonical root → drift.  This is the core
// cross-machine scenario the guard is for.
static void testPathOutsideCanonicalIsDrift()
{
    std::cout << "testPathOutsideCanonicalIsDrift\n";
    QList<SyncGuardInput> mods = {
        mod("Local",  "/home/jalcazo/Games/nerevarine_mods/A"),
        mod("USB",    "/mnt/usb/random/mods/B"),
        mod("OtherU", "/home/someone_else/mods/C"),
    };
    auto r = findModlistPathDrift(mods, {"/home/jalcazo/Games/nerevarine_mods"});
    check("two drift entries (USB + OtherU)",
          r.driftEntries.size() == 2,
          QString::number(r.driftEntries.size()));
    QStringList labels;
    for (const auto &d : r.driftEntries) labels << d.modLabel;
    check("USB flagged",    labels.contains("USB"));
    check("OtherU flagged", labels.contains("OtherU"));
    check("Local NOT flagged", !labels.contains("Local"));
}

// "/home/x/mods" must NOT accept "/home/x/mods_backup/..." - classic
// string-prefix gotcha that would silently pass bad paths.
static void testPrefixBoundaryIsEnforced()
{
    std::cout << "testPrefixBoundaryIsEnforced\n";
    QList<SyncGuardInput> mods = {
        mod("Sneaky", "/home/jalcazo/Games/nerevarine_mods_backup/Foo"),
    };
    auto r = findModlistPathDrift(mods, {"/home/jalcazo/Games/nerevarine_mods"});
    check("sibling directory NOT accepted as under canonical root",
          r.driftEntries.size() == 1);
}

// Multiple canonical roots (e.g. user keeps mods under two different
// mount points) → mods under EITHER root are clean.
static void testMultipleCanonicalRoots()
{
    std::cout << "testMultipleCanonicalRoots\n";
    QList<SyncGuardInput> mods = {
        mod("A", "/home/jalcazo/Games/nerevarine_mods/Foo"),
        mod("B", "/mnt/ssd/openmw_mods/Bar"),
        mod("C", "/tmp/random/Baz"),
    };
    auto r = findModlistPathDrift(mods, {
        "/home/jalcazo/Games/nerevarine_mods",
        "/mnt/ssd/openmw_mods",
    });
    check("exactly one drift (C)", r.driftEntries.size() == 1);
    if (!r.driftEntries.isEmpty()) {
        check("C is the one flagged",
              r.driftEntries.first().modLabel == "C");
    }
}

// Empty canonical roots → guard inactive.  Return empty drift, NOT flag
// everything.  The UI reads canonicalRoots to decide what message to show.
static void testEmptyCanonicalRootsInactive()
{
    std::cout << "testEmptyCanonicalRootsInactive\n";
    QList<SyncGuardInput> mods = {
        mod("A", "/anywhere/A"),
        mod("B", "/elsewhere/B"),
    };
    auto r = findModlistPathDrift(mods, {});
    check("no drift entries when guard inactive",
          r.driftEntries.isEmpty());
    check("canonicalRoots echo is empty",
          r.canonicalRoots.isEmpty());
    check("totalModsChecked still accurate",
          r.totalModsChecked == 2);
}

// Empty path on a mod row → always drift with a specific reason.  Points
// at a mod entry that probably got its ModPath cleared by a cancel flow
// and will silently drop out of openmw.cfg - worth surfacing even if the
// guard is disabled elsewhere.
static void testEmptyPathIsDrift()
{
    std::cout << "testEmptyPathIsDrift\n";
    QList<SyncGuardInput> mods = {
        mod("Ghost", QString()),
    };
    auto r = findModlistPathDrift(mods, {"/home/jalcazo/Games/nerevarine_mods"});
    check("one drift entry for empty path",
          r.driftEntries.size() == 1);
    if (!r.driftEntries.isEmpty()) {
        check("reason is 'path is empty'",
              r.driftEntries.first().reason == "path is empty",
              r.driftEntries.first().reason);
    }
}

// Trailing slashes on canonical roots shouldn't double-count / fail the
// boundary check.  Exercised because QDir paths in the app sometimes
// come through with trailing '/'.
static void testTrailingSlashesTolerated()
{
    std::cout << "testTrailingSlashesTolerated\n";
    QList<SyncGuardInput> mods = {
        mod("A", "/home/jalcazo/Games/nerevarine_mods/Foo/"),
    };
    auto r = findModlistPathDrift(mods, {"/home/jalcazo/Games/nerevarine_mods/"});
    check("trailing slashes on both root and path → no drift",
          r.driftEntries.isEmpty());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testCanonicalizePreservesAbsolute();
    testAllUnderCanonical();
    testPathOutsideCanonicalIsDrift();
    testPrefixBoundaryIsEnforced();
    testMultipleCanonicalRoots();
    testEmptyCanonicalRootsInactive();
    testEmptyPathIsDrift();
    testTrailingSlashesTolerated();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
