#pragma once

// settings_migrations - one-shot rewrites of QSettings keys on version bumps.
//
// Purpose: QSettings keys accumulate across refactors.  When a key is
// renamed ("ui/dark_mode" → "ui/theme") or removed ("loot/banner_disabled"
// folded into "loot/" group), the old key sits in the user's config file
// forever and a future reader still sees it.  A migration step on startup,
// gated on a version counter, cleans the slate deterministically.
//
// Design:
//   · Store abstraction - migrations act against a small key/value
//     interface, not QSettings directly.  Lets tests run against an
//     in-memory MockStore and assert on the result without touching the
//     user's real config file.
//   · Migration entries - (fromVersion → toVersion, description, apply
//     callable).  Registered in builtinMigrations() in the .cpp.
//   · applyMigrations walks the list in version order, running each one
//     whose toVersion is still ahead of the stored version, and bumps the
//     version key after each successful apply so a crash between steps
//     doesn't double-run.
//
// No Qt Widgets, no filesystem I/O beyond whatever the concrete Store
// impl chooses.  Same "pure" pattern as openmwconfigwriter.h and friends.

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>

namespace settings {

// Key under which we track "what version of the settings schema has been
// applied to this Store."  Stored as an integer.  Absent == 0 (fresh
// install, every migration is eligible).
inline constexpr auto kVersionKey = "meta/settings_version";

// Minimal key/value surface migrations need.  Sufficient for rename /
// remove / default-reset patterns.  Intentionally smaller than QSettings's
// API - migrations shouldn't be writing groups, syncing, or reading
// platform-specific bits.
struct Store {
    virtual ~Store() = default;
    virtual QVariant    value(const QString &key, const QVariant &def = {}) const = 0;
    virtual void        setValue(const QString &key, const QVariant &v) = 0;
    virtual void        remove(const QString &key) = 0;
    virtual bool        contains(const QString &key) const = 0;
    // Every key known to the store, recursive.  Used for prefix-based
    // migrations (e.g. "rename every games/*/loot_flag to loot/*_flag").
    virtual QStringList allKeys() const = 0;
};

struct Migration {
    int     fromVersion;       // version the store must be at before this runs
    int     toVersion;         // version the store will be at after this runs
    QString description;       // short human-readable, shown in the session log
    std::function<void(Store&)> apply;
};

struct MigrationResult {
    int         storeVersionBefore = 0;
    int         storeVersionAfter  = 0;
    QStringList appliedDescriptions;   // in order of application
};

// Run every registered migration that advances the store's stored version,
// capped at `targetVersion` (typically kCurrentVersion below).  The
// version key is written after each successful apply so a crash mid-
// sequence leaves the store at a known-good step, not halfway through
// the NEXT migration.
//
// If the store's stored version is GREATER than targetVersion (the user
// downgraded the app), applyMigrations is a safe no-op - migrations only
// run forward.
MigrationResult applyMigrations(Store &store,
                                const QList<Migration> &migrations,
                                int targetVersion);

// The list this build ships.  Lives in settings_migrations.cpp so each
// migration has one reviewable unit + a corresponding test case.  Callers
// get a by-value copy so they can't mutate the registry.
QList<Migration> builtinMigrations();

// The schema version this build of the app targets.  Bump by +1 every
// time a new Migration is appended to builtinMigrations().  Never reuse
// a retired number - migrations are one-way.
inline constexpr int kCurrentVersion = 1;

} // namespace settings
