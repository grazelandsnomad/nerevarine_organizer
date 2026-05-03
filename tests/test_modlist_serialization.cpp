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
#include "modentry.h"
#include "modlist_serializer.h"

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

// -- v2 (JSONL) schema-versioned format ---

static ModEntry makeMod(const QString &path = QStringLiteral("/mods/X"))
{
    ModEntry e;
    e.itemType    = QStringLiteral("mod");
    e.checked     = true;
    e.modPath     = path;
    e.displayName = QStringLiteral("X");
    return e;
}

static ModEntry makeSep(const QString &name)
{
    ModEntry e;
    e.itemType    = QStringLiteral("separator");
    e.displayName = name;
    e.bgColor     = QColor("#ff112233");
    e.fgColor     = QColor("#ffeeeeee");
    return e;
}

// Round-trip: write a mod with every persisted field set, parse it
// back, every field that round-trips must match.  Locks in the v2
// JSON key naming against silent renames.
static void testV2ModRoundTrip()
{
    std::cout << "\n-- v2: mod row round-trips through JSONL --\n";

    ModEntry m = makeMod("/games/mods/MyMod_v3");
    m.customName    = QStringLiteral("My Mod (with custom name)");
    m.annotation    = QStringLiteral("multi\nline\tannotation\\with\\backslashes");
    m.nexusUrl      = QStringLiteral("https://www.nexusmods.com/morrowind/mods/12345");
    m.dateAdded     = QDateTime::fromString("2026-04-15T10:00:00", Qt::ISODate);
    m.dependsOn     = {QStringLiteral("https://example.com/a"),
                       QStringLiteral("https://example.com/b")};
    m.updateAvailable = true;
    m.isUtility       = true;
    m.isFavorite      = true;
    m.fomodChoices    = QStringLiteral("0:0:1;0:1:0;1:0:2");
    m.videoUrl        = QStringLiteral("https://www.youtube.com/watch?v=abc");
    m.sourceUrl       = QStringLiteral("https://github.com/user/repo");

    const QString text = modlist_serializer::serializeModlist({m});
    check("output starts with v2 schema header",
          text.startsWith(QStringLiteral("{\"format\":\"nerevarine_modlist\",\"version\":2}")));

    const QList<ModEntry> got = modlist_serializer::parseModlist(text);
    check("one row parsed back",        got.size() == 1, QString::number(got.size()));
    if (got.isEmpty()) return;
    const ModEntry &g = got.first();
    check("modPath round-trips",        g.modPath          == m.modPath);
    check("customName round-trips",     g.customName       == m.customName);
    check("annotation round-trips",     g.annotation       == m.annotation, g.annotation);
    check("nexusUrl round-trips",       g.nexusUrl         == m.nexusUrl);
    check("dateAdded round-trips",      g.dateAdded        == m.dateAdded);
    check("deps round-trip",            g.dependsOn        == m.dependsOn);
    check("updateAvailable round-trips",g.updateAvailable  == m.updateAvailable);
    check("isUtility round-trips",      g.isUtility        == m.isUtility);
    check("isFavorite round-trips",     g.isFavorite       == m.isFavorite);
    check("fomodChoices round-trips",   g.fomodChoices     == m.fomodChoices);
    check("videoUrl round-trips",       g.videoUrl         == m.videoUrl);
    check("sourceUrl round-trips",      g.sourceUrl        == m.sourceUrl);
    check("checked round-trips",        g.checked          == m.checked);
}

// The whole point of moving off tab-separated text: a tab character
// inside a custom name MUST survive serialize-parse without corruption.
// In v1 this would have shredded the row into bogus columns.
static void testV2TabInCustomNameSurvives()
{
    std::cout << "\n-- v2: tab in custom name no longer corrupts the row --\n";
    ModEntry m = makeMod();
    m.customName = QStringLiteral("Beth\tname\twith\ttabs");
    m.nexusUrl   = QStringLiteral("https://example.com/x");

    const QString text = modlist_serializer::serializeModlist({m});
    const QList<ModEntry> got = modlist_serializer::parseModlist(text);
    check("one row preserved", got.size() == 1);
    if (got.isEmpty()) return;
    check("tab characters survive", got.first().customName == m.customName,
          got.first().customName);
}

