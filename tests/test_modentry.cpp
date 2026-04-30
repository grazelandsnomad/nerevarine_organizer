// tests/test_modentry.cpp
//
// Unit tests for the ModEntry value type.  The whole point of introducing
// ModEntry was to make mod-list business logic - sorting in particular -
// testable without a QListWidget standing up in the harness.  This test
// links against QtCore only: if that invariant breaks (e.g. someone pulls
// QListWidgetItem into modentry.cpp), the test refuses to link and the
// refactor has quietly regressed.
//
// Build with CMake (see tests/CMakeLists.txt) and run directly:
//   ./build/tests/test_modentry

#include "modentry.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <iostream>
#include <vector>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok)
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name << "\n";
        ++s_failed;
    }
}

// Tiny builders so each test case reads as a readable data description
// instead of fifteen lines of field assignments.

static ModEntry mod(const QString &name, qint64 size = 0, const QDateTime &added = {})
{
    ModEntry e;
    e.itemType    = QStringLiteral("mod");
    e.displayName = name;
    e.modSize     = size;
    e.dateAdded   = added;
    return e;
}

static ModEntry separator(const QString &title)
{
    ModEntry e;
    e.itemType    = QStringLiteral("separator");
    e.displayName = title;
    return e;
}

static QStringList names(const std::vector<ModEntry> &v)
{
    QStringList out;
    out.reserve(int(v.size()));
    for (const auto &e : v) out << e.displayName;
    return out;
}

int main()
{
    std::cout << "=== ModEntry tests ===\n";

    // ---
    // Defaults: a zero-initialised ModEntry is a "mod" row, unchecked,
    // everything else zero/empty.  This is the contract new call sites
    // rely on when they copy-and-modify a ModEntry.
    // ---
    {
        ModEntry e;
        check("default itemType is \"mod\"",   e.itemType == QStringLiteral("mod"));
        check("default isMod() true",           e.isMod());
        check("default isSeparator() false",    !e.isSeparator());
        check("default not checked",            !e.checked);
        check("default modSize is 0",           e.modSize == 0);
        check("default dateAdded invalid",      !e.dateAdded.isValid());
        check("default dependsOn empty",        e.dependsOn.isEmpty());
    }

    // ---
    // effectiveName prefers customName when set; falls back to the raw
    // displayName otherwise (matches the ModRole::CustomName rule).
    // ---
    {
        ModEntry e = mod("raw_folder_name");
        check("effectiveName falls back to displayName",
              e.effectiveName() == QStringLiteral("raw_folder_name"));

        e.customName = QStringLiteral("Pretty Name");
        check("effectiveName uses customName when set",
              e.effectiveName() == QStringLiteral("Pretty Name"));

        e.customName.clear();
        check("effectiveName falls back again when customName cleared",
              e.effectiveName() == QStringLiteral("raw_folder_name"));
    }

    // ---
    // Equality: defaulted operator== compares memberwise.  Two entries
    // built with the same fields compare equal; flipping any single
    // field breaks equality.
    // ---
    {
        ModEntry a = mod("Foo", 1024);
        ModEntry b = mod("Foo", 1024);
        check("memberwise equality",            a == b);

        b.annotation = QStringLiteral("bumped");
        check("differing annotation breaks eq", !(a == b));
    }

    // ---
    // lessByDisplayName: case-insensitive ascending, separators last.
    // ---
    {
        std::vector<ModEntry> v = {
            mod("Zeta"),
            separator("---- UI ----"),
            mod("alpha"),
            mod("Beta"),
        };
        std::stable_sort(v.begin(), v.end(), lessByDisplayName);
        check("by name: case-insensitive asc, separators last",
              names(v) == QStringList{"alpha", "Beta", "Zeta", "---- UI ----"});
    }

    // ---
    // lessByModSize: ascending, zero-size entries and separators trail.
    // Mirrors the UI's "unknown size at the end" rule.
    // ---
    {
        std::vector<ModEntry> v = {
            mod("Big",      10'000'000),
            mod("Unknown",  0),
            mod("Small",    1'024),
            separator("---- section ----"),
            mod("Medium",   500'000),
        };
        std::stable_sort(v.begin(), v.end(), lessByModSize);
        check("by size: asc, 0-size and separators trail",
              names(v) == QStringList{"Small", "Medium", "Big", "Unknown", "---- section ----"});
    }

    // ---
    // lessByDateAdded: ascending, invalid dates and separators trail.
    // ---
    {
        const QDateTime t1 = QDateTime::fromString("2024-01-01T00:00:00", Qt::ISODate);
        const QDateTime t2 = QDateTime::fromString("2025-06-15T12:00:00", Qt::ISODate);
        const QDateTime t3 = QDateTime::fromString("2026-04-17T08:30:00", Qt::ISODate);

        std::vector<ModEntry> v = {
            mod("newest",   0, t3),
            mod("nodate",   0, QDateTime{}),
            separator("---- section ----"),
            mod("oldest",   0, t1),
            mod("middle",   0, t2),
        };
        std::stable_sort(v.begin(), v.end(), lessByDateAdded);
        check("by date: asc, invalid dates and separators trail",
              names(v) == QStringList{"oldest", "middle", "newest", "nodate", "---- section ----"});
    }

    // ---
    // Sort comparators are strict weak orderings: for any a, !(a<a).
    // A sanity check that catches accidental off-by-one reflexivity bugs.
    // ---
    {
        const ModEntry m = mod("x", 1234, QDateTime::fromString("2026-01-01", Qt::ISODate));
        const ModEntry s = separator("section");
        bool reflexive = lessByDisplayName(m, m) || lessByDisplayName(s, s)
                      || lessByModSize    (m, m) || lessByModSize    (s, s)
                      || lessByDateAdded  (m, m) || lessByDateAdded  (s, s);
        check("comparators are irreflexive", !reflexive);
    }

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
