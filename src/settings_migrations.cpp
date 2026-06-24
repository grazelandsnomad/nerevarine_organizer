#include "settings_migrations.h"

#include <algorithm>

namespace settings {

QList<Migration> builtinMigrations()
{
    // v0 -> v1 baseline. No-op; stamps kVersionKey so future migrations
    // know they start from a known state. Append further migrations and
    // bump kCurrentVersion in settings_migrations.h.
    QList<Migration> list;
    Migration baseline;
    baseline.fromVersion = 0;
    baseline.toVersion   = 1;
    baseline.description = QStringLiteral("baseline (settings_version=1)");
    baseline.apply       = [](Store &) {};
    list << baseline;
    return list;
}

MigrationResult applyMigrations(Store &store,
                                const QList<Migration> &migrations,
                                int targetVersion)
{
    MigrationResult r;
    int current = store.value(kVersionKey, 0).toInt();
    r.storeVersionBefore = current;
    r.storeVersionAfter  = current;

    // Don't trust the registry to be sorted - the author might insert a
    // new migration in the middle of the list and forget to reorder.
    // Sort by fromVersion so we apply chain-of-upgrades top-down.
    QList<Migration> sorted = migrations;
    std::sort(sorted.begin(), sorted.end(),
              [](const Migration &a, const Migration &b) {
                  return a.fromVersion < b.fromVersion;
              });

    for (const Migration &m : sorted) {
        // Reject malformed entries up front so the iteration invariant
        // "toVersion is strictly greater than fromVersion" is enforced.
        if (m.toVersion <= m.fromVersion) continue;
        // Store is already past this migration's target - nothing to do.
        if (current >= m.toVersion)       continue;
        // Store is not yet at this migration's starting point - gap in
        // the chain.  Stop here; the user will need a build that
        // includes the bridge migration to proceed.
        if (current <  m.fromVersion)     break;
        // Caller-imposed ceiling (usually kCurrentVersion).  Never run
        // beyond what this build has tests for.
        if (m.toVersion > targetVersion)  break;
        // apply callable is required - skip silently if author left it
        // null rather than crashing an unrelated startup path.
        if (!m.apply)                     continue;

        m.apply(store);
        current = m.toVersion;
        // Persist the bump AFTER apply, so a crash mid-apply leaves us
        // at the previous version and the migration re-runs on the next
        // launch.  Migrations must be idempotent to survive that
        // possibility - common patterns (rename-if-present, remove-
        // stale, default-only-if-absent) are naturally idempotent.
        store.setValue(kVersionKey, current);
        r.appliedDescriptions << m.description;
    }
    r.storeVersionAfter = current;
    return r;
}

} // namespace settings
