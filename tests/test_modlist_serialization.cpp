// tests/test_modlist_serialization.cpp
//
// Round-trip tests for the tab-delimited modlist format written by
// saveModList() and read back by loadModList() in mainwindow.cpp.
//
// The format is one line per entry.  Mod lines start with "+ " (enabled)
// or "- " (disabled); the rest is tab-separated fields:
//
//   [+/-] parts[0]\tparts[1]\tparts[2]\tparts[3]\tparts[4]\tparts[5]\tparts[6]\tparts[7]\tparts[8]\tparts[9]
//          modPath   custName  annot    url       dateISO   reserved  deps      update    utility   favorite
//
// Separator lines start with "# " and carry colour/collapse metadata.
// Mid-install placeholder lines use "- " with only four fields.
//
// These tests do NOT require QListWidget or any Qt widget - they operate
// purely at the text level, mirroring the exact save/load logic.
//
// Build + run:
//   cmake --build build && ./build/tests/test_modlist_serialization

#include "annotation_codec.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok, const QString &detail = {})
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name << "\n";
        if (!detail.isEmpty())
            std::cout << "    got: " << detail.toStdString() << "\n";
        ++s_failed;
    }
}

// -- Mirrors the save logic in saveModList() ---

struct ModFields {
    bool        enabled       = true;
    QString     modPath;
    QString     custName;
    QString     annot;
    QString     url;
    QDateTime   dateAdded;
    QStringList deps;
    bool        updateAvailable = false;
    bool        isUtility       = false;
    bool        isFavorite      = false;
    QString     fomodChoices;   // parts[10]: "si:gi:pi;..." or empty
};

static QString serialise(const ModFields &m)
{
    QChar   prefix     = m.enabled ? '+' : '-';
    QString dateStr    = m.dateAdded.toString(Qt::ISODate);
    QString depsStr    = m.deps.join(',');
    int     updateFlag  = m.updateAvailable ? 1 : 0;
    int     utilityFlag = m.isUtility       ? 1 : 0;
    int     favoriteFlag= m.isFavorite      ? 1 : 0;

    return QString("%1 %2\t%3\t%4\t%5\t%6\t\t%7\t%8\t%9\t%10\t%11")
        .arg(prefix)
        .arg(m.modPath,
             m.custName,
             encodeAnnot(m.annot),
             m.url,
             dateStr,
             depsStr)
        .arg(updateFlag)
        .arg(utilityFlag)
        .arg(favoriteFlag)
        .arg(m.fomodChoices);
}

// -- Mirrors the parse logic in loadModList() ---

static ModFields parse(const QString &line)
{
    ModFields out;
    if (line.size() < 2) return out;

    out.enabled           = (line[0] == '+');
    QStringList parts     = line.mid(2).split('\t');
    out.modPath           = parts[0];
    out.custName          = parts.size() > 1 ? parts[1] : QString();
    out.annot             = parts.size() > 2 ? decodeAnnot(parts[2]) : QString();
    out.url               = parts.size() > 3 ? parts[3] : QString();
    out.dateAdded         = parts.size() > 4
                            ? QDateTime::fromString(parts[4], Qt::ISODate)
                            : QDateTime();
    // parts[5]: reserved (was endorsement state) - skipped
    out.deps              = parts.size() > 6
                            ? parts[6].split(',', Qt::SkipEmptyParts)
                            : QStringList();
    out.updateAvailable   = parts.size() > 7 && parts[7].toInt() == 1;
    out.isUtility         = parts.size() > 8 && parts[8].toInt() == 1;
    out.isFavorite        = parts.size() > 9 && parts[9].toInt() == 1;
    // parts[10]: serialized FOMOD install choices.  Missing → empty.
    out.fomodChoices      = parts.size() > 10 ? parts[10] : QString();
    return out;
}

// -- Tests ---

