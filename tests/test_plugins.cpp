// plugin parser, bsa reader, master satisfaction, plugin/asset collisions.
#include "pluginparser.h"
#include "bsareader.h"
#include "master_satisfaction.h"
#include "plugin_collisions.h"
#include "asset_collisions.h"
#include "log_triage.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstring>
#include <iostream>

#include "test_harness.h"

// ===== plugin_parser =====

// TES3 plugin on disk: "TES3"(4) + recSize(4 LE) + 8 pad, then subrecords
// tag(4)+size(4 LE)+data. Only MAST needed; readTes3Masters skips HEDR.

static QByteArray pp_uint32le(quint32 v) {
    QByteArray b(4, '\0');
    std::memcpy(b.data(), &v, 4);
    return b;
}

static QByteArray pp_masterSubrecord(const QByteArray &name)
{
    QByteArray payload = name;
    payload.append('\0');  // null-terminated
    QByteArray sub;
    sub.append("MAST", 4);
    sub.append(pp_uint32le(payload.size()));
    sub.append(payload);
    return sub;
}

static QByteArray pp_dataSubrecord(quint64 fileSize)
{
    QByteArray payload(8, '\0');
    std::memcpy(payload.data(), &fileSize, 8);
    QByteArray sub;
    sub.append("DATA", 4);
    sub.append(pp_uint32le(8));
    sub.append(payload);
    return sub;
}

static void pp_touchFile(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.close();
}

static void pp_writeTes3(const QString &path, const QStringList &masters)
{
    QByteArray body;
    for (const QString &m : masters) {
        body.append(pp_masterSubrecord(m.toLatin1()));
        body.append(pp_dataSubrecord(0));  // DATA follows each MAST
    }

    QByteArray file;
    file.append("TES3", 4);
    file.append(pp_uint32le(body.size()));
    file.append(QByteArray(8, '\0'));  // padding
    file.append(body);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(file);
    f.close();
}

static void run_plugin_parser()
{
    std::cout << "=== plugin parser tests ===\n";

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::cout << "Could not create temp dir\n";
        return;
    }
    QDir root(tmp.path());

    {
        QString f = root.filePath("one_master.esp");
        pp_writeTes3(f, {"Morrowind.esm"});
        auto m = plugins::readTes3Masters(f);
        check("single-master TES3 parses",
              m.size() == 1 && m[0] == "Morrowind.esm");
    }
    {
        QString f = root.filePath("multi_master.esp");
        pp_writeTes3(f, {"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"});
        auto m = plugins::readTes3Masters(f);
        check("three-master TES3 parses in order",
              m == QStringList{"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"});
    }
    {
        // Wrong magic: empty, no throw.
        QString f = root.filePath("garbage.bin");
        QFile g(f);
        if (g.open(QIODevice::WriteOnly)) {
            g.write(QByteArray(256, 'X'));
            g.close();
        }
        check("non-TES3 file returns empty masters",
              plugins::readTes3Masters(f).isEmpty());
    }
    {
        check("missing file returns empty masters",
              plugins::readTes3Masters(root.filePath("nope.esm")).isEmpty());
    }

    // collectDataFolders: modB/fomod/ skipped, modC nesting found.
    {
        QDir(root.filePath("modA")).mkpath(".");
        pp_touchFile(root.filePath("modA/modA.esp"));

        QDir(root.filePath("modB/00 Core")).mkpath(".");
        pp_touchFile(root.filePath("modB/00 Core/OAAB_Data.esm"));
        QDir(root.filePath("modB/fomod")).mkpath(".");
        pp_touchFile(root.filePath("modB/fomod/ModuleConfig.xml"));
        // .esp inside fomod/ must not be picked up
        pp_touchFile(root.filePath("modB/fomod/plugin_inside_fomod.esp"));

        QDir(root.filePath("modC/Data Files/nested")).mkpath(".");
        pp_touchFile(root.filePath("modC/Data Files/nested/mod.esp"));

        auto exts = plugins::contentExtensions();

        auto fa = plugins::collectDataFolders(root.filePath("modA"), exts);
        check("modA: root-level .esp picked up",
              fa.size() == 1 && fa[0].second == QStringList{"modA.esp"});

        auto fb = plugins::collectDataFolders(root.filePath("modB"), exts);
        bool fbOk = (fb.size() == 1)
                 && fb[0].second == QStringList{"OAAB_Data.esm"}
                 && fb[0].first.endsWith("00 Core");
        check("modB: fomod/ is skipped, 00 Core/ picked up", fbOk);

        auto fc = plugins::collectDataFolders(root.filePath("modC"), exts);
        bool fcOk = (fc.size() == 1)
                 && fc[0].second == QStringList{"mod.esp"};
        check("modC: two-deep nesting is found", fcOk);
    }
    {
        // Files at depth N still get scanned when maxDepth=N (scan runs
        // before the descend check), so put the plugin one level beyond:
        // depth 7 against default maxDepth=6.
        QString deep = root.filePath("deep/a/b/c/d/e/f/g/"); // 7 subdirs below "deep"
        QDir().mkpath(deep);
        pp_touchFile(deep + "plugin.esp");

        auto exts = plugins::contentExtensions();
        auto miss = plugins::collectDataFolders(root.filePath("deep"), exts, 6);
        auto hit  = plugins::collectDataFolders(root.filePath("deep"), exts, 8);
        check("maxDepth=6 skips plugin at depth 7",
              miss.isEmpty() || (miss.size() == 1 && !miss[0].second.contains("plugin.esp")));
        check("maxDepth=8 finds deep plugin",
              hit.size() == 1 && hit[0].second == QStringList{"plugin.esp"});
    }
    {
        check("non-existent root returns empty list",
              plugins::collectDataFolders(
                  root.filePath("absolutely_not_there"),
                  plugins::contentExtensions()).isEmpty());
    }
}

