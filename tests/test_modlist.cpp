// io, serialization, ModEntry, sync_guard. Shared check() harness.

#include "annotation_codec.h"
#include "modentry.h"
#include "modlist_io.h"
#include "modlist_serializer.h"
#include "modlist_sync_guard.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <iostream>
#include <vector>

static int s_passed = 0;
static int s_failed = 0;

// check(name, ok), check(name, ok, detail), or check(name, ok, got, want).
static void check(const char *name, bool ok,
                  const QString &detail = {}, const QString &want = {})
{
    if (ok) {
        std::cout << "  \033[32m\xE2\x9C\x93\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m\xE2\x9C\x97\033[0m " << name;
        if (want.isEmpty()) {
            if (!detail.isEmpty())
                std::cout << " (" << detail.toStdString() << ")";
            std::cout << "\n";
        } else {
            std::cout << "\n";
            std::cout << "    --- want ---\n" << want.toStdString() << "\n";
            std::cout << "    ---  got ---\n" << detail.toStdString() << "\n";
        }
        ++s_failed;
    }
}

// --- modlist_io ---

namespace io_section {

static QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

static void testWriteModlistFile_success()
{
    std::cout << "\n[writeModlistFile: success returns nullopt + writes content]\n";
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::cerr << "tmp setup failed\n"; std::exit(1); }
    const QString path = tmp.path() + "/modlist.txt";
    const QString content = "v2-schema\nrow1\nrow2\n";

    auto err = modlist_io::writeModlistFile(path, content);
    check("returns nullopt on success", !err.has_value(),
          err.value_or("(success)"));
    check("file lands on disk", QFile::exists(path));
    check("content matches", readFile(path) == content);
}

static void testWriteModlistFile_overwriteCreatesBackup()
{
    std::cout << "\n[writeModlistFile: overwrite snapshot-backs the previous content]\n";
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::cerr << "tmp setup failed\n"; std::exit(1); }
    const QString path = tmp.path() + "/modlist.txt";

    // seed a pre-existing file
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            std::cerr << "couldn't pre-seed " << path.toStdString() << "\n";
            std::exit(2);
        }
        f.write("OLD\n");
    }

    auto err = modlist_io::writeModlistFile(path, "NEW\n");
    check("nullopt on overwrite", !err.has_value(),
          err.value_or("(success)"));
    check("new content present", readFile(path) == "NEW\n");

    // backup is a sibling "<basename>.bak.*".
    QDir dir(tmp.path());
    const QStringList baks = dir.entryList(
        {"modlist.txt.bak.*"}, QDir::Files);
    check("snapshot backup created", !baks.isEmpty(),
          QString::number(baks.size()) + " backup(s)");
}

static void testWriteModlistFile_missingParentReturnsError()
{
    std::cout << "\n[writeModlistFile: missing parent dir → error string returned]\n";
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::cerr << "tmp setup failed\n"; std::exit(1); }

    // Missing parent dir fails open() everywhere; QFile won't auto-mkdir.
    // The old chmod-0500 trick passed on Windows (NTFS ignores POSIX bits
    // via MinGW chmod()).
    const QString path = tmp.path() + "/no_such_dir/sub/modlist.txt";
    auto err = modlist_io::writeModlistFile(path, "content");

    check("returns an error string on open failure", err.has_value());
    check("error string is non-empty",
          err.has_value() && !err->isEmpty());
    check("file was NOT created (no auto-mkdir)",
          !QFile::exists(path));
}

static void testWriteModlistFile_emptyPathReturnsError()
{
    std::cout << "\n[writeModlistFile: empty path returns error]\n";
    auto err = modlist_io::writeModlistFile(QString(), "content");
    check("empty path produces an error", err.has_value());
}

static void testWriteModlistFile_emptyContentSucceeds()
{
    std::cout << "\n[writeModlistFile: empty content writes a 0-byte file]\n";
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::cerr << "tmp setup failed\n"; std::exit(1); }
    const QString path = tmp.path() + "/empty.txt";

    auto err = modlist_io::writeModlistFile(path, QString());
    check("nullopt on empty content", !err.has_value());
    check("file exists",      QFile::exists(path));
    check("file is 0 bytes",  QFileInfo(path).size() == 0);
}

