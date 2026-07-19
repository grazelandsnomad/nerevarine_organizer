// settings::applyMigrations semantics + ForbiddenModsRegistry per-game storage
// and legacy-file migration. One QCoreApplication, two suites.

#include "settings_migrations.h"
#include "forbidden_mods.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <iostream>

#include "test_harness.h"

// --- settings_migrations ---

// Mock store backed by a QMap.
class MockStore final : public settings::Store {
public:
    QVariant value(const QString &key, const QVariant &def = {}) const override
    {
        auto it = m_data.find(key);
        return it == m_data.constEnd() ? def : *it;
    }
    void setValue(const QString &key, const QVariant &v) override
    {
        m_data.insert(key, v);
    }
    void remove(const QString &key) override
    {
        m_data.remove(key);
    }
    bool contains(const QString &key) const override
    {
        return m_data.contains(key);
    }
    QStringList allKeys() const override
    {
        return m_data.keys();
    }
    int size() const { return m_data.size(); }

private:
    QMap<QString, QVariant> m_data;
};

using settings::Migration;
using settings::applyMigrations;
using settings::kVersionKey;

// Build a rename-style migration oldKey -> newKey.
static Migration renameMigration(int from, int to,
                                 const QString &oldKey, const QString &newKey,
                                 const QString &desc = {})
{
    Migration m;
    m.fromVersion = from;
    m.toVersion   = to;
    m.description = desc.isEmpty()
        ? QString("rename %1 → %2").arg(oldKey, newKey)
        : desc;
    m.apply = [oldKey, newKey](settings::Store &s) {
        if (!s.contains(oldKey)) return;
        s.setValue(newKey, s.value(oldKey));
        s.remove(oldKey);
    };
    return m;
}

// Empty store + empty list: no-op, version key stays absent.
static void testNoMigrationsNoChange()
{
    std::cout << "testNoMigrationsNoChange\n";
    MockStore s;
    const auto r = applyMigrations(s, {}, 5);
    check("before = 0", r.storeVersionBefore == 0);
    check("after = 0",  r.storeVersionAfter  == 0);
    check("no descriptions recorded", r.appliedDescriptions.isEmpty());
    check("store untouched (version key absent)",
          !s.contains(kVersionKey));
}

// Fresh install runs the single v0->v1 migration.
static void testFreshInstallAppliesBaseline()
{
    std::cout << "testFreshInstallAppliesBaseline\n";
    MockStore s;
    Migration baseline;
    baseline.fromVersion = 0;
    baseline.toVersion   = 1;
    baseline.description = QStringLiteral("baseline");
    baseline.apply = [](settings::Store &) {};
    const auto r = applyMigrations(s, {baseline}, 1);
    check("before = 0",  r.storeVersionBefore == 0);
    check("after  = 1",  r.storeVersionAfter  == 1);
    check("one migration applied", r.appliedDescriptions.size() == 1);
    check("version key persisted", s.value(kVersionKey).toInt() == 1);
}

// Store already at v1 must not re-run v0->v1.
static void testAlreadyAtTargetNoOp()
{
    std::cout << "testAlreadyAtTargetNoOp\n";
    MockStore s;
    s.setValue(kVersionKey, 1);
    Migration baseline;
    baseline.fromVersion = 0;
    baseline.toVersion   = 1;
    baseline.description = QStringLiteral("baseline");
    // If apply runs it sets a sentinel; the test passes only when apply is
    // skipped entirely and the sentinel stays unset.
    baseline.apply = [](settings::Store &s) {
        s.setValue("should/not/be/set", "oops");
    };
    const auto r = applyMigrations(s, {baseline}, 1);
    check("version stays at 1", r.storeVersionAfter == 1);
    check("no migrations applied", r.appliedDescriptions.isEmpty());
    check("sentinel key NOT set (apply did not run)",
          !s.contains("should/not/be/set"));
}

// v0->v1->v2 chain runs end-to-end from a fresh store.
static void testChainAppliesInOrder()
{
    std::cout << "testChainAppliesInOrder\n";
    MockStore s;
    s.setValue("ui/dark_mode", true);
    s.setValue("loot/banner_disabled", false);

    QList<Migration> chain;
    chain << renameMigration(0, 1, "ui/dark_mode", "ui/theme_legacy");
    Migration m2;
    m2.fromVersion = 1;
    m2.toVersion   = 2;
    m2.description = QStringLiteral("translate ui/theme_legacy → ui/theme");
    m2.apply = [](settings::Store &s) {
        if (!s.contains("ui/theme_legacy")) return;
        const bool dark = s.value("ui/theme_legacy").toBool();
        s.setValue("ui/theme", dark ? "dark" : "light");
        s.remove("ui/theme_legacy");
    };
    chain << m2;

    const auto r = applyMigrations(s, chain, 2);
    check("both migrations applied in order",
          r.appliedDescriptions.size() == 2);
    check("final version = 2", s.value(kVersionKey).toInt() == 2);
    check("old key removed",   !s.contains("ui/dark_mode"));
    check("intermediate key removed", !s.contains("ui/theme_legacy"));
    check("final key set to 'dark'",
          s.value("ui/theme").toString() == "dark",
          s.value("ui/theme").toString());
}

