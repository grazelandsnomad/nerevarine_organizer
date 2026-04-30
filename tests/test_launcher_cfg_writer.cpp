// tests/test_launcher_cfg_writer.cpp
//
// Golden-file tests for openmw::renderLauncherCfg.
//
// The launcher.cfg sync was added after a regression where uninstalled mods
// kept appearing in the OpenMW Launcher's Data Files tab because the
// launcher reads from its own per-profile cache, not from openmw.cfg.
// Pinning the behaviours these tests check:
//
//   * only the CURRENT profile's data=/content= lines get rewritten
//   * everything else (other profiles, [Settings]/[General]/[Importer],
//     fallback-archive= within the current profile, blank lines, comments)
//     survives byte-for-byte
//   * data= paths are emitted unquoted even when they contain spaces -
//     that's what the launcher itself writes on disk
//
// Build + run:
//   ./build/tests/test_launcher_cfg_writer

#include "openmwconfigwriter.h"

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

using openmw::renderLauncherCfg;
using openmw::readLauncherCfgContentOrder;
using openmw::readLauncherCfgDataPaths;

// Empty input → empty output.  Caller treats that as "no file, do nothing".
static void testEmptyInput()
{
    std::cout << "testEmptyInput\n";
    const QString out = renderLauncherCfg({}, {"/a"}, {"A.esp"});
    check("empty input returns empty string", out.isEmpty(), out, QStringLiteral("<empty>"));
}

// No [Profiles] section → no-op.  Happens if the launcher wrote a partial
// file (unlikely) or a future version restructures.
static void testNoProfilesSection()
{
    std::cout << "testNoProfilesSection\n";
    const QString in =
        "[Settings]\n"
        "language=English\n"
        "[General]\n"
        "firstrun=false\n";
    const QString out = renderLauncherCfg(in, {"/a"}, {"A.esp"});
    check("missing [Profiles] returns empty", out.isEmpty(), out, QStringLiteral("<empty>"));
}

// [Profiles] present but no currentprofile= → no-op.  Nothing safe to touch.
static void testNoCurrentProfile()
{
    std::cout << "testNoCurrentProfile\n";
    const QString in =
        "[Settings]\n"
        "[Profiles]\n"
        "2026-01-01T00:00:00/data=/x\n";
    const QString out = renderLauncherCfg(in, {"/a"}, {"A.esp"});
    check("missing currentprofile= returns empty", out.isEmpty(), out, QStringLiteral("<empty>"));
}

// Happy path: current profile has stale data/content, rewrite replaces them.
// fallback-archive= inside the current profile must stick around.
static void testRewriteCurrentProfile()
{
    std::cout << "testRewriteCurrentProfile\n";
    const QString in =
        "[Settings]\n"
        "language=English\n"
        "\n"
        "[Profiles]\n"
        "currentprofile=2026-04-18T20:01:36\n"
        "2026-04-18T20:01:36/fallback-archive=Morrowind.bsa\n"
        "2026-04-18T20:01:36/fallback-archive=Tribunal.bsa\n"
        "2026-04-18T20:01:36/data=/old/base\n"
        "2026-04-18T20:01:36/data=/old/HSN\n"
        "2026-04-18T20:01:36/content=HlaaluSeydaNeen.esp\n"
        "\n"
        "[General]\n"
        "firstrun=false\n";

    const QString want =
        "[Settings]\n"
        "language=English\n"
        "\n"
        "[Profiles]\n"
        "currentprofile=2026-04-18T20:01:36\n"
        "2026-04-18T20:01:36/fallback-archive=Morrowind.bsa\n"
        "2026-04-18T20:01:36/fallback-archive=Tribunal.bsa\n"
        "2026-04-18T20:01:36/data=/new/base\n"
        "2026-04-18T20:01:36/data=/new/mod\n"
        "2026-04-18T20:01:36/content=A.esp\n"
        "2026-04-18T20:01:36/content=B.esp\n"
        "\n"
        "[General]\n"
        "firstrun=false\n";

    const QString out = renderLauncherCfg(
        in,
        {"/new/base", "/new/mod"},
        {"A.esp", "B.esp"});

    check("HSN lines replaced; fallback-archive preserved", out == want, out, want);
}