// Newline in the custom name would have outright broken v1's line-
// based parser.  v2 stores the value as a JSON string; the writer
// escapes newlines.  Stays one line on disk.
static void testV2NewlineInCustomNameSurvives()
{
    std::cout << "\n-- v2: newline in custom name encodes as one logical row --\n";
    ModEntry m = makeMod();
    m.customName = QStringLiteral("multi\nline\nname");
    m.nexusUrl   = QStringLiteral("https://example.com/y");

    const QString text = modlist_serializer::serializeModlist({m});
    // Header + one record = exactly two trailing newlines, so three
    // pieces after split('\n') (last is the trailing empty).  More
    // means the serializer actually broke the record across lines.
    const QStringList split = text.split('\n');
    int nonEmpty = 0;
    for (const QString &l : split) if (!l.isEmpty()) ++nonEmpty;
    check("exactly 2 non-empty lines (header + record)",
          nonEmpty == 2, QString::number(nonEmpty));

    const QList<ModEntry> got = modlist_serializer::parseModlist(text);
    check("newline round-trips intact", got.size() == 1
          && got.first().customName == m.customName);
}

// A separator with collapsed=true and explicit colours must round-trip
// identically.  Lock in the JSON key naming for separator records.
static void testV2SeparatorRoundTrip()
{
    std::cout << "\n-- v2: separator (color+collapsed) round-trips --\n";
    ModEntry s = makeSep("── Visual Overhauls ──");
    s.collapsed = true;

    const QString text = modlist_serializer::serializeModlist({s});
    const QList<ModEntry> got = modlist_serializer::parseModlist(text);
    check("one separator parsed",     got.size() == 1);
    if (got.isEmpty()) return;
    check("isSeparator true",          got.first().isSeparator());
    check("displayName preserved",     got.first().displayName == s.displayName);
    check("bgColor preserved",         got.first().bgColor.name(QColor::HexArgb)
                                        == s.bgColor.name(QColor::HexArgb));
    check("fgColor preserved",         got.first().fgColor.name(QColor::HexArgb)
                                        == s.fgColor.name(QColor::HexArgb));
    check("collapsed preserved",       got.first().collapsed == s.collapsed);
}

// Mid-install placeholder: v2 emits `installing: true` instead of the
// v1 dance with leading-tab columns.  Round-trip must preserve
// installStatus=2 and the URL/date/name carriers.
static void testV2InstallingPlaceholderRoundTrip()
{
    std::cout << "\n-- v2: mid-install placeholder round-trips with installing=true --\n";
    ModEntry m;
    m.itemType      = QStringLiteral("mod");
    m.checked       = false;
    m.installStatus = 2;
    m.customName    = QStringLiteral("Pending download");
    m.displayName   = m.customName;
    m.nexusUrl      = QStringLiteral("https://www.nexusmods.com/morrowind/mods/9999");
    m.dateAdded     = QDateTime::fromString("2026-04-30T22:00:00", Qt::ISODate);

    const QString text = modlist_serializer::serializeModlist({m});
    check("output mentions installing flag",
          text.contains(QStringLiteral("\"installing\":true")));
    const QList<ModEntry> got = modlist_serializer::parseModlist(text);
    check("one placeholder parsed",         got.size() == 1);
    if (got.isEmpty()) return;
    check("installStatus=2 round-trips",    got.first().installStatus == 2);
    check("nexusUrl preserved",             got.first().nexusUrl == m.nexusUrl);
    check("date preserved",                 got.first().dateAdded == m.dateAdded);
}

// A pre-v2 file (legacy tab format) MUST still load on first launch
// under v2 code.  The header sniff treats anything that doesn't start
// with the JSON header as v1 and dispatches to the legacy parser.
static void testV1LegacyFileStillLoads()
{
    std::cout << "\n-- v1: legacy tab-format file still loads --\n";
    // Mirror what the pre-v2 saver would emit for a typical user's
    // file: one separator + two mods, one of them a placeholder.
    const QString legacy =
        "# Visuals <color>#ff1a237e</color><fgcolor>#ffffffff</fgcolor><collapsed>1</collapsed>\n"
        "+ /mods/Foo\tFoo Custom Name\t\thttps://example.com/foo\t2026-01-01T00:00:00\t\t\t1\t0\t0\t0:0:1\thttps://yt/foo\thttps://gh/foo\n"
        "- \tPending Mod\t\thttps://example.com/pending\t2026-01-02T00:00:00\n";

    const QList<ModEntry> got = modlist_serializer::parseModlist(legacy);
    check("3 entries parsed",                      got.size() == 3,
          QString::number(got.size()));
    if (got.size() < 3) return;

    check("[0] is separator",                      got[0].isSeparator());
    check("[0] separator name",                    got[0].displayName == "Visuals");
    check("[0] separator collapsed",               got[0].collapsed);

    check("[1] is mod",                            got[1].isMod());
    check("[1] checked (was '+')",                 got[1].checked);
    check("[1] modPath",                           got[1].modPath == "/mods/Foo");
    check("[1] customName",                        got[1].customName == "Foo Custom Name");
    check("[1] updateAvailable parsed",            got[1].updateAvailable);
    check("[1] fomodChoices parsed",               got[1].fomodChoices == "0:0:1");
    check("[1] videoUrl parsed",                   got[1].videoUrl == "https://yt/foo");
    check("[1] sourceUrl parsed",                  got[1].sourceUrl == "https://gh/foo");

    check("[2] is mod placeholder",                got[2].isMod());
    check("[2] installStatus==2",                  got[2].installStatus == 2);
    check("[2] placeholder url preserved",         got[2].nexusUrl == "https://example.com/pending");
}