// targetVersion caps how far we migrate, so a caller never runs migrations it
// doesn't know about (staged rollout).
static void testTargetVersionCap()
{
    std::cout << "testTargetVersionCap\n";
    MockStore s;
    QList<Migration> chain;
    chain << renameMigration(0, 1, "a", "b", "v0→v1");
    chain << renameMigration(1, 2, "b", "c", "v1→v2");
    chain << renameMigration(2, 3, "c", "d", "v2→v3");
    s.setValue("a", 42);

    // Cap at v2: the v2->v3 migration must be skipped.
    const auto r = applyMigrations(s, chain, 2);
    check("stopped at target version 2", r.storeVersionAfter == 2);
    check("two descriptions recorded",
          r.appliedDescriptions.size() == 2);
    check("key migrated exactly twice",
          s.contains("c") && !s.contains("a") && !s.contains("b") && !s.contains("d"),
          s.allKeys().join(","));
}

// Downgrade: stored version > targetVersion is a no-op. User ran a newer build
// that bumped the version, then opened an older one; don't erase the bump or
// run stale upgrade logic.
static void testDowngradeIsNoOp()
{
    std::cout << "testDowngradeIsNoOp\n";
    MockStore s;
    s.setValue(kVersionKey, 99);
    QList<Migration> chain;
    chain << renameMigration(0, 1, "a", "b");
    chain << renameMigration(1, 2, "b", "c");
    const auto r = applyMigrations(s, chain, 2);
    check("version untouched", s.value(kVersionKey).toInt() == 99);
    check("no migrations applied", r.appliedDescriptions.isEmpty());
    check("before == after in report",
          r.storeVersionBefore == 99 && r.storeVersionAfter == 99);
}

// Malformed entry (toVersion <= fromVersion) is skipped: an author mistake
// shouldn't crash startup.
static void testMalformedEntrySkipped()
{
    std::cout << "testMalformedEntrySkipped\n";
    MockStore s;
    QList<Migration> chain;
    Migration bad;
    bad.fromVersion = 2;
    bad.toVersion   = 1;  // backwards
    bad.description = QStringLiteral("broken");
    bad.apply = [](settings::Store &s) { s.setValue("side/effect", true); };
    chain << bad;
    chain << renameMigration(0, 1, "a", "b");
    s.setValue("a", 7);
    const auto r = applyMigrations(s, chain, 1);
    check("broken entry produced no side effect",
          !s.contains("side/effect"));
    check("valid entry still ran", s.contains("b"));
    check("final version = 1", s.value(kVersionKey).toInt() == 1);
}

// Gap in the chain: v0->v1 and v3->v4 with nothing bridging them. Store stops
// at v1, the furthest reachable point.
static void testGapStopsAtReachableVersion()
{
    std::cout << "testGapStopsAtReachableVersion\n";
    MockStore s;
    QList<Migration> chain;
    chain << renameMigration(0, 1, "a", "b");
    chain << renameMigration(3, 4, "c", "d", "v3→v4 (unreachable)");
    const auto r = applyMigrations(s, chain, 4);
    check("reached v1, NOT v4", r.storeVersionAfter == 1,
          QString::number(r.storeVersionAfter));
    check("exactly one migration applied",
          r.appliedDescriptions.size() == 1);
}

// Crash-safe contract: each migration persists the version key immediately, so
// if a later one throws mid-apply the stored version reflects the last
// fully-applied step, never a future one.
static void testVersionPersistedPerStep()
{
    std::cout << "testVersionPersistedPerStep\n";
    MockStore s;
    QList<Migration> chain;
    chain << renameMigration(0, 1, "a", "b");
    // Records the version the store reports mid-apply. apply must run before
    // setValue(kVersionKey, toVersion), so we expect fromVersion here; seeing
    // toVersion would mean the contract is broken.
    int observedInsideApply = -1;
    Migration spy;
    spy.fromVersion = 1;
    spy.toVersion   = 2;
    spy.description = QStringLiteral("spy");
    spy.apply = [&](settings::Store &store) {
        observedInsideApply = store.value(settings::kVersionKey, -1).toInt();
    };
    chain << spy;
    applyMigrations(s, chain, 2);
    check("spy saw the PREVIOUS version while applying",
          observedInsideApply == 1,
          QString::number(observedInsideApply));
}

// A null std::function apply is skipped, not crashed on.
static void testNullApplyIsSkipped()
{
    std::cout << "testNullApplyIsSkipped\n";
    MockStore s;
    Migration broken;
    broken.fromVersion = 0;
    broken.toVersion   = 1;
    broken.description = QStringLiteral("null-apply");
    // broken.apply left default-constructed (empty)
    const auto r = applyMigrations(s, {broken}, 1);
    check("null apply skipped, version NOT bumped",
          r.storeVersionAfter == 0);
    check("no descriptions recorded",
          r.appliedDescriptions.isEmpty());
}

