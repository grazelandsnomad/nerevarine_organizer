// tests/test_log_triage.cpp
//
// Golden-file tests for openmw::triageOpenMWLog.
//
// The triage is pure: (logText, mods) → LogTriageReport.  That makes the
// error shapes OpenMW emits trivially reproducible here - every pattern
// we rely on is pinned by a test, so an OpenMW release that rephrases one
// of them can't silently break the diagnostic dialog.
//
// Patterns covered:
//   * "File X.esp asks for parent file Y.esm" (MissingMaster)
//   * "Fatal error: Failed loading X.esp: the content file does not exist"
//     (MissingPlugin)
//   * "Can't find texture \"X.dds\"" + "Error loading X.nif"
//     (MissingAsset, several phrasings)
//   * unknown "Error:" lines fall into OtherError, capped so a 10k-line
//     crash log doesn't produce 10k unhelpful rows
//   * dedup: a parent-file error repeated 80 times still makes one issue
//   * cross-reference: plugin filename resolves to the modlist display name
//
// Build + run:
//   ./build/tests/test_log_triage

#include "log_triage.h"

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

using openmw::LogIssueKind;
using openmw::LogIssue;
using openmw::LogTriageReport;
using openmw::TriageMod;
using openmw::triageOpenMWLog;

// Convenience: find first issue matching a predicate.
template <typename Pred>
static const LogIssue *find(const LogTriageReport &r, Pred p)
{
    for (const LogIssue &i : r.issues) if (p(i)) return &i;
    return nullptr;
}

// Empty log → empty report, no crashes.
static void testEmptyLog()
{
    std::cout << "testEmptyLog\n";
    auto r = triageOpenMWLog({}, {});
    check("empty log → no issues", r.issues.isEmpty() && r.errorLines == 0);
}

// The exact phrasing comes straight from OpenMW, cross-checked against
// include/master_satisfaction.h and the comment at mainwindow.cpp:7439.
static void testMissingMaster()
{
    std::cout << "testMissingMaster\n";
    const QString log =
        "[20:00:00.000 E] Fatal error: File HlaaluSeydaNeen_AFFresh_Patch.ESP "
        "asks for parent file AFFresh.esm, but it is not available or has "
        "been loaded in the wrong order\n";

    QList<TriageMod> mods = {
        {"Hlaalu Seyda Neen",
         {"HlaaluSeydaNeen.esp", "HlaaluSeydaNeen_AFFresh_Patch.ESP"}},
        {"Unrelated Mod", {"Foo.esp"}},
    };

    auto r = triageOpenMWLog(log, mods);
    const LogIssue *i = find(r, [](const LogIssue &x){
        return x.kind == LogIssueKind::MissingMaster;
    });
    check("parent-file line classified as MissingMaster", i != nullptr);
    if (!i) return;
    check("child target captured", i->target == "HlaaluSeydaNeen_AFFresh_Patch.ESP", i->target);
    check("parent captured",       i->parent == "AFFresh.esm",                     i->parent);
    check("suspect mod resolved",  i->suspectMod == "Hlaalu Seyda Neen",           i->suspectMod);
}

// The "Failed loading X.esp: the content file does not exist" phrasing -
// documented at src/mainwindow.cpp:7400 and status_orphan_content_dropped
// in translations/english.ini.
static void testMissingPlugin()
{
    std::cout << "testMissingPlugin\n";
    const QString log =
        "[20:00:05.123 E] Fatal error: Failed loading Ghost.esp: "
        "the content file does not exist\n";

    QList<TriageMod> mods = {{"Ghostly Content Pack", {"Ghost.esp"}}};

    auto r = triageOpenMWLog(log, mods);
    const LogIssue *i = find(r, [](const LogIssue &x){
        return x.kind == LogIssueKind::MissingPlugin;
    });
    check("failed-loading line classified as MissingPlugin", i != nullptr);
    if (!i) return;
    check("plugin target captured",    i->target == "Ghost.esp",           i->target);
    check("suspect mod resolved",      i->suspectMod == "Ghostly Content Pack", i->suspectMod);
}

// Texture phrasing from OpenMW's OSG asset loader.  Variants in the wild:
//   * "Can't find texture \"bar.dds\""
//   * "Can not find texture bar.dds"
static void testMissingTexture()
{
    std::cout << "testMissingTexture\n";
    const QString log =
        "[20:00:10.000 W] Warning: Can't find texture \"textures/stone.dds\"\n"
        "[20:00:10.001 W] Warning: Can not find texture grass.dds\n";

    auto r = triageOpenMWLog(log, {});
    const LogIssue *i1 = find(r, [](const LogIssue &x){
        return x.kind == LogIssueKind::MissingAsset && x.target.contains("stone.dds");
    });
    const LogIssue *i2 = find(r, [](const LogIssue &x){
        return x.kind == LogIssueKind::MissingAsset && x.target.contains("grass.dds");
    });
    check("quoted texture path captured",    i1 != nullptr);
    check("unquoted texture name captured",  i2 != nullptr);
}

// Mesh phrasing.  OpenMW typically prints "Error loading foo.nif: ..."
static void testMissingMesh()
{
    std::cout << "testMissingMesh\n";
    const QString log =
        "[20:00:15.000 E] Error loading meshes/f/foo.nif: unexpected EOF\n";

    auto r = triageOpenMWLog(log, {});
    const LogIssue *i = find(r, [](const LogIssue &x){
        return x.kind == LogIssueKind::MissingAsset
            && x.target.contains("foo.nif");
    });
    check("NIF load error captured as MissingAsset", i != nullptr);
}