// ===== bsa_reader =====

static QByteArray br_u32le(quint32 v)
{
    QByteArray b(4, '\0');
    std::memcpy(b.data(), &v, 4);
    return b;
}

// TES3 BSA with `names` as its file table. File data omitted; the reader
// never touches it. Layout: see src/bsareader.cpp.
static QByteArray br_buildTes3Bsa(const QStringList &names)
{
    const int N = names.size();

    // Pack null-terminated names; record each block-relative offset.
    QByteArray nameBlock;
    QList<quint32> nameOffsets;
    for (const QString &n : names) {
        nameOffsets << quint32(nameBlock.size());
        nameBlock += n.toLatin1();
        nameBlock += '\0';
    }

    // hashOffset = bytes from end of 12-byte header to the hash table:
    // 8*N file records + 4*N name offsets + nameBlock.
    const quint32 hashOffset = quint32(8 * N + 4 * N + nameBlock.size());

    QByteArray out;
    out += br_u32le(0x100);               // version
    out += br_u32le(hashOffset);
    out += br_u32le(quint32(N));          // fileCount

    for (int i = 0; i < N; ++i) {
        out += br_u32le(0);               // record size
        out += br_u32le(0);               // record offset
    }
    for (int i = 0; i < N; ++i)
        out += br_u32le(nameOffsets[i]);

    out += nameBlock;

    // Hash table: 8*N bytes, content ignored by the reader.
    out.append(8 * N, '\0');

    return out;
}

static QString br_writeBsa(const QTemporaryDir &dir, const QString &name,
                           const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {};
    f.write(bytes);
    f.close();
    return path;
}

static void br_testHappyPath()
{
    std::cout << "testHappyPath\n";

    QTemporaryDir dir;
    const QStringList in{
        "meshes\\foo.nif",
        "textures\\BAR.DDS",
        "sound\\fx\\hit.wav"
    };
    const QString path = br_writeBsa(dir, "ok.bsa", br_buildTes3Bsa(in));
    const QStringList got = bsa::listTes3BsaFiles(path);

    check("three files returned", got.size() == 3);
    check("backslashes → forward slashes",
          got.contains("meshes/foo.nif"));
    check("case lowered",
          got.contains("textures/bar.dds"));
    check("nested separator kept",
          got.contains("sound/fx/hit.wav"));
}

static void br_testMissingFile()
{
    std::cout << "testMissingFile\n";

    const QStringList got = bsa::listTes3BsaFiles("/definitely/not/here.bsa");
    check("missing file returns empty, not error", got.isEmpty());
}

static void br_testWrongVersion()
{
    std::cout << "testWrongVersion\n";

    // Valid layout, version != 0x100: a Skyrim/Oblivion BSA in a mod
    // folder. Should silently skip.
    QTemporaryDir dir;
    QByteArray blob = br_buildTes3Bsa({"meshes\\foo.nif"});
    // version -> 0x67 (TES4-era)
    QByteArray v = br_u32le(0x67);
    std::memcpy(blob.data(), v.constData(), 4);

    const QString path = br_writeBsa(dir, "skyrim-ish.bsa", blob);
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("non-TES3 BSA returns empty", got.isEmpty());
}

