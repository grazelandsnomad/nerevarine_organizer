// tests/test_master_satisfaction.cpp
//
// End-to-end test for openmw::findUnsatisfiedMasters - writes real TES3
// plugins (only a MAST record, no HEDR/DATA body) to a tmpdir and verifies
// the detection pipeline suppresses plugins whose parents are missing.
//
// The motivating scenario is pinned by `testHlaaluSeydaNeenAFFreshScenario`
// - Hlaalu Seyda Neen bundles optional patches for AFFresh / TR / etc., and
// enabling the AFFresh patch without AFFresh.esm installed used to crash
// OpenMW at launch with "File X asks for parent file Y, but it is not
// available or has been loaded in the wrong order".
//
// Build + run:
//   ./build/tests/test_master_satisfaction

#include "master_satisfaction.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstring>
#include <iostream>

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

// -- TES3 fixture helpers (mirror tests/test_plugin_parser.cpp) ---

static QByteArray uint32le(quint32 v) {
    QByteArray b(4, '\0');
    std::memcpy(b.data(), &v, 4);
    return b;
}

static QByteArray masterSubrecord(const QByteArray &name)
{
    QByteArray payload = name;
    payload.append('\0');
    QByteArray sub;
    sub.append("MAST", 4);
    sub.append(uint32le(payload.size()));
    sub.append(payload);
    return sub;
}

static QByteArray dataSubrecord(quint64 fileSize)
{
    QByteArray payload(8, '\0');
    std::memcpy(payload.data(), &fileSize, 8);
    QByteArray sub;
    sub.append("DATA", 4);
    sub.append(uint32le(8));
    sub.append(payload);
    return sub;
}

static void writeTes3(const QString &path, const QStringList &masters)
{
    QByteArray body;
    for (const QString &m : masters) {
        body.append(masterSubrecord(m.toLatin1()));
        body.append(dataSubrecord(0));
    }

    QByteArray file;
    file.append("TES3", 4);
    file.append(uint32le(body.size()));
    file.append(QByteArray(8, '\0'));
    file.append(body);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(file);
    f.close();
}

using openmw::findUnsatisfiedMasters;

// -- Tests ---

// A plugin whose declared MAST is satisfied stays put.
static void testSatisfiedMasterNotSuppressed(QDir &root)
{
    std::cout << "testSatisfiedMasterNotSuppressed\n";

    const QString p = root.filePath("ok.esp");
    writeTes3(p, {"Morrowind.esm"});

    QSet<QString> available{"morrowind.esm"};
    const auto bad = findUnsatisfiedMasters({{"ok.esp", p}}, available);

    check("satisfied plugin not reported", !bad.contains("ok.esp"));
    check("no other entries returned",      bad.isEmpty());
}

// A plugin whose MAST isn't in the available set gets reported.
static void testMissingMasterReported(QDir &root)
{
    std::cout << "testMissingMasterReported\n";

    const QString p = root.filePath("needs_missing.esp");
    writeTes3(p, {"Morrowind.esm", "DoesNotExist.esm"});

    QSet<QString> available{"morrowind.esm"};
    const auto bad = findUnsatisfiedMasters({{"needs_missing.esp", p}}, available);

    check("plugin with missing master is reported",
          bad.contains("needs_missing.esp"));
}

// Case-insensitive match - "AFFresh.esm" in MAST vs "affresh.esm" in
// availableLower must still count as available.
static void testMasterCaseInsensitive(QDir &root)
{
    std::cout << "testMasterCaseInsensitive\n";

    const QString p = root.filePath("case_mix.esp");
    writeTes3(p, {"AFFresh.esm"});   // modder's spelling

    QSet<QString> available{"affresh.esm"}; // availableLower is lowercased
    const auto bad = findUnsatisfiedMasters({{"case_mix.esp", p}}, available);

    check("case-differing master counts as available", bad.isEmpty());
}

// Transitive chain: A depends on B, B depends on missing master. Both get
// suppressed in a single call.
static void testTransitiveSuppression(QDir &root)
{
    std::cout << "testTransitiveSuppression\n";

    const QString b = root.filePath("b.esp");
    const QString a = root.filePath("a.esp");
    writeTes3(b, {"Missing.esm"});        // B depends on something not on disk
    writeTes3(a, {"b.esp"});              // A depends on B

    // Seed availableLower with both peer plugins - the fixpoint should
    // drop B first, then catch A on the next pass.
    QSet<QString> available{"morrowind.esm", "a.esp", "b.esp"};
    const auto bad = findUnsatisfiedMasters(
        {{"b.esp", b}, {"a.esp", a}},
        available);

    check("B suppressed (direct missing master)", bad.contains("b.esp"));
    check("A suppressed transitively",            bad.contains("a.esp"));
}