// Shipped registry is self-consistent and carries the baseline migration.
static void testBuiltinRegistrySelfConsistent()
{
    std::cout << "testBuiltinRegistrySelfConsistent\n";
    const auto migrations = settings::builtinMigrations();
    check("registry is non-empty (baseline exists)",
          !migrations.isEmpty());
    bool allWellFormed = true;
    for (const auto &m : migrations)
        if (m.toVersion <= m.fromVersion) allWellFormed = false;
    check("every registry entry has toVersion > fromVersion", allWellFormed);
    // Highest toVersion must equal kCurrentVersion, else a fresh store never
    // reaches it.
    int maxTo = 0;
    for (const auto &m : migrations)
        if (m.toVersion > maxTo) maxTo = m.toVersion;
    check("max toVersion == kCurrentVersion",
          maxTo == settings::kCurrentVersion,
          QString::number(maxTo));
}

// Fresh install against the shipped registry reaches kCurrentVersion.
static void testFreshInstallReachesCurrentVersion()
{
    std::cout << "testFreshInstallReachesCurrentVersion\n";
    MockStore s;
    const auto r = applyMigrations(
        s, settings::builtinMigrations(), settings::kCurrentVersion);
    check("fresh install lands at kCurrentVersion",
          r.storeVersionAfter == settings::kCurrentVersion,
          QString::number(r.storeVersionAfter));
}

static void run_settings_migrations()
{
    testNoMigrationsNoChange();
    testFreshInstallAppliesBaseline();
    testAlreadyAtTargetNoOp();
    testChainAppliesInOrder();
    testTargetVersionCap();
    testDowngradeIsNoOp();
    testMalformedEntrySkipped();
    testGapStopsAtReachableVersion();
    testVersionPersistedPerStep();
    testNullApplyIsSkipped();
    testBuiltinRegistrySelfConsistent();
    testFreshInstallReachesCurrentVersion();
}

// --- forbidden_mods ---

// Two Morrowind entries shaped like the shipped legacy file.
static void writeLegacy(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);
    out << "The Wabbajack\thttps://www.nexusmods.com/morrowind/mods/44653\tneeds MWSE\n";
    out << "Glowbugs\thttps://www.nexusmods.com/morrowind/mods/50538\tno OpenMW\n";
}

static void run_forbidden_mods()
{
    std::cout << "ForbiddenModsRegistry per-game + migration\n";

    // 1. Legacy file migrates into the morrowind game.
    {
        QTemporaryDir dir;
        const QString legacy = dir.filePath("forbidden_mods.txt");
        const QString mw     = dir.filePath("forbidden_mods_morrowind.txt");
        writeLegacy(legacy);

        ForbiddenModsRegistry reg;
        reg.reload(mw, "morrowind", legacy);

        check("morrowind inherits the 2 legacy entries", reg.size() == 2,
              QString::number(reg.size()));
        check("per-game morrowind file now exists", QFile::exists(mw));
        check("legacy file consumed by the migration", !QFile::exists(legacy));
    }

    // 2. Oblivion starts empty and does not inherit Morrowind's list.
    {
        QTemporaryDir dir;
        const QString legacy = dir.filePath("forbidden_mods.txt");
        const QString obl    = dir.filePath("forbidden_mods_oblivion.txt");
        writeLegacy(legacy);  // present but un-migrated

        ForbiddenModsRegistry reg;
        reg.reload(obl, "oblivion", legacy);

        check("oblivion forbidden list is empty", reg.size() == 0,
              QString::number(reg.size()));
        check("oblivion file created", QFile::exists(obl));
        check("legacy file left intact for morrowind to claim later",
              QFile::exists(legacy));
    }

    // 3. An existing per-game file is never clobbered by migration.
    {
        QTemporaryDir dir;
        const QString legacy = dir.filePath("forbidden_mods.txt");
        const QString mw     = dir.filePath("forbidden_mods_morrowind.txt");
        writeLegacy(legacy);  // 2 entries
        {   // pre-existing morrowind file with one different entry
            QFile f(mw);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                QTextStream(&f) << "OnlyOne\thttps://www.nexusmods.com/morrowind/mods/1\tx\n";
        }

        ForbiddenModsRegistry reg;
        reg.reload(mw, "morrowind", legacy);

        check("existing per-game file wins over legacy migration", reg.size() == 1,
              QString::number(reg.size()));
        check("legacy untouched when per-game file already exists",
              QFile::exists(legacy));
    }

    // 4. find() stays scoped to the Nexus game slug in the URL.
    {
        QTemporaryDir dir;
        const QString mw = dir.filePath("forbidden_mods_morrowind.txt");
        writeLegacy(mw);  // already a per-game file, no legacy migration

        ForbiddenModsRegistry reg;
        reg.reload(mw, "morrowind", QString());

        check("find matches the morrowind mod id",
              reg.find("morrowind", 44653) != nullptr);
        check("find does NOT match the same id under another game",
              reg.find("oblivion", 44653) == nullptr);
        check("find misses an unknown id", reg.find("morrowind", 999999) == nullptr);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("nerevarine-test");
    QCoreApplication::setApplicationName("forbidden-mods-test");
    QStandardPaths::setTestModeEnabled(true);  // isolate QSettings access

    run_settings_migrations();
    run_forbidden_mods();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