static void testWriteModlistFile_serialOverwriteIsAtomicEnough()
{
    std::cout << "\n[writeModlistFile: two sequential writes leave the LAST content on disk]\n";
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::cerr << "tmp setup failed\n"; std::exit(1); }
    const QString path = tmp.path() + "/modlist.txt";

    // MainWindow serializes saves via m_lastSaveFuture.waitForFinished(),
    // so "concurrent" saves are really two sequential writes. Last wins.
    (void)modlist_io::writeModlistFile(path, "FIRST\n");
    auto err = modlist_io::writeModlistFile(path, "SECOND\n");

    check("second write succeeds",          !err.has_value());
    check("second write's content lands",   readFile(path) == "SECOND\n");
}

static void testWriteModlistFile_largeContentRoundtrip()
{
    std::cout << "\n[writeModlistFile: 1 MB roundtrip is byte-identical]\n";
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::cerr << "tmp setup failed\n"; std::exit(1); }
    const QString path = tmp.path() + "/big.txt";

    QString big;
    big.reserve(1 << 20);
    for (int i = 0; i < (1 << 20); ++i) big.append(QChar('A' + (i % 26)));

    auto err = modlist_io::writeModlistFile(path, big);
    check("nullopt on big write", !err.has_value());
    check("roundtrip is byte-identical (1 MB)", readFile(path) == big);
}

} // namespace io_section

static void run_modlist_io()
{
    std::cout << "=== modlist_io::writeModlistFile ===\n";

    io_section::testWriteModlistFile_success();
    io_section::testWriteModlistFile_overwriteCreatesBackup();
    io_section::testWriteModlistFile_missingParentReturnsError();
    io_section::testWriteModlistFile_emptyPathReturnsError();
    io_section::testWriteModlistFile_emptyContentSucceeds();
    io_section::testWriteModlistFile_serialOverwriteIsAtomicEnough();
    io_section::testWriteModlistFile_largeContentRoundtrip();
}

// --- modlist serialization ---

namespace serialization_section {

// Same on-disk shape as saveModList().

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

// Same shape as loadModList().
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
    // parts[10]: FOMOD install choices, missing -> empty
    out.fomodChoices      = parts.size() > 10 ? parts[10] : QString();
    return out;
}

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

    // pre-utility-column file (ends at updateFlag)
    QString legacyLine = "+ /mods/OldMod\tOld Mod\t\t\t\t\t\t0";
    ModFields got = parse(legacyLine);
    check("pre-utility line: isUtility defaults false",  !got.isUtility);
    check("pre-utility line: isFavorite defaults false", !got.isFavorite);

    // before the favorite column (has utility, no favorite)
    QString preFavLine = "+ /mods/UtilMod\tUtil\t\t\t\t\t\t0\t1";
    got = parse(preFavLine);
    check("pre-favorite line: isUtility reads correctly", got.isUtility == true);
    check("pre-favorite line: isFavorite defaults false", !got.isFavorite);

    // before the fomodChoices column
    QString preFomodLine = "+ /mods/FomodMod\tFomod\t\t\t\t\t\t0\t0\t1";
    got = parse(preFomodLine);
    check("pre-fomodChoices line: isFavorite reads correctly", got.isFavorite == true);
    check("pre-fomodChoices line: fomodChoices defaults empty", got.fomodChoices.isEmpty());
}

static void testAnnotationRoundTrip()
{
    std::cout << "\n-- annotation encoding inside modlist line --\n";

    // newlines/tabs/backslashes must survive the tab-delimited format
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

    // empty deps must not yield a spurious "" entry
    ModFields empty;
    empty.modPath = "/mods/D";
    ModFields gotEmpty = parse(serialise(empty));
    check("empty deps stays empty",      gotEmpty.deps.isEmpty());
}

static void testSeparatorLine()
{
    std::cout << "\n-- separator line format --\n";

    // shape the saver emits
    QString sepLine = "# My Section"
                      " <color>#ff1a237e</color>"
                      "<fgcolor>#ffffffff</fgcolor>"
                      "<collapsed>1</collapsed>";

    check("separator starts with '# '",       sepLine.startsWith("# "));
    check("separator has <color> tag",         sepLine.contains("<color>"));
    check("separator has <fgcolor> tag",       sepLine.contains("<fgcolor>"));
    check("separator carries collapsed state", sepLine.contains("<collapsed>1</collapsed>"));
    // separators must not parse as mod lines
    check("separator not parsed as mod line",  !(sepLine.size() >= 2
                                                 && (sepLine[0] == '+' || sepLine[0] == '-')
                                                 && sepLine[1] == ' '));
}