// Non-current profiles must not change at all.  Build a two-profile fixture;
// the old profile's slab must survive byte-identical.
static void testOtherProfilesUntouched()
{
    std::cout << "testOtherProfilesUntouched\n";
    const QString in =
        "[Profiles]\n"
        "currentprofile=2026-04-18T20:01:36\n"
        "2026-04-18T20:01:36/data=/old/HSN\n"
        "2026-04-18T20:01:36/content=HSN.esp\n"
        "2026-03-01T00:00:00/fallback-archive=Morrowind.bsa\n"
        "2026-03-01T00:00:00/data=/archive/old\n"
        "2026-03-01T00:00:00/content=Archive.esp\n";

    const QString out = renderLauncherCfg(in, {"/new"}, {"New.esp"});

    check("archived profile data= survives",
          out.contains("2026-03-01T00:00:00/data=/archive/old"),
          out);
    check("archived profile content= survives",
          out.contains("2026-03-01T00:00:00/content=Archive.esp"),
          out);
    check("archived profile fallback-archive= survives",
          out.contains("2026-03-01T00:00:00/fallback-archive=Morrowind.bsa"),
          out);
    check("stale HSN content= removed",
          !out.contains("HSN.esp"),
          out);
    check("stale HSN data= removed",
          !out.contains("/old/HSN"),
          out);
}

// Round-trip idempotence: running the renderer twice with the same args
// produces identical output.  Protects against insertion-point drift.
static void testIdempotence()
{
    std::cout << "testIdempotence\n";
    const QString in =
        "[Profiles]\n"
        "currentprofile=TS\n"
        "TS/fallback-archive=Morrowind.bsa\n"
        "TS/data=/stale\n"
        "TS/content=stale.esp\n";

    const QString once = renderLauncherCfg(in,
        {"/a", "/b"}, {"A.esp", "B.esp"});
    const QString twice = renderLauncherCfg(once,
        {"/a", "/b"}, {"A.esp", "B.esp"});

    check("renderer is idempotent", once == twice, twice, once);
}

// CRLF in the input must produce CRLF in the output.  Launcher.cfg in the
// wild is LF-only but we've been burned by Windows round-trips before
// (OpenMW Launcher on Windows still writes LF, but user-edited copies
// via Notepad can pick up CRLF).
static void testCrlfPreserved()
{
    std::cout << "testCrlfPreserved\n";
    const QString in =
        "[Profiles]\r\n"
        "currentprofile=TS\r\n"
        "TS/data=/old\r\n";

    const QString out = renderLauncherCfg(in, {"/new"}, {});
    check("CRLF line endings survive", out.contains(QStringLiteral("\r\n")), out);
    check("new data= line emitted", out.contains("TS/data=/new"), out);
}

// Path with spaces: launcher stores these UNQUOTED.  Quoting them would
// make the launcher treat the quotes as literal path characters and fail
// to find the directory.
static void testSpacesUnquoted()
{
    std::cout << "testSpacesUnquoted\n";
    const QString in =
        "[Profiles]\n"
        "currentprofile=TS\n";

    const QString out = renderLauncherCfg(in,
        {"/mods/A Mod With Spaces"}, {});
    check("path with spaces is not quoted",
          out.contains("TS/data=/mods/A Mod With Spaces")
          && !out.contains("TS/data=\"/mods/A Mod With Spaces\""),
          out);
}

