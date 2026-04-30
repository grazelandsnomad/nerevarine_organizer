// tests/test_settings_migrations.cpp
//
// Unit tests for settings::applyMigrations against an in-memory mock Store.
// The goal is to pin the registry semantics - "run forward only, skip
// already-applied, stop at targetVersion, persist the version key after
// each step so a crash doesn't re-run work" - without touching a real
// QSettings file.

#include "settings_migrations.h"

#include <QCoreApplication>
#include <QMap>

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

// -- Mock store backed by a QMap ---

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

// Helper: build a rename-style migration from oldKey → newKey.
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

// -- Tests ---

// Empty store + empty migration list → no-op; version key remains absent.
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

// Fresh install, one v0→v1 migration, applied.
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

// Store already at v1 doesn't re-run v0→v1.
static void testAlreadyAtTargetNoOp()
{
    std::cout << "testAlreadyAtTargetNoOp\n";
    MockStore s;
    s.setValue(kVersionKey, 1);
    Migration baseline;
    baseline.fromVersion = 0;
    baseline.toVersion   = 1;
    baseline.description = QStringLiteral("baseline");
    // If apply runs, it leaves a sentinel we can assert on below.  The
    // test passes only when the entire apply lambda is skipped - so the
    // sentinel never gets set.
    baseline.apply = [](settings::Store &s) {
        s.setValue("should/not/be/set", "oops");
    };
    const auto r = applyMigrations(s, {baseline}, 1);
    check("version stays at 1", r.storeVersionAfter == 1);
    check("no migrations applied", r.appliedDescriptions.isEmpty());
    check("sentinel key NOT set (apply did not run)",
          !s.contains("should/not/be/set"));
}

// Chain of migrations v0→v1→v2 runs end-to-end from a fresh store.
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

// targetVersion caps how far we migrate - prevents running migrations a
// caller doesn't know about (e.g., policy/staged rollout).
static void testTargetVersionCap()
{
    std::cout << "testTargetVersionCap\n";
    MockStore s;
    QList<Migration> chain;
    chain << renameMigration(0, 1, "a", "b", "v0→v1");
    chain << renameMigration(1, 2, "b", "c", "v1→v2");
    chain << renameMigration(2, 3, "c", "d", "v2→v3");
    s.setValue("a", 42);

    // Cap at v2 - v2→v3 migration must be skipped.
    const auto r = applyMigrations(s, chain, 2);
    check("stopped at target version 2", r.storeVersionAfter == 2);
    check("two descriptions recorded",
          r.appliedDescriptions.size() == 2);
    check("key migrated exactly twice",
          s.contains("c") && !s.contains("a") && !s.contains("b") && !s.contains("d"),
          s.allKeys().join(","));
}

// Downgrade scenario: stored version > targetVersion → no-op.  The user
// ran a newer build that bumped the version, then opened an older build.
// We must NOT erase the newer bump or run stale "upgrade" logic.
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

// Malformed entry (toVersion <= fromVersion) is silently skipped - author
// error shouldn't crash user's startup.
static void testMalformedEntrySkipped()
{
    std::cout << "testMalformedEntrySkipped\n";
    MockStore s;
    QList<Migration> chain;
    Migration bad;
    bad.fromVersion = 2;
    bad.toVersion   = 1;  // backwards!
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

// Gap in the chain: v0→v1 and v3→v4 with nothing to bridge them.  Store
// ends up at v1 (the furthest reachable point).
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

// Each successful migration persists the version key immediately.  That
// way, if a LATER migration in the chain throws mid-apply (simulated here
// by an apply that partially writes then we pretend the loop died), the
// store's stored version reflects the last fully-applied migration, not
// a future one.  This asserts the "crash-safe" contract.
static void testVersionPersistedPerStep()
{
    std::cout << "testVersionPersistedPerStep\n";
    MockStore s;
    QList<Migration> chain;
    chain << renameMigration(0, 1, "a", "b");
    // A migration that RECORDS the version the store reports mid-apply.
    // If apply runs BEFORE setValue(kVersionKey, toVersion), the recorded
    // value is fromVersion - correct.  If apply runs AFTER, we'd see
    // toVersion and the contract would be violated.
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

// applyMigrations tolerates an entry with a null std::function apply -
// skips it rather than crashing.  Defensive against author oversight.
static void testNullApplyIsSkipped()
{
    std::cout << "testNullApplyIsSkipped\n";
    MockStore s;
    Migration broken;
    broken.fromVersion = 0;
    broken.toVersion   = 1;
    broken.description = QStringLiteral("null-apply");
    // broken.apply intentionally left default-constructed (empty)
    const auto r = applyMigrations(s, {broken}, 1);
    check("null apply skipped, version NOT bumped",
          r.storeVersionAfter == 0);
    check("no descriptions recorded",
          r.appliedDescriptions.isEmpty());
}

// -- builtinMigrations ---
// Sanity-check the shipped registry - mostly that it's self-consistent and
// carries the baseline migration the .cpp documents.
static void testBuiltinRegistrySelfConsistent()
{
    std::cout << "testBuiltinRegistrySelfConsistent\n";
    const auto migrations = settings::builtinMigrations();
    check("registry is non-empty (baseline exists)",
          !migrations.isEmpty());
    // Every entry has from < to.
    bool allWellFormed = true;
    for (const auto &m : migrations)
        if (m.toVersion <= m.fromVersion) allWellFormed = false;
    check("every registry entry has toVersion > fromVersion", allWellFormed);
    // The highest toVersion should match kCurrentVersion - otherwise
    // applyMigrations against a fresh store wouldn't reach kCurrentVersion.
    int maxTo = 0;
    for (const auto &m : migrations)
        if (m.toVersion > maxTo) maxTo = m.toVersion;
    check("max toVersion == kCurrentVersion",
          maxTo == settings::kCurrentVersion,
          QString::number(maxTo));
}

// Fresh install run against the shipped registry reaches kCurrentVersion.
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

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

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