// Migration: load a v1 file, immediately re-serialize.  Output is the
// v2 header.  This is the silent in-place migration the user gets on
// first save under the new code.
static void testV1ToV2MigrationOnReSave()
{
    std::cout << "\n-- migration: v1 file → v2 file via parse + serialize --\n";
    const QString legacy =
        "+ /mods/A\tA\t\thttps://example.com/a\t2026-04-01T00:00:00\t\t\t0\t0\t1\n";
    const QList<ModEntry> parsed = modlist_serializer::parseModlist(legacy);
    check("parsed one row",          parsed.size() == 1);

    const QString reserialised = modlist_serializer::serializeModlist(parsed);
    check("re-saved file starts with v2 header",
          reserialised.startsWith(QStringLiteral("{\"format\":\"nerevarine_modlist\",\"version\":2}")));
    check("re-saved file contains the JSON record",
          reserialised.contains(QStringLiteral("\"path\":\"/mods/A\"")));
    check("favorite flag preserved through migration",
          reserialised.contains(QStringLiteral("\"favorite\":true")));
}

// Forward-compat: a v2 reader must IGNORE unknown fields without
// crashing.  Simulates what happens when a v2 tool reads a v3 file
// that's added new keys.
static void testV2ParserIgnoresUnknownFields()
{
    std::cout << "\n-- v2: unknown fields are silently ignored (forward-compat) --\n";
    const QString futureFile =
        "{\"format\":\"nerevarine_modlist\",\"version\":2}\n"
        "{\"type\":\"mod\",\"enabled\":true,\"path\":\"/mods/Z\","
        "\"name\":\"Future\",\"new_v3_field\":\"won't break me\","
        "\"another_unknown\":[1,2,3]}\n";
    const QList<ModEntry> got = modlist_serializer::parseModlist(futureFile);
    check("v3-style record parsed without error",  got.size() == 1);
    if (got.isEmpty()) return;
    check("known fields still readable",           got.first().modPath == "/mods/Z");
    check("known fields still readable (name)",    got.first().customName == "Future");
}

// Empty input → empty list, NOT a crash.
static void testEmptyInputReturnsEmpty()
{
    std::cout << "\n-- empty input handled gracefully --\n";
    check("empty string → empty list",
          modlist_serializer::parseModlist(QString()).isEmpty());
    check("whitespace-only → empty list",
          modlist_serializer::parseModlist(QStringLiteral("   \n\n\n")).isEmpty());
}

// -- Load-order schema-versioned format ---

static void testLoadOrderV2RoundTrip()
{
    std::cout << "\n-- load order: v2 round-trip --\n";
    const QStringList plugins = {
        QStringLiteral("Morrowind.esm"),
        QStringLiteral("Tribunal.esm"),
        QStringLiteral("Bloodmoon.esm"),
        QStringLiteral("OAAB_Data.esm"),
    };
    const QString text = modlist_serializer::serializeLoadOrder(plugins);
    check("output begins with v2 schema header",
          text.startsWith(QStringLiteral("{\"format\":\"nerevarine_loadorder\",\"version\":2}")));
    const QStringList got = modlist_serializer::parseLoadOrder(text);
    check("plugins round-trip in order", got == plugins);
}

// Pre-v2 load-order files (no header, just one filename per line,
// optional `#`-comments) MUST still load.
static void testLoadOrderV1StillLoads()
{
    std::cout << "\n-- load order: legacy file (no header) still loads --\n";
    const QString legacy =
        "# Plugin load order for this game profile\n"
        "Morrowind.esm\n"
        "Tribunal.esm\n"
        "Bloodmoon.esm\n";
    const QStringList got = modlist_serializer::parseLoadOrder(legacy);
    const QStringList want = {
        QStringLiteral("Morrowind.esm"),
        QStringLiteral("Tribunal.esm"),
        QStringLiteral("Bloodmoon.esm"),
    };
    check("legacy load order parsed", got == want);
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

    // Schema-versioned (v2) tests.
    testV2ModRoundTrip();
    testV2TabInCustomNameSurvives();
    testV2NewlineInCustomNameSurvives();
    testV2SeparatorRoundTrip();
    testV2InstallingPlaceholderRoundTrip();
    testV1LegacyFileStillLoads();
    testV1ToV2MigrationOnReSave();
    testV2ParserIgnoresUnknownFields();
    testEmptyInputReturnsEmpty();
    testLoadOrderV2RoundTrip();
    testLoadOrderV1StillLoads();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