static void testColumnLayout()
{
    std::cout << "\n-- column layout --\n";

    ModFields m;
    m.modPath   = "/home/user/mods/MyMod";
    m.custName  = "My Mod";
    m.annot     = "a note";
    m.url       = "https://www.nexusmods.com/morrowind/mods/12345";
    m.dateAdded = QDateTime::fromString("2026-01-15T10:00:00", Qt::ISODate);
    m.deps      = {"https://www.nexusmods.com/morrowind/mods/99"};
    m.updateAvailable = true;
    m.isUtility       = false;
    m.isFavorite      = true;
    m.fomodChoices    = "0:0:1;0:1:0";

    QString line       = serialise(m);
    QStringList parts  = line.mid(2).split('\t');

    check("line starts with '+ '",         line.startsWith("+ "));
    check("11 tab-separated parts present", parts.size() == 11,
          QString("got %1 parts").arg(parts.size()));
    check("parts[0] = modPath",             parts[0] == m.modPath);
    check("parts[1] = custName",            parts[1] == m.custName);
    check("parts[3] = url",                 parts[3] == m.url);
    check("parts[5] is empty (reserved)",   parts[5].isEmpty());
    check("parts[7] = updateFlag (1)",      parts[7] == "1");
    check("parts[8] = utilityFlag (0)",     parts[8] == "0");
    check("parts[9] = favoriteFlag (1)",    parts[9] == "1");
    check("parts[10] = fomodChoices",       parts[10] == m.fomodChoices);
}

static void testEnabledDisabled()
{
    std::cout << "\n-- enabled / disabled prefix --\n";

    ModFields on;
    on.modPath   = "/mods/Mod";
    on.enabled   = true;
    check("enabled serialises as '+ '",  serialise(on).startsWith("+ "));
    check("enabled round-trips",          parse(serialise(on)).enabled == true);

    ModFields off;
    off.modPath  = "/mods/Mod";
    off.enabled  = false;
    check("disabled serialises as '- '", serialise(off).startsWith("- "));
    check("disabled round-trips",         parse(serialise(off)).enabled == false);
}

static void testFlagCombinations()
{
    std::cout << "\n-- utility / favorite flag combinations --\n";

    struct Case { bool util; bool fav; const char *label; };
    const Case cases[] = {
        { false, false, "util=0 fav=0" },
        { false, true,  "util=0 fav=1" },
        { true,  false, "util=1 fav=0" },
        { true,  true,  "util=1 fav=1" },
    };

    for (const auto &c : cases) {
        ModFields m;
        m.modPath   = "/mods/X";
        m.isUtility = c.util;
        m.isFavorite= c.fav;

        ModFields got = parse(serialise(m));

        QString label = QString("%1 - isUtility").arg(c.label);
        check(label.toUtf8().constData(), got.isUtility == c.util);

        label = QString("%1 - isFavorite").arg(c.label);
        check(label.toUtf8().constData(), got.isFavorite == c.fav);
    }
}

static void testBackwardCompatibility()
{
    std::cout << "\n-- backward compatibility (older files with fewer columns) --\n";

    // A line written by an older version that predates the utility column (col 8).
    // Format: "+ modPath\tcustName\tannot\turl\tdate\t\tdeps\tupdateFlag"
    QString legacyLine = "+ /mods/OldMod\tOld Mod\t\t\t\t\t\t0";
    ModFields got = parse(legacyLine);
    check("pre-utility line: isUtility defaults false",  !got.isUtility);
    check("pre-utility line: isFavorite defaults false", !got.isFavorite);

    // A line written before the favorite column (has utility but not favorite).
    QString preFavLine = "+ /mods/UtilMod\tUtil\t\t\t\t\t\t0\t1";
    got = parse(preFavLine);
    check("pre-favorite line: isUtility reads correctly", got.isUtility == true);
    check("pre-favorite line: isFavorite defaults false", !got.isFavorite);

    // A line written before the fomodChoices column.
    QString preFomodLine = "+ /mods/FomodMod\tFomod\t\t\t\t\t\t0\t0\t1";
    got = parse(preFomodLine);
    check("pre-fomodChoices line: isFavorite reads correctly", got.isFavorite == true);
    check("pre-fomodChoices line: fomodChoices defaults empty", got.fomodChoices.isEmpty());
}

static void testAnnotationRoundTrip()
{
    std::cout << "\n-- annotation encoding inside modlist line --\n";

    // Annotations with special chars must survive the tab-delimited line format.
    ModFields m;
    m.modPath = "/mods/A";
    m.annot   = "line1\nline2\twith\ttabs\\and\\backslashes";
    ModFields got = parse(serialise(m));
    check("multiline annotation round-trips through modlist format",
          got.annot == m.annot, got.annot);
}

static void testDateRoundTrip()
{
    std::cout << "\n-- date round-trip --\n";

    ModFields m;
    m.modPath   = "/mods/B";
    m.dateAdded = QDateTime::fromString("2026-03-07T14:23:00", Qt::ISODate);
    ModFields got = parse(serialise(m));
    check("dateAdded round-trips as ISO string", got.dateAdded == m.dateAdded,
          got.dateAdded.toString(Qt::ISODate));
}