// Repeated errors must fold into a single issue - OpenMW's loader will
// complain about the same missing master every time anything references
// it, and an 80-entry dialog is useless.
static void testDedup()
{
    std::cout << "testDedup\n";
    QString log;
    for (int n = 0; n < 20; ++n) {
        log += "[20:00:00.000 E] Fatal error: File A.esp asks for parent "
               "file B.esm, but it is not available\n";
    }
    auto r = triageOpenMWLog(log, {});
    int hits = 0;
    for (const auto &i : r.issues)
        if (i.kind == LogIssueKind::MissingMaster) ++hits;
    check("same MissingMaster collapses to one issue", hits == 1,
          QString::number(hits), QStringLiteral("1"));
}

// errorLines counts EVERY [... E] / Fatal / Error line we scanned - even
// ones we classified.  It's a "triage saw N, matched M" stat, not a
// "unclassified" stat.
static void testErrorLinesCounted()
{
    std::cout << "testErrorLinesCounted\n";
    const QString log =
        "[20:00:00.000 E] Fatal error: File A.esp asks for parent file B.esm,\n"
        "[20:00:01.000 E] Fatal error: Failed loading C.esp: the content file does not exist\n"
        "[20:00:02.000 I] Loaded master Morrowind.esm\n"      // NOT error
        "[20:00:03.000 E] Error: something we don't recognise\n";
    auto r = triageOpenMWLog(log, {});
    check("errorLines counts E+Fatal lines", r.errorLines == 3,
          QString::number(r.errorLines), QStringLiteral("3"));
}

// Unclassified Error lines become OtherError rows so the user still sees
// them - critical when OpenMW ships a new error string we haven't taught
// the parser about yet.
static void testUnknownErrorFallsIntoOther()
{
    std::cout << "testUnknownErrorFallsIntoOther\n";
    const QString log =
        "[20:00:00.000 E] Error: something unprecedented just happened\n";
    auto r = triageOpenMWLog(log, {});
    const LogIssue *i = find(r, [](const LogIssue &x){
        return x.kind == LogIssueKind::OtherError;
    });
    check("unknown error captured as OtherError", i != nullptr);
    if (i)
        check("OtherError target has the raw line",
              i->target.contains("unprecedented"), i->target);
}

// Non-error lines (Info, Debug) must not produce any issue.  Guards
// against a regex accidentally matching normal log noise.
static void testIgnoresNonErrorLines()
{
    std::cout << "testIgnoresNonErrorLines\n";
    const QString log =
        "[20:00:00.000 I] OpenMW version 0.49.0\n"
        "[20:00:00.001 D] Loading settings.cfg\n"
        "[20:00:00.002 I] Data path /home/x/Data Files\n";
    auto r = triageOpenMWLog(log, {});
    check("info/debug lines produce zero issues", r.issues.isEmpty());
    check("info/debug lines don't bump errorLines", r.errorLines == 0,
          QString::number(r.errorLines), QStringLiteral("0"));
}

// OtherError is capped at 10 per report - a crash that emits thousands
// of unclassified errors must not explode the dialog.
static void testOtherErrorCap()
{
    std::cout << "testOtherErrorCap\n";
    QString log;
    for (int n = 0; n < 50; ++n) {
        log += QString("[20:00:%1.000 E] Error: distinct message %1\n")
               .arg(n, 2, 10, QChar('0'));
    }
    auto r = triageOpenMWLog(log, {});
    int others = 0;
    for (const auto &i : r.issues)
        if (i.kind == LogIssueKind::OtherError) ++others;
    check("OtherError row count is capped", others <= 10,
          QString::number(others), QStringLiteral("<=10"));
    check("errorLines still reflects all seen lines", r.errorLines == 50,
          QString::number(r.errorLines), QStringLiteral("50"));
}

// Case-insensitive plugin lookup - OpenMW prints ".ESP" in some places,
// ".esp" in others, and mod filenames on disk can be ".Esp".  If a log
// names FOO.ESP and the mod owns foo.esp, we still name the mod.
static void testPluginCaseInsensitive()
{
    std::cout << "testPluginCaseInsensitive\n";
    const QString log =
        "[20:00:00.000 E] Fatal error: Failed loading FOO.ESP: the content "
        "file does not exist\n";
    QList<TriageMod> mods = {{"Foo Mod", {"foo.esp"}}};
    auto r = triageOpenMWLog(log, mods);
    const LogIssue *i = find(r, [](const LogIssue &x){
        return x.kind == LogIssueKind::MissingPlugin;
    });
    check("case-insensitive mod resolution",
          i && i->suspectMod == "Foo Mod",
          i ? i->suspectMod : QString(), QStringLiteral("Foo Mod"));
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    testEmptyLog();
    testMissingMaster();
    testMissingPlugin();
    testMissingTexture();
    testMissingMesh();
    testDedup();
    testErrorLinesCounted();
    testUnknownErrorFallsIntoOther();
    testIgnoresNonErrorLines();
    testOtherErrorCap();
    testPluginCaseInsensitive();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