// No existing <ts>/ lines other than currentprofile= → new block lands
// directly under currentprofile=.  Seen on a fresh launcher profile that
// was created but never saved.
static void testInsertAfterCurrentProfile()
{
    std::cout << "testInsertAfterCurrentProfile\n";
    const QString in =
        "[Profiles]\n"
        "currentprofile=TS\n"
        "[General]\n"
        "firstrun=false\n";

    const QString want =
        "[Profiles]\n"
        "currentprofile=TS\n"
        "TS/data=/a\n"
        "TS/content=A.esp\n"
        "[General]\n"
        "firstrun=false\n";

    const QString out = renderLauncherCfg(in, {"/a"}, {"A.esp"});
    check("new block lands after currentprofile=", out == want, out, want);
}

// -- readLauncherCfgContentOrder ---
//
// Powers absorbExternalLoadOrder's "user reordered in the launcher's Data
// Files tab" recovery: the launcher updates launcher.cfg on every reorder
// even when the user closes without clicking Save/Play. Without reading
// this signal (instead of only openmw.cfg), the next sync rewrites
// openmw.cfg from a stale m_loadOrder and silently clobbers the reorder.

static void testReadEmpty()
{
    std::cout << "testReadEmpty\n";
    check("empty input → empty list",
          readLauncherCfgContentOrder({}).isEmpty());
}

static void testReadNoProfilesSection()
{
    std::cout << "testReadNoProfilesSection\n";
    const QString in =
        "[Settings]\nlanguage=English\n"
        "[General]\nfirstrun=false\n";
    check("no [Profiles] → empty list",
          readLauncherCfgContentOrder(in).isEmpty());
}

static void testReadNoCurrentProfile()
{
    std::cout << "testReadNoCurrentProfile\n";
    const QString in =
        "[Profiles]\n2026-01-01T00:00:00/content=X.esp\n";
    check("no currentprofile= → empty list",
          readLauncherCfgContentOrder(in).isEmpty());
}

// Happy path: ordered content= lines in the current profile come back
// in encounter order, case-preserved.
static void testReadHappyPath()
{
    std::cout << "testReadHappyPath\n";
    const QString in =
        "[Settings]\n"
        "[Profiles]\n"
        "currentprofile=TS\n"
        "TS/fallback-archive=Morrowind.bsa\n"
        "TS/data=/mods/A\n"
        "TS/content=Zeta.esp\n"
        "TS/content=Alpha.ESP\n"
        "TS/content=Bravo.esp\n"
        "[General]\n";

    const auto order = readLauncherCfgContentOrder(in);
    check("three entries",                  order.size() == 3,
          QString::number(order.size()));
    check("first is Zeta.esp (user order)",  order.value(0) == "Zeta.esp",
          order.value(0));
    check("second is Alpha.ESP (case preserved)",
          order.value(1) == "Alpha.ESP",   order.value(1));
    check("third is Bravo.esp",              order.value(2) == "Bravo.esp",
          order.value(2));
}

// Only the CURRENT profile counts.  Content= lines under another
// timestamp must not leak in - otherwise the merge in absorb could
// resurrect plugins the user disabled ages ago.
static void testReadIgnoresOtherProfiles()
{
    std::cout << "testReadIgnoresOtherProfiles\n";
    const QString in =
        "[Profiles]\n"
        "currentprofile=CURRENT\n"
        "CURRENT/content=Active.esp\n"
        "OLD/content=Archived.esp\n";

    const auto order = readLauncherCfgContentOrder(in);
    check("only current profile's content= returned",
          order == QStringList{"Active.esp"},
          order.join(","));
}

static void testReadCrlfTolerated()
{
    std::cout << "testReadCrlfTolerated\n";
    const QString in =
        "[Profiles]\r\ncurrentprofile=TS\r\nTS/content=A.esp\r\nTS/content=B.esp\r\n";
    check("CRLF input still parses",
          readLauncherCfgContentOrder(in) == QStringList{"A.esp", "B.esp"});
}