static void testDepsRoundTrip()
{
    std::cout << "\n-- dependency list round-trip --\n";

    ModFields m;
    m.modPath = "/mods/C";
    m.deps    = {
        "https://www.nexusmods.com/morrowind/mods/100",
        "https://www.nexusmods.com/morrowind/mods/200",
    };
    ModFields got = parse(serialise(m));
    check("deps list round-trips",       got.deps == m.deps);
    check("deps count preserved",        got.deps.size() == 2);

    // Empty deps list must not produce a spurious empty entry.
    ModFields empty;
    empty.modPath = "/mods/D";
    ModFields gotEmpty = parse(serialise(empty));
    check("empty deps stays empty",      gotEmpty.deps.isEmpty());
}

static void testSeparatorLine()
{
    std::cout << "\n-- separator line format --\n";

    // Mirrors the save logic for separator lines.
    QString sepLine = "# My Section"
                      " <color>#ff1a237e</color>"
                      "<fgcolor>#ffffffff</fgcolor>"
                      "<collapsed>1</collapsed>";

    check("separator starts with '# '",       sepLine.startsWith("# "));
    check("separator has <color> tag",         sepLine.contains("<color>"));
    check("separator has <fgcolor> tag",       sepLine.contains("<fgcolor>"));
    check("separator carries collapsed state", sepLine.contains("<collapsed>1</collapsed>"));
    // Separator lines must NOT be parsed as mod lines.
    check("separator not parsed as mod line",  !(sepLine.size() >= 2
                                                 && (sepLine[0] == '+' || sepLine[0] == '-')
                                                 && sepLine[1] == ' '));
}

// Tests the loadModList install-status logic: a path that exists but whose
// directory is empty must be treated as not-installed.  An empty folder
// fools the old QDir::exists() check but has nothing in it for
// collectDataFolders to find, causing false "installed" badges and missing-
// master warnings that cannot be resolved.
static void testInstallStatusEmptyDir()
{
    std::cout << "\n-- install-status: empty directory treated as not-installed --\n";

    // Mirror the fixed loadModList logic.
    auto computeInstalled = [](const QString &modPath) -> bool {
        QDir d(modPath);
        return !modPath.isEmpty() && d.exists() && !d.isEmpty();
    };

    // Non-existent path → not installed.
    check("missing path → not installed",
          !computeInstalled("/nonexistent/path/that/does/not/exist"));

    // Empty string → not installed.
    check("empty modPath → not installed",
          !computeInstalled(QString()));

    // A real directory that definitely exists but is empty: use /proc/self/fd
    // (always present on Linux, usually has only a few numbered entries).
    // We need a truly empty dir - use QTemporaryDir instead.
    {
        QTemporaryDir tmp;
        bool exists  = QDir(tmp.path()).exists();
        bool isEmpty = QDir(tmp.path()).isEmpty();
        check("QTemporaryDir exists",    exists);
        check("QTemporaryDir is empty",  isEmpty);
        check("empty tmpdir → not installed",
              !computeInstalled(tmp.path()));

        // Write one file into it - now it should count as installed.
        {
            QFile f(tmp.path() + "/OAAB_Data.esm");
            (void)f.open(QIODevice::WriteOnly);
            f.write("dummy");
        }
        check("non-empty tmpdir → installed",
              computeInstalled(tmp.path()));
    }
}

static void testFomodChoicesRoundTrip()
{
    std::cout << "\n-- FOMOD choices round-trip --\n";

    ModFields m;
    m.modPath      = "/mods/FomodMod";
    m.fomodChoices = "0:0:1;0:1:0;1:0:2";
    ModFields got = parse(serialise(m));
    check("fomodChoices round-trips",         got.fomodChoices == m.fomodChoices);

    // Empty choices must not produce a spurious entry.
    ModFields empty;
    empty.modPath = "/mods/NonFomod";
    ModFields gotEmpty = parse(serialise(empty));
    check("empty fomodChoices stays empty",   gotEmpty.fomodChoices.isEmpty());
}

// -- Entry point ---

int main()
{
    std::cout << "=== modlist serialization tests ===\n";

    testColumnLayout();
    testEnabledDisabled();
    testFlagCombinations();
    testBackwardCompatibility();
    testAnnotationRoundTrip();
    testDateRoundTrip();
    testDepsRoundTrip();
    testSeparatorLine();
    testFomodChoicesRoundTrip();
    testInstallStatusEmptyDir();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