static void br_testTruncated()
{
    std::cout << "testTruncated\n";

    QTemporaryDir dir;
    QByteArray blob = br_buildTes3Bsa({"meshes\\foo.nif", "textures\\bar.dds"});
    blob.truncate(blob.size() / 2);

    const QString path = br_writeBsa(dir, "truncated.bsa", blob);
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("truncated file returns empty, doesn't crash", got.isEmpty());
}

static void br_testTooShort()
{
    std::cout << "testTooShort\n";

    QTemporaryDir dir;
    const QString path = br_writeBsa(dir, "tiny.bsa", QByteArray("ab", 2));
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("sub-header file returns empty", got.isEmpty());
}

static void br_testEmptyArchive()
{
    std::cout << "testEmptyArchive\n";

    QTemporaryDir dir;
    const QString path = br_writeBsa(dir, "empty.bsa", br_buildTes3Bsa({}));
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("empty archive returns empty list", got.isEmpty());
}

// Conflict scanner must still see a one-file BSA (single retexture), not
// treat it as empty/broken.
static void br_testSingleFileArchive()
{
    std::cout << "testSingleFileArchive\n";

    QTemporaryDir dir;
    const QString path = br_writeBsa(dir, "one.bsa",
                                     br_buildTes3Bsa({"meshes\\lonely.nif"}));
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("one entry returned", got.size() == 1);
    check("normalised separator and case",
          got.contains("meshes/lonely.nif"));
}

// Duplicate filename entries pass through, not deduped: conflict inspector
// wants one Provider per TOC row; dedup happens higher up.
static void br_testDuplicateEntries()
{
    std::cout << "testDuplicateEntries\n";

    QTemporaryDir dir;
    const QStringList in{
        "meshes\\dup.nif", "meshes\\dup.nif", "meshes\\dup.nif"
    };
    const QString path = br_writeBsa(dir, "dup.bsa", br_buildTes3Bsa(in));
    const QStringList got = bsa::listTes3BsaFiles(path);

    check("three entries returned (duplicates preserved)", got.size() == 3);
    int dupCount = 0;
    for (const QString &e : got) if (e == "meshes/dup.nif") ++dupCount;
    check("all three are the same normalised path", dupCount == 3);
}

// Name offset past the name block must fail safely: no crash, no OOB read,
// returns empty.
static void br_testNameOffsetOutOfRange()
{
    std::cout << "testNameOffsetOutOfRange\n";

    QTemporaryDir dir;
    QByteArray blob = br_buildTes3Bsa({"meshes\\foo.nif", "textures\\bar.dds"});

    // Second name offset (N=2) lives at byte 32. Set it past the name block.
    QByteArray huge = br_u32le(0xFFFFFFFF);
    std::memcpy(blob.data() + 32, huge.constData(), 4);

    const QString path = br_writeBsa(dir, "oor.bsa", blob);
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("out-of-range offset → empty, no crash", got.isEmpty());
}

// hashOffset below the mandatory sections implies a negative-size name
// block; parser must return empty, not do a negative read.
static void br_testHashOffsetTooSmall()
{
    std::cout << "testHashOffsetTooSmall\n";

    QTemporaryDir dir;
    QByteArray blob = br_buildTes3Bsa({"a", "b", "c"});
    // hashOffset (byte 4) = 12, below the 12*N=36 needed for N=3.
    QByteArray tiny = br_u32le(12);
    std::memcpy(blob.data() + 4, tiny.constData(), 4);

    const QString path = br_writeBsa(dir, "tiny-hash.bsa", blob);
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("hashOffset < space required → empty", got.isEmpty());
}

// Absurd fileCount must not allocate multi-GB vectors or spin; parser
// caps at 1e6.
static void br_testAbsurdFileCount()
{
    std::cout << "testAbsurdFileCount\n";

    QTemporaryDir dir;
    QByteArray blob;
    blob += br_u32le(0x100);           // version
    blob += br_u32le(0);               // hashOffset - moot under the cap
    blob += br_u32le(0xFFFFFFFFu);     // fileCount

    const QString path = br_writeBsa(dir, "evil.bsa", blob);
    const QStringList got = bsa::listTes3BsaFiles(path);
    check("huge fileCount → empty, no OOM", got.isEmpty());
}

static void run_bsa_reader()
{
    br_testHappyPath();
    br_testMissingFile();
    br_testWrongVersion();
    br_testTruncated();
    br_testTooShort();
    br_testEmptyArchive();
    br_testSingleFileArchive();
    br_testDuplicateEntries();
    br_testNameOffsetOutOfRange();
    br_testHashOffsetTooSmall();
    br_testAbsurdFileCount();
}

// ===== master_satisfaction =====

// TES3 fixture helpers, same as the plugin-parser section.