// -- readLauncherCfgDataPaths ---
//
// Powers syncOpenMWConfig's "preserve launcher-only externals" augmentation:
// when the user configured Morrowind only via the OpenMW Launcher, vanilla
// "data=<Morrowind Data Files>" can live ONLY in launcher.cfg - the local
// openmw.cfg never saw it.  Without reading this signal, the orphan-plugin
// scrub treats Morrowind.esm as "no data= provides this" and the launcher
// sync at the end of saveModList wipes the entire vanilla Content List
// from launcher.cfg on the next user run.

static void testReadDataPathsEmpty()
{
    std::cout << "testReadDataPathsEmpty\n";
    check("empty input → empty list",
          readLauncherCfgDataPaths({}).isEmpty());
}

static void testReadDataPathsNoCurrentProfile()
{
    std::cout << "testReadDataPathsNoCurrentProfile\n";
    const QString in =
        "[Profiles]\nTS/data=/should/not/leak\n";
    check("no currentprofile= → empty list",
          readLauncherCfgDataPaths(in).isEmpty());
}

static void testReadDataPathsHappyPath()
{
    std::cout << "testReadDataPathsHappyPath\n";
    const QString in =
        "[Settings]\n"
        "[Profiles]\n"
        "currentprofile=TS\n"
        "TS/fallback-archive=Morrowind.bsa\n"
        "TS/data=/home/user/Games/Morrowind/Data Files\n"
        "TS/data=/home/user/Games/MorrowindMods/A Mod\n"
        "TS/content=Morrowind.esm\n"
        "[General]\n";

    const auto paths = readLauncherCfgDataPaths(in);
    check("two data= entries returned in encounter order",
          paths.size() == 2, QString::number(paths.size()));
    check("vanilla data= path is preserved with spaces unquoted",
          paths.value(0) == "/home/user/Games/Morrowind/Data Files",
          paths.value(0));
    check("second data= path is preserved",
          paths.value(1) == "/home/user/Games/MorrowindMods/A Mod",
          paths.value(1));
}

static void testReadDataPathsIgnoresOtherProfiles()
{
    std::cout << "testReadDataPathsIgnoresOtherProfiles\n";
    const QString in =
        "[Profiles]\n"
        "currentprofile=CURRENT\n"
        "CURRENT/data=/active/path\n"
        "OLD/data=/archived/path\n";

    const auto paths = readLauncherCfgDataPaths(in);
    check("only current profile's data= entries are returned",
          paths == QStringList{"/active/path"},
          paths.join(","));
}

static void testReadDataPathsTolerateQuoted()
{
    std::cout << "testReadDataPathsTolerateQuoted\n";
    // Belt-and-braces: launchers that might wrap paths in quotes (e.g.
    // hand-edited cfgs) should still produce unquoted paths so the caller's
    // path comparison against m_modsDir works.
    const QString in =
        "[Profiles]\n"
        "currentprofile=TS\n"
        "TS/data=\"/home/user/Games/Morrowind/Data Files\"\n";
    check("quoted data= path is unquoted on read",
          readLauncherCfgDataPaths(in)
            == QStringList{"/home/user/Games/Morrowind/Data Files"});
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    testEmptyInput();
    testNoProfilesSection();
    testNoCurrentProfile();
    testRewriteCurrentProfile();
    testOtherProfilesUntouched();
    testIdempotence();
    testCrlfPreserved();
    testSpacesUnquoted();
    testInsertAfterCurrentProfile();
    testReadEmpty();
    testReadNoProfilesSection();
    testReadNoCurrentProfile();
    testReadHappyPath();
    testReadIgnoresOtherProfiles();
    testReadCrlfTolerated();
    testReadDataPathsEmpty();
    testReadDataPathsNoCurrentProfile();
    testReadDataPathsHappyPath();
    testReadDataPathsIgnoresOtherProfiles();
    testReadDataPathsTolerateQuoted();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