// Existing-but-empty mod dir counts as not-installed. A bare QDir::exists()
// passed it -> false "installed" badges + bogus missing-master warnings.
static void testInstallStatusEmptyDir()
{
    std::cout << "\n-- install-status: empty directory treated as not-installed --\n";

    // same check loadModList does
    auto computeInstalled = [](const QString &modPath) -> bool {
        QDir d(modPath);
        return !modPath.isEmpty() && d.exists() && !d.isEmpty();
    };

    check("missing path → not installed",
          !computeInstalled("/nonexistent/path/that/does/not/exist"));

    check("empty modPath → not installed",
          !computeInstalled(QString()));

    // a real dir that exists but is empty
    {
        QTemporaryDir tmp;
        bool exists  = QDir(tmp.path()).exists();
        bool isEmpty = QDir(tmp.path()).isEmpty();
        check("QTemporaryDir exists",    exists);
        check("QTemporaryDir is empty",  isEmpty);
        check("empty tmpdir → not installed",
              !computeInstalled(tmp.path()));

        // drop a file in -> installed
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

    // empty choices stay empty, no spurious entry
    ModFields empty;
    empty.modPath = "/mods/NonFomod";
    ModFields gotEmpty = parse(serialise(empty));
    check("empty fomodChoices stays empty",   gotEmpty.fomodChoices.isEmpty());
}

// --- v2 (JSONL) schema-versioned format ---

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

// Every persisted field set, save+load, all must match. Catches silent
// renames of the v2 JSON keys.
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

// Tab inside a custom name must survive serialize/parse. v1 shredded
// the row into bogus columns - why we left tab-separated text.
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

// A newline in the custom name broke v1's line-based parser. v2 escapes
// it inside the JSON string, so the record stays one line on disk.
static void testV2NewlineInCustomNameSurvives()
{
    std::cout << "\n-- v2: newline in custom name encodes as one logical row --\n";
    ModEntry m = makeMod();
    m.customName = QStringLiteral("multi\nline\nname");
    m.nexusUrl   = QStringLiteral("https://example.com/y");

    const QString text = modlist_serializer::serializeModlist({m});
    // Header + record = 2 non-empty lines. More means the serializer
    // split the record across lines.
    const QStringList split = text.split('\n');
    int nonEmpty = 0;
    for (const QString &l : split) if (!l.isEmpty()) ++nonEmpty;
    check("exactly 2 non-empty lines (header + record)",
          nonEmpty == 2, QString::number(nonEmpty));

    const QList<ModEntry> got = modlist_serializer::parseModlist(text);
    check("newline round-trips intact", got.size() == 1
          && got.first().customName == m.customName);
}

// Separator with collapsed=true + explicit colours must save/load
// identically. Pins the separator JSON keys.
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

// Mid-install placeholder: v2 emits `installing: true` instead of v1's
// leading-tab columns. installStatus=2 + URL/date/name must survive.
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

// Pre-v2 tab-format file must still load. Anything not starting with the
// JSON header is sniffed as v1.
static void testV1LegacyFileStillLoads()
{
    std::cout << "\n-- v1: legacy tab-format file still loads --\n";
    // pre-v2 saver output: separator + two mods, last one a placeholder.
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

// Load a v1 file, re-serialize, output carries the v2 header. This is the
// silent migration users get on first save under the new code.
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

// Forward-compat: v2 reader must ignore unknown fields, not crash
// (e.g. a v2 build reading a v3 file with new keys).
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

// In-flight placeholders carry a per-install QUuid token. It must survive
// save/load so a relaunch can reattach pending InstallController signals
// to the right row.
static void testV2InstallTokenRoundTrip()
{
    std::cout << "\n-- v2: installing-row token round-trips --\n";
    ModEntry m;
    m.itemType      = QStringLiteral("mod");
    m.installStatus = 2;
    m.customName    = QStringLiteral("Pending mod");
    m.displayName   = m.customName;
    m.nexusUrl      = QStringLiteral("https://www.nexusmods.com/morrowind/mods/4242");
    m.installToken  = QUuid::createUuid();

    const QString text = modlist_serializer::serializeModlist({m});
    check("output mentions token field",
          text.contains(QStringLiteral("\"token\"")));
    check("output contains the token's hex form",
          text.contains(m.installToken.toString(QUuid::WithoutBraces)));
    const QList<ModEntry> got = modlist_serializer::parseModlist(text);
    check("one placeholder parsed",        got.size() == 1);
    if (got.isEmpty()) return;
    check("token matches",                 got.first().installToken == m.installToken);
    check("nexusUrl preserved",            got.first().nexusUrl     == m.nexusUrl);
}

// Installed (status==1) rows don't need a token; serializer omits the
// field instead of writing a null UUID.
static void testV2InstalledRowSkipsToken()
{
    std::cout << "\n-- v2: installed-row token field omitted --\n";
    ModEntry m;
    m.itemType      = QStringLiteral("mod");
    m.installStatus = 1;
    m.checked       = true;
    m.modPath       = QStringLiteral("/mods/Foo");
    m.customName    = QStringLiteral("Foo");
    m.installToken  = QUuid::createUuid();   // stray token, shouldn't reach the file

    const QString text = modlist_serializer::serializeModlist({m});
    check("installed row never carries `token`",
          !text.contains(QStringLiteral("\"token\"")));
}

// Pre-v2 load-order files (no header, one filename per line, optional
// #-comments) must still load.
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

} // namespace serialization_section

static void run_modlist_serialization()
{
    using namespace serialization_section;

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

    // v2 schema tests.
    testV2ModRoundTrip();
    testV2TabInCustomNameSurvives();
    testV2NewlineInCustomNameSurvives();
    testV2SeparatorRoundTrip();
    testV2InstallingPlaceholderRoundTrip();
    testV2InstallTokenRoundTrip();
    testV2InstalledRowSkipsToken();
    testV1LegacyFileStillLoads();
    testV1ToV2MigrationOnReSave();
    testV2ParserIgnoresUnknownFields();
    testEmptyInputReturnsEmpty();
    testLoadOrderV2RoundTrip();
    testLoadOrderV1StillLoads();
}

// === ModEntry ===

namespace modentry_section {

// Tiny builders so each case reads as data.

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

} // namespace modentry_section

static void run_modentry()
{
    using namespace modentry_section;

    std::cout << "=== ModEntry tests ===\n";

    // Default ModEntry: "mod" row, unchecked, else zero/empty. Copy-and-
    // modify call sites rely on this.
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

    // effectiveName prefers customName, else displayName (same as the
    // ModRole::CustomName rule).
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

    // Defaulted operator== is memberwise; one differing field breaks eq.
    {
        ModEntry a = mod("Foo", 1024);
        ModEntry b = mod("Foo", 1024);
        check("memberwise equality",            a == b);

        b.annotation = QStringLiteral("bumped");
        check("differing annotation breaks eq", !(a == b));
    }

    // lessByDisplayName: case-insensitive asc, separators last.
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

    // lessByModSize: asc, zero-size and separators trail (UI's "unknown
    // size at the end" rule).
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

    // lessByDateAdded: asc, invalid dates and separators trail.
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

    // Comparators must be irreflexive: !(a<a).
    {
        const ModEntry m = mod("x", 1234, QDateTime::fromString("2026-01-01", Qt::ISODate));
        const ModEntry s = separator("section");
        bool reflexive = lessByDisplayName(m, m) || lessByDisplayName(s, s)
                      || lessByModSize    (m, m) || lessByModSize    (s, s)
                      || lessByDateAdded  (m, m) || lessByDateAdded  (s, s);
        check("comparators are irreflexive", !reflexive);
    }
}

// === modlist sync guard ===

namespace sync_guard_section {

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

// Every mod under the root -> no drift.
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

// Mod outside the root -> drift. The cross-machine case the guard exists for.
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

// "/home/x/mods" must NOT accept "/home/x/mods_backup/..." - the naive
// string-prefix bug that silently passes bad paths.
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

// Mods split across mount points: under either root is clean.
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

// No roots -> guard inactive: empty drift, don't flag everything. The UI
// reads canonicalRoots to pick its message.
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

// Empty path -> always drift, with a reason. A cancel flow can clear
// ModPath; such a row silently drops out of openmw.cfg, so surface it
// even when the guard is otherwise off.
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

// Trailing slashes mustn't fail the boundary check - QDir paths sometimes
// arrive with a trailing '/'.
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

} // namespace sync_guard_section

static void run_modlist_sync_guard()
{
    using namespace sync_guard_section;

    testCanonicalizePreservesAbsolute();
    testAllUnderCanonical();
    testPathOutsideCanonicalIsDrift();
    testPrefixBoundaryIsEnforced();
    testMultipleCanonicalRoots();
    testEmptyCanonicalRootsInactive();
    testEmptyPathIsDrift();
    testTrailingSlashesTolerated();
}

int main(int argc, char **argv)
{
    // One QCoreApplication for all sections; none need Widgets.
    QCoreApplication app(argc, argv);

    run_modlist_io();
    std::cout << "\n";
    run_modlist_serialization();
    std::cout << "\n";
    run_modentry();
    std::cout << "\n";
    run_modlist_sync_guard();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