static QByteArray ms_uint32le(quint32 v) {
    QByteArray b(4, '\0');
    std::memcpy(b.data(), &v, 4);
    return b;
}

static QByteArray ms_masterSubrecord(const QByteArray &name)
{
    QByteArray payload = name;
    payload.append('\0');
    QByteArray sub;
    sub.append("MAST", 4);
    sub.append(ms_uint32le(payload.size()));
    sub.append(payload);
    return sub;
}

static QByteArray ms_dataSubrecord(quint64 fileSize)
{
    QByteArray payload(8, '\0');
    std::memcpy(payload.data(), &fileSize, 8);
    QByteArray sub;
    sub.append("DATA", 4);
    sub.append(ms_uint32le(8));
    sub.append(payload);
    return sub;
}

static void ms_writeTes3(const QString &path, const QStringList &masters)
{
    QByteArray body;
    for (const QString &m : masters) {
        body.append(ms_masterSubrecord(m.toLatin1()));
        body.append(ms_dataSubrecord(0));
    }

    QByteArray file;
    file.append("TES3", 4);
    file.append(ms_uint32le(body.size()));
    file.append(QByteArray(8, '\0'));
    file.append(body);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(file);
    f.close();
}

using openmw::findUnsatisfiedMasters;

// Declared MAST is satisfied -> plugin stays put.
static void ms_testSatisfiedMasterNotSuppressed(QDir &root)
{
    std::cout << "testSatisfiedMasterNotSuppressed\n";

    const QString p = root.filePath("ok.esp");
    ms_writeTes3(p, {"Morrowind.esm"});

    QSet<QString> available{"morrowind.esm"};
    const auto bad = findUnsatisfiedMasters({{"ok.esp", p}}, available);

    check("satisfied plugin not reported", !bad.contains("ok.esp"));
    check("no other entries returned",      bad.isEmpty());
}

// MAST not in the available set -> reported.
static void ms_testMissingMasterReported(QDir &root)
{
    std::cout << "testMissingMasterReported\n";

    const QString p = root.filePath("needs_missing.esp");
    ms_writeTes3(p, {"Morrowind.esm", "DoesNotExist.esm"});

    QSet<QString> available{"morrowind.esm"};
    const auto bad = findUnsatisfiedMasters({{"needs_missing.esp", p}}, available);

    check("plugin with missing master is reported",
          bad.contains("needs_missing.esp"));
}

// Case-insensitive: "AFFresh.esm" in MAST vs "affresh.esm" available still
// counts as available.
static void ms_testMasterCaseInsensitive(QDir &root)
{
    std::cout << "testMasterCaseInsensitive\n";

    const QString p = root.filePath("case_mix.esp");
    ms_writeTes3(p, {"AFFresh.esm"});   // modder's spelling

    QSet<QString> available{"affresh.esm"}; // availableLower is lowercased
    const auto bad = findUnsatisfiedMasters({{"case_mix.esp", p}}, available);

    check("case-differing master counts as available", bad.isEmpty());
}

// Transitive: A depends on B, B depends on a missing master. Both
// suppressed in one call.
static void ms_testTransitiveSuppression(QDir &root)
{
    std::cout << "testTransitiveSuppression\n";

    const QString b = root.filePath("b.esp");
    const QString a = root.filePath("a.esp");
    ms_writeTes3(b, {"Missing.esm"});        // B depends on something not on disk
    ms_writeTes3(a, {"b.esp"});              // A depends on B

    // Seed both peer plugins; the fixpoint drops B first, then catches A
    // on the next pass.
    QSet<QString> available{"morrowind.esm", "a.esp", "b.esp"};
    const auto bad = findUnsatisfiedMasters(
        {{"b.esp", b}, {"a.esp", a}},
        available);

    check("B suppressed (direct missing master)", bad.contains("b.esp"));
    check("A suppressed transitively",            bad.contains("a.esp"));
}