// The originating bug: Hlaalu Seyda Neen bundles a patch ESP that requires
// AFFresh.esm, which the user didn't install.  Simulates a full modlist:
// the host plugin loads fine, the AFFresh patch gets suppressed, the
// Nine-holes patch (where the companion IS installed) stays.
static void testHlaaluSeydaNeenAFFreshScenario(QDir &root)
{
    std::cout << "testHlaaluSeydaNeenAFFreshScenario\n";

    const QString host        = root.filePath("Hlaalu Seyda Neen.esp");
    const QString ninePatch   = root.filePath("HlaaluSeydaNeen_NineHoles_Patch.ESP");
    const QString affreshPatch= root.filePath("HlaaluSeydaNeen_AFFresh_Patch.ESP");
    const QString nineHoles   = root.filePath("Nine-holes.esp");

    writeTes3(host,         {"Morrowind.esm"});
    writeTes3(nineHoles,    {"Morrowind.esm"});
    writeTes3(ninePatch,    {"Morrowind.esm", "Hlaalu Seyda Neen.esp",
                             "Nine-holes.esp"});
    writeTes3(affreshPatch, {"Morrowind.esm", "Hlaalu Seyda Neen.esp",
                             "AFFresh.esm"}); // AFFresh.esm NOT installed

    QSet<QString> available{
        "morrowind.esm", "tribunal.esm", "bloodmoon.esm",
        "hlaalu seyda neen.esp",
        "nine-holes.esp",
        "hlaaluseydaneen_nineholes_patch.esp",
        "hlaaluseydaneen_affresh_patch.esp",
    };

    const auto bad = findUnsatisfiedMasters(
        {
            {"Hlaalu Seyda Neen.esp",                 host},
            {"Nine-holes.esp",                        nineHoles},
            {"HlaaluSeydaNeen_NineHoles_Patch.ESP",   ninePatch},
            {"HlaaluSeydaNeen_AFFresh_Patch.ESP",     affreshPatch},
        },
        available);

    check("host plugin NOT suppressed",
          !bad.contains("Hlaalu Seyda Neen.esp"));
    check("Nine-holes.esp NOT suppressed",
          !bad.contains("Nine-holes.esp"));
    check("Nine-holes patch (companion installed) NOT suppressed",
          !bad.contains("HlaaluSeydaNeen_NineHoles_Patch.ESP"));
    check("AFFresh patch (companion MISSING) IS suppressed",
          bad.contains("HlaaluSeydaNeen_AFFresh_Patch.ESP"));
}

// Empty inputs shouldn't crash and should return an empty set.
static void testEmptyInputsNoop()
{
    std::cout << "testEmptyInputsNoop\n";

    QSet<QString> available;
    const auto bad = findUnsatisfiedMasters({}, available);
    check("empty input returns empty set", bad.isEmpty());
}

// A plugin file that doesn't exist on disk → readTes3Masters returns
// empty → no masters to fail → not reported.  Documents current behaviour;
// missing plugin files are handled by the upstream scrub, not here.
static void testMissingPluginFileNotReported(QDir &root)
{
    std::cout << "testMissingPluginFileNotReported\n";

    QSet<QString> available{"morrowind.esm"};
    const auto bad = findUnsatisfiedMasters(
        {{"never_written.esp", root.filePath("never_written.esp")}},
        available);
    check("absent plugin file treated as empty-masters",
          !bad.contains("never_written.esp"));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "=== master_satisfaction tests ===\n";

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::cout << "Could not create temp dir\n";
        return 2;
    }
    QDir root(tmp.path());

    testSatisfiedMasterNotSuppressed(root);
    testMissingMasterReported(root);
    testMasterCaseInsensitive(root);
    testTransitiveSuppression(root);
    testHlaaluSeydaNeenAFFreshScenario(root);
    testEmptyInputsNoop();
    testMissingPluginFileNotReported(root);

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