// Originating bug: Hlaalu Seyda Neen bundles a patch ESP needing AFFresh.esm
// which wasn't installed. Host loads fine, AFFresh patch is suppressed, the
// Nine-holes patch (companion IS installed) stays.
static void ms_testHlaaluSeydaNeenAFFreshScenario(QDir &root)
{
    std::cout << "testHlaaluSeydaNeenAFFreshScenario\n";

    const QString host        = root.filePath("Hlaalu Seyda Neen.esp");
    const QString ninePatch   = root.filePath("HlaaluSeydaNeen_NineHoles_Patch.ESP");
    const QString affreshPatch= root.filePath("HlaaluSeydaNeen_AFFresh_Patch.ESP");
    const QString nineHoles   = root.filePath("Nine-holes.esp");

    ms_writeTes3(host,         {"Morrowind.esm"});
    ms_writeTes3(nineHoles,    {"Morrowind.esm"});
    ms_writeTes3(ninePatch,    {"Morrowind.esm", "Hlaalu Seyda Neen.esp",
                             "Nine-holes.esp"});
    ms_writeTes3(affreshPatch, {"Morrowind.esm", "Hlaalu Seyda Neen.esp",
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

// Empty inputs: no crash, empty set.
static void ms_testEmptyInputsNoop()
{
    std::cout << "testEmptyInputsNoop\n";

    QSet<QString> available;
    const auto bad = findUnsatisfiedMasters({}, available);
    check("empty input returns empty set", bad.isEmpty());
}

// Plugin file absent on disk -> readTes3Masters empty -> no masters to fail
// -> not reported. Missing files are handled by the upstream scrub, not here.
static void ms_testMissingPluginFileNotReported(QDir &root)
{
    std::cout << "testMissingPluginFileNotReported\n";

    QSet<QString> available{"morrowind.esm"};
    const auto bad = findUnsatisfiedMasters(
        {{"never_written.esp", root.filePath("never_written.esp")}},
        available);
    check("absent plugin file treated as empty-masters",
          !bad.contains("never_written.esp"));
}

static void run_master_satisfaction()
{
    std::cout << "=== master_satisfaction tests ===\n";

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::cout << "Could not create temp dir\n";
        return;
    }
    QDir root(tmp.path());

    ms_testSatisfiedMasterNotSuppressed(root);
    ms_testMissingMasterReported(root);
    ms_testMasterCaseInsensitive(root);
    ms_testTransitiveSuppression(root);
    ms_testHlaaluSeydaNeenAFFreshScenario(root);
    ms_testEmptyInputsNoop();
    ms_testMissingPluginFileNotReported(root);
}

// ===== plugin_collisions =====

using openmw::CollisionInput;
using openmw::findPluginBasenameCollisions;

static CollisionInput pc_mod(const QString &label,
                             const QString &dataRoot,
                             const QStringList &plugins)
{
    CollisionInput ci;
    ci.modLabel = label;
    ci.pluginDirs = {{dataRoot, plugins}};
    return ci;
}

// Empty input -> empty report.
static void pc_testEmpty()
{
    std::cout << "testEmpty\n";
    auto r = findPluginBasenameCollisions({});
    check("no collisions on empty input",
          r.collisions.isEmpty() && r.totalPluginsChecked == 0);
}

// Disjoint plugin sets -> no collisions.
static void pc_testDisjoint()
{
    std::cout << "testDisjoint\n";
    QList<CollisionInput> mods = {
        pc_mod("A", "/mods/A", {"A.esp"}),
        pc_mod("B", "/mods/B", {"B.esp"}),
    };
    auto r = findPluginBasenameCollisions(mods);
    check("two mods, two plugins, no overlap",
          r.collisions.isEmpty() && r.totalPluginsChecked == 2);
}

// Motivating case: Rocky_WG_Base_1.1.esp in three mods.
static void pc_testRockyWGCollision()
{
    std::cout << "testRockyWGCollision\n";
    QList<CollisionInput> mods = {
        pc_mod("Rocky West Gash",
            "/mods/Rocky_WG_Base",
            {"Rocky_WG_Base_1.1.esp"}),
        pc_mod("Caldera Priory",
            "/mods/CalderaPriory/01 Rocky West Gash Patch",
            {"Rocky_WG_Base_1.1.esp"}),
        pc_mod("Caldera Priory",
            "/mods/CalderaPriory/03 Rocky WG Aggressively Compatible",
            {"Rocky_WG_Base_1.1.esp"}),
    };
    auto r = findPluginBasenameCollisions(mods);
    check("one collision surfaced",
          r.collisions.size() == 1,
          QString::number(r.collisions.size()));
    if (r.collisions.isEmpty()) return;

    const auto &c = r.collisions.first();
    check("collision basename captured case-preserved",
          c.basename == "Rocky_WG_Base_1.1.esp");
    check("three providers recorded", c.providers.size() == 3,
          QString::number(c.providers.size()));

    // This triple is the exact shape the Caldera Priory FOMOD produced;
    // keep the assertion specific.
    bool baseSeen = false, p01Seen = false, p03Seen = false;
    for (const auto &p : c.providers) {
        if (p.dataRoot.endsWith("Rocky_WG_Base"))                               baseSeen = true;
        if (p.dataRoot.endsWith("01 Rocky West Gash Patch"))                    p01Seen = true;
        if (p.dataRoot.endsWith("03 Rocky WG Aggressively Compatible"))         p03Seen = true;
    }
    check("base mod provider present",     baseSeen);
    check("01 patch provider present",     p01Seen);
    check("03 patch provider present",     p03Seen);
}

// FOO.ESP and foo.esp are the same VFS entry everywhere that matters; the
// detector must collapse them.
static void pc_testCaseInsensitive()
{
    std::cout << "testCaseInsensitive\n";
    QList<CollisionInput> mods = {
        pc_mod("Upper", "/mods/U", {"FOO.ESP"}),
        pc_mod("Lower", "/mods/L", {"foo.esp"}),
    };
    auto r = findPluginBasenameCollisions(mods);
    check("case-different names reported as a single collision",
          r.collisions.size() == 1);
    if (r.collisions.isEmpty()) return;
    // First-seen spelling wins for display; here "FOO.ESP".
    check("display basename is first-seen spelling",
          r.collisions.first().basename == "FOO.ESP",
          r.collisions.first().basename);
}

// Same (mod, root) pair with the same basename twice must NOT self-collide;
// only >1 distinct provider counts. Guards the caller double-building
// pluginDirs.
static void pc_testSameModSameRootNoSelfCollision()
{
    std::cout << "testSameModSameRootNoSelfCollision\n";
    CollisionInput ci;
    ci.modLabel = "Dup";
    ci.pluginDirs = {
        {"/mods/D", {"Dup.esp"}},
        {"/mods/D", {"Dup.esp"}},  // identical
    };
    auto r = findPluginBasenameCollisions({ci});
    check("identical (modLabel, dataRoot) pairs don't self-collide",
          r.collisions.isEmpty());
}

// Same mod, two different data roots, same basename: a real within-mod
// FOMOD-extract bug, must be reported.
static void pc_testSameModDifferentRootsIsReported()
{
    std::cout << "testSameModDifferentRootsIsReported\n";
    CollisionInput ci;
    ci.modLabel = "FomodMess";
    ci.pluginDirs = {
        {"/mods/F/01 Core",   {"Plugin.esp"}},
        {"/mods/F/02 Altern", {"Plugin.esp"}},
    };
    auto r = findPluginBasenameCollisions({ci});
    check("same mod, two roots, same basename → reported",
          r.collisions.size() == 1);
    if (!r.collisions.isEmpty()) {
        check("both data roots listed",
              r.collisions.first().providers.size() == 2);
    }
}

// Output sorted by basename (case-insensitive) so the Inspector order
// doesn't depend on modlist order.
static void pc_testDeterministicOrdering()
{
    std::cout << "testDeterministicOrdering\n";
    QList<CollisionInput> mods = {
        pc_mod("M1", "/m1", {"Zebra.esp", "Alpha.esp"}),
        pc_mod("M2", "/m2", {"Zebra.esp", "Alpha.esp"}),
    };
    auto r = findPluginBasenameCollisions(mods);
    check("two collisions", r.collisions.size() == 2);
    if (r.collisions.size() >= 2) {
        check("Alpha sorted before Zebra",
              r.collisions[0].basename.toLower() <
              r.collisions[1].basename.toLower(),
              r.collisions[0].basename + " / " + r.collisions[1].basename);
    }
}

// totalPluginsChecked counts every basename seen, not just colliding ones,
// so the UI can say "checked N plugins, found M collisions".
static void pc_testTotalCountsNonCollidingToo()
{
    std::cout << "testTotalCountsNonCollidingToo\n";
    QList<CollisionInput> mods = {
        pc_mod("A", "/a", {"Shared.esp", "A_only.esp"}),
        pc_mod("B", "/b", {"Shared.esp", "B_only.esp"}),
    };
    auto r = findPluginBasenameCollisions(mods);
    check("total counts 4 plugin rows even though only 1 collides",
          r.totalPluginsChecked == 4,
          QString::number(r.totalPluginsChecked));
    check("exactly one collision", r.collisions.size() == 1);
}

static void run_plugin_collisions()
{
    pc_testEmpty();
    pc_testDisjoint();
    pc_testRockyWGCollision();
    pc_testCaseInsensitive();
    pc_testSameModSameRootNoSelfCollision();
    pc_testSameModDifferentRootsIsReported();
    pc_testDeterministicOrdering();
    pc_testTotalCountsNonCollidingToo();
}

// ===== asset_collisions =====

using openmw::AssetCaseInput;
using openmw::findAssetCaseCollisions;

// Empty input -> empty report.
static void ac_testEmpty()
{
    std::cout << "testEmpty\n";
    auto r = findAssetCaseCollisions({});
    check("no hits on empty input",
          r.mods.isEmpty() && r.totalFilesChecked == 0);
}

// All paths distinct even case-insensitively -> no hits.
static void ac_testNoCollision()
{
    std::cout << "testNoCollision\n";
    AssetCaseInput in;
    in.modLabel = "Clean";
    in.dataRoot = "/mods/Clean";
    in.relPaths = {"Scripts/Player.lua", "Meshes/Actor.nif", "Textures/Actor.dds"};
    auto r = findAssetCaseCollisions({in});
    check("disjoint paths produce no hits", r.mods.isEmpty());
    check("total files counted", r.totalFilesChecked == 3,
          QString::number(r.totalFilesChecked), "3");
}

// Motivating case: Player.lua + player.lua in the same data root.
static void ac_testLuaCaseCollision()
{
    std::cout << "testLuaCaseCollision\n";
    AssetCaseInput in;
    in.modLabel = "SomeMod";
    in.dataRoot = "/mods/SomeMod/Data Files";
    in.relPaths = {"Scripts/Player.lua", "Scripts/player.lua", "Meshes/actor.nif"};
    auto r = findAssetCaseCollisions({in});
    check("one mod affected", r.mods.size() == 1,
          QString::number(r.mods.size()), "1");
    check("total files counted", r.totalFilesChecked == 3,
          QString::number(r.totalFilesChecked), "3");
    if (r.mods.isEmpty()) return;
    const auto &m = r.mods.first();
    check("modLabel preserved", m.modLabel == "SomeMod");
    check("dataRoot preserved", m.dataRoot == "/mods/SomeMod/Data Files");
    check("one hit", m.hits.size() == 1,
          QString::number(m.hits.size()), "1");
    if (m.hits.isEmpty()) return;
    const auto &h = m.hits.first();
    check("lowercased key correct", h.lowercasedRel == "scripts/player.lua",
          h.lowercasedRel, "scripts/player.lua");
    check("both spellings present", h.spellings.size() == 2,
          QString::number(h.spellings.size()), "2");
    check("spellings contain Player.lua", h.spellings.contains("Scripts/Player.lua"));
    check("spellings contain player.lua", h.spellings.contains("Scripts/player.lua"));
}

// Exact duplicate paths (same spelling twice) must NOT self-collide.
static void ac_testExactDuplicateNoSelfCollision()
{
    std::cout << "testExactDuplicateNoSelfCollision\n";
    AssetCaseInput in;
    in.modLabel = "Dup";
    in.dataRoot = "/mods/Dup";
    in.relPaths = {"Scripts/Foo.lua", "Scripts/Foo.lua"}; // same exact path twice
    auto r = findAssetCaseCollisions({in});
    check("exact duplicate does not self-collide", r.mods.isEmpty());
}

// Two data roots for the same mod: each root scans independently;
// cross-root comparison is out of scope here.
static void ac_testTwoInputsIndependent()
{
    std::cout << "testTwoInputsIndependent\n";
    AssetCaseInput a;
    a.modLabel = "ModA";
    a.dataRoot = "/mods/A/root1";
    a.relPaths = {"Scripts/Player.lua"};

    AssetCaseInput b;
    b.modLabel = "ModA";
    b.dataRoot = "/mods/A/root2";
    b.relPaths = {"Scripts/player.lua"};  // different root, not a within-root collision

    auto r = findAssetCaseCollisions({a, b});
    check("cross-root is not flagged", r.mods.isEmpty());
    check("both files counted", r.totalFilesChecked == 2,
          QString::number(r.totalFilesChecked), "2");
}

// Multiple hits in the same data root.
static void ac_testMultipleHitsInOneRoot()
{
    std::cout << "testMultipleHitsInOneRoot\n";
    AssetCaseInput in;
    in.modLabel = "Multi";
    in.dataRoot = "/mods/Multi";
    in.relPaths = {
        "Scripts/Player.lua", "Scripts/player.lua",       // collision 1
        "Textures/Icon.dds",  "Textures/icon.dds",        // collision 2
        "Meshes/Actor.nif",                                // no collision
    };
    auto r = findAssetCaseCollisions({in});
    check("one mod in report", r.mods.size() == 1);
    check("total files counted", r.totalFilesChecked == 5,
          QString::number(r.totalFilesChecked), "5");
    if (r.mods.isEmpty()) return;
    check("two hits found", r.mods.first().hits.size() == 2,
          QString::number(r.mods.first().hits.size()), "2");
}

static void run_asset_collisions()
{
    ac_testEmpty();
    ac_testNoCollision();
    ac_testLuaCaseCollision();
    ac_testExactDuplicateNoSelfCollision();
    ac_testTwoInputsIndependent();
    ac_testMultipleHitsInOneRoot();
}

// -- log_triage ---------------------------------------------------------------
// The parser had no coverage at all. These pin the shape that mattered in
// practice: a save-game dependency warning must NOT read as a broken install.
namespace triage_section {
using namespace openmw;

static const LogIssue *find(const LogTriageReport &r, LogIssueKind k,
                            const QString &target)
{
    for (const LogIssue &i : r.issues)
        if (i.kind == k && i.target.compare(target, Qt::CaseInsensitive) == 0)
            return &i;
    return nullptr;
}

static void testSaveGameDependencyIsItsOwnKind()
{
    std::cout << "\n[log_triage: 'Saved game dependency' is benign, not an error]\n";
    // Verbatim from a real openmw.log where all three mods were installed and
    // loading fine; the save simply predated a plugin rename.
    const QString log =
        "[23:50:43.120 I] Loading content file OAAB - Tombs and Towers.esm\n"
        "[23:50:48.072 W] Warning: Saved game dependency OAAB - Tombs and Towers.ESP is missing.\n"
        "[23:50:48.072 W] Warning: Saved game dependency Trackless Grazeland OAAB Dwemer Pavements Patch.ESP is missing.\n";
    const LogTriageReport r = triageOpenMWLog(log, {});

    check("both save dependencies classified",
          find(r, LogIssueKind::SaveGameDependency, "OAAB - Tombs and Towers.ESP")
              && find(r, LogIssueKind::SaveGameDependency,
                      "Trackless Grazeland OAAB Dwemer Pavements Patch.ESP"));
    check("a name with spaces is captured whole",
          find(r, LogIssueKind::SaveGameDependency,
               "Trackless Grazeland OAAB Dwemer Pavements Patch.ESP") != nullptr,
          r.issues.isEmpty() ? "" : r.issues.first().target);

    // The regression: these used to fall through to OtherError and read as a
    // broken install, which sent the user reinstalling healthy mods.
    for (const LogIssue &i : r.issues)
        check("not misfiled as a generic error",
              i.kind != LogIssueKind::OtherError, i.target);
    check("not misfiled as a missing plugin",
          find(r, LogIssueKind::MissingPlugin, "OAAB - Tombs and Towers.ESP") == nullptr);
    // The owning mod is usually still installed under the NEW name, so naming
    // it would accuse a mod that works.
    for (const LogIssue &i : r.issues)
        check("no mod is blamed", i.suspectMod.isEmpty(), i.suspectMod);
}

static void testRealErrorsStillClassified()
{
    std::cout << "\n[log_triage: genuine failures keep their own kinds]\n";
    const QString log =
        "[10:00:00.000 E] File \"Patch.esp\" asks for parent file \"Base.esm\", "
        "but it is not available\n"
        "[10:00:01.000 E] Fatal error: Failed loading Ghost.esp: "
        "the content file does not exist\n"
        "[10:00:02.000 W] Warning: Saved game dependency Old.ESP is missing.\n";
    const LogTriageReport r = triageOpenMWLog(log, {});
    check("missing master still detected",
          find(r, LogIssueKind::MissingMaster, "Patch.esp") != nullptr);
    check("missing plugin still detected",
          find(r, LogIssueKind::MissingPlugin, "Ghost.esp") != nullptr);
    check("save dependency detected alongside them",
          find(r, LogIssueKind::SaveGameDependency, "Old.ESP") != nullptr);
    check("three distinct issues", r.issues.size() == 3,
          QString::number(r.issues.size()));
}

static void testRepeatsCollapse()
{
    std::cout << "\n[log_triage: the same save dependency repeated collapses to one]\n";
    QString log;
    for (int i = 0; i < 20; ++i)
        log += "[10:00:00.000 W] Warning: Saved game dependency Old.ESP is missing.\n";
    const LogTriageReport r = triageOpenMWLog(log, {});
    check("deduped to a single issue", r.issues.size() == 1,
          QString::number(r.issues.size()));
}
} // namespace triage_section

static void run_log_triage()
{
    std::cout << "\n=== log_triage ===\n";
    triage_section::testSaveGameDependencyIsItsOwnKind();
    triage_section::testRealErrorsStillClassified();
    triage_section::testRepeatsCollapse();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    run_plugin_parser();
    run_bsa_reader();
    run_master_satisfaction();
    run_plugin_collisions();
    run_asset_collisions();
    run_log_triage();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
