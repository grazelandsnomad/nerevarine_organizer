#include "bethesda_deploy.h"
#include "bethesda_loadorder.h"
#include "bethesda_archives.h"
#include "bethesda_custom_ini.h"
#include "deployment_report.h"
#include "proton_paths.h"
#include "game_adapter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <iostream>

using bethesda_archives::configureArchives;

#include "test_harness.h"

namespace deploy_section {
using namespace bethesda_deploy;

static void touch(const QString &p, const QByteArray &b)
{
    QDir().mkpath(QFileInfo(p).absolutePath());
    QFile f(p);
    if (f.open(QIODevice::WriteOnly)) { f.write(b); f.close(); }
}
static QByteArray readAll(const QString &p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
static bool exists(const QString &p)
{
    return QFileInfo::exists(p) || QFileInfo(p).isSymLink();
}

static void testLastWriterWins()
{
    std::cout << "\n[deploy: later mod overrides earlier]\n";
    QTemporaryDir tmp;
    const QString data = tmp.filePath("Data");
    const QString bak  = tmp.filePath("backup");
    QDir().mkpath(data);

    touch(tmp.filePath("A/meshes/x.nif"), "A");
    touch(tmp.filePath("A/meshes/keep.nif"), "A");
    touch(tmp.filePath("B/meshes/x.nif"), "B");

    const DeployResult r = deploy(data, bak,
        {{"A", tmp.filePath("A")}, {"B", tmp.filePath("B")}});

    check("no errors", r.errors.isEmpty(), r.errors.join(';'));
    check("two distinct files deployed", r.filesDeployed == 2,
          QString::number(r.filesDeployed));
    check("no vanilla displaced", r.vanillaBackedUp == 0);
    check("later mod won x.nif", readAll(data + "/meshes/x.nif") == "B",
          readAll(data + "/meshes/x.nif"));
    check("earlier-only file kept", readAll(data + "/meshes/keep.nif") == "A");

    QString winner;
    for (const auto &f : r.manifest.files)
        if (f.rel.endsWith("x.nif")) winner = f.sourceMod;
    check("manifest credits B as x.nif's source", winner == "B", winner);
}

static void testVanillaBackupRestore()
{
    std::cout << "\n[deploy: vanilla backed up; undeploy restores it exactly]\n";
    QTemporaryDir tmp;
    const QString data = tmp.filePath("Data");
    const QString bak  = tmp.filePath("backup");

    touch(data + "/meshes/x.nif", "VANILLA");     // pre-existing game file
    touch(data + "/Oblivion.esm", "BASE");        // bystander
    touch(tmp.filePath("A/meshes/x.nif"), "MOD"); // mod overrides it

    const DeployResult r = deploy(data, bak, {{"A", tmp.filePath("A")}});

    check("one vanilla displaced", r.vanillaBackedUp == 1,
          QString::number(r.vanillaBackedUp));
    check("mod file now in Data/", readAll(data + "/meshes/x.nif") == "MOD");
    check("vanilla preserved in backup", readAll(bak + "/meshes/x.nif") == "VANILLA");
    check("bystander untouched", readAll(data + "/Oblivion.esm") == "BASE");
    bool flagged = false;
    for (const auto &f : r.manifest.files)
        if (f.rel.endsWith("x.nif")) flagged = f.displacedVanilla;
    check("manifest flags x.nif as displacing vanilla", flagged);

    const UndeployResult u = undeploy(data, bak, r.manifest);
    check("undeploy removed our file", u.removed == 1, QString::number(u.removed));
    check("undeploy restored vanilla", u.restored == 1, QString::number(u.restored));
    check("Data/ x.nif back to vanilla", readAll(data + "/meshes/x.nif") == "VANILLA");
    check("bystander still untouched after undeploy",
          readAll(data + "/Oblivion.esm") == "BASE");
}

static void testUndeployNoVanilla()
{
    std::cout << "\n[undeploy: a purely-added file is removed cleanly]\n";
    QTemporaryDir tmp;
    const QString data = tmp.filePath("Data");
    const QString bak  = tmp.filePath("backup");
    QDir().mkpath(data);
    touch(tmp.filePath("A/plugin.esp"), "P");

    const DeployResult r = deploy(data, bak, {{"A", tmp.filePath("A")}});
    check("deployed into empty Data/", exists(data + "/plugin.esp"));

    const UndeployResult u = undeploy(data, bak, r.manifest);
    check("removed the added file", u.removed == 1);
    check("nothing restored (no vanilla)", u.restored == 0);
    check("Data/ no longer has the file", !exists(data + "/plugin.esp"));
}

static void testNestedAndCopyMethod()
{
    std::cout << "\n[deploy: deep paths, explicit Copy method]\n";
    QTemporaryDir tmp;
    const QString data = tmp.filePath("Data");
    const QString bak  = tmp.filePath("backup");
    QDir().mkpath(data);
    touch(tmp.filePath("A/textures/armor/iron/boots.dds"), "DDS");

    const DeployResult r = deploy(data, bak, {{"A", tmp.filePath("A")}},
                                  LinkMethod::Copy);
    check("deep nested file deployed",
          readAll(data + "/textures/armor/iron/boots.dds") == "DDS");
    check("method recorded as Copy",
          !r.manifest.files.isEmpty()
              && r.manifest.files.first().method == LinkMethod::Copy);
}

static void testManifestRoundTrip()
{
    std::cout << "\n[manifest: JSON load/save preserves every field]\n";
    Manifest m;
    m.files.append({"meshes/x.nif", "Better Meshes", LinkMethod::Hardlink, true});
    m.files.append({"plugin.esp",   "Cool Mod",      LinkMethod::Copy,     false});

    const Manifest back = manifestFromJson(manifestToJson(m));
    check("file count round-trips", back.files.size() == 2,
          QString::number(back.files.size()));
    if (back.files.size() == 2) {
        check("rel + mod + method + vanilla flag round-trip",
              back.files[0].rel == "meshes/x.nif"
              && back.files[0].sourceMod == "Better Meshes"
              && back.files[0].method == LinkMethod::Hardlink
              && back.files[0].displacedVanilla == true
              && back.files[1].method == LinkMethod::Copy
              && back.files[1].displacedVanilla == false);
    }
    check("garbage JSON parses to empty manifest",
          manifestFromJson("not json").files.isEmpty());
}
} // namespace deploy_section

static void run_bethesda_deploy()
{
    std::cout << "=== bethesda_deploy tests ===\n";
    deploy_section::testLastWriterWins();
    deploy_section::testVanillaBackupRestore();
    deploy_section::testUndeployNoVanilla();
    deploy_section::testNestedAndCopyMethod();
    deploy_section::testManifestRoundTrip();
}

namespace loadorder_section {
using namespace bethesda_loadorder;

static void touch(const QString &p, const QByteArray &b = "x")
{
    QDir().mkpath(QFileInfo(p).absolutePath());
    QFile f(p);
    if (f.open(QIODevice::WriteOnly)) { f.write(b); f.close(); }
}

static void testMastersFirst()
{
    std::cout << "\n[mastersFirst: .esm/.esl before .esp, order stable]\n";
    const QStringList in{"B.esp", "Core.esm", "A.esp", "Light.esl", "Base.esm"};
    const QStringList out = mastersFirst(in);
    check("masters/esl float to the front in original relative order",
          out == QStringList({"Core.esm", "Light.esl", "Base.esm", "B.esp", "A.esp"}),
          out.join(','));
    check("case-insensitive master detection",
          mastersFirst({"a.ESP", "b.EsM"}) == QStringList({"b.EsM", "a.ESP"}));
}

static void testPluginsTxt()
{
    std::cout << "\n[pluginsTxtContent: CRLF, no '*' prefix, order preserved]\n";
    const QString body = pluginsTxtContent({"Oblivion.esm", "Mod.esp"});
    check("exact CRLF body", body == QStringLiteral("Oblivion.esm\r\nMod.esp\r\n"),
          QString(body).replace("\r", "\\r").replace("\n", "\\n"));
    check("empty list -> empty body", pluginsTxtContent({}).isEmpty());
}

static void testAsteriskPluginsTxt()
{
    std::cout << "\n[asteriskPluginsTxtContent: '*'-prefixed, CRLF, order]\n";
    const QString body = asteriskPluginsTxtContent({"Core.esm", "Mod.esp"});
    check("each line '*'-prefixed, CRLF",
          body == QStringLiteral("*Core.esm\r\n*Mod.esp\r\n"),
          QString(body).replace("\r", "\\r").replace("\n", "\\n"));
    check("empty list -> empty body", asteriskPluginsTxtContent({}).isEmpty());
}

static void testTimestampOrder()
{
    std::cout << "\n[applyTimestampOrder: ascending mtimes, missing -> error]\n";
    QTemporaryDir tmp;
    touch(tmp.filePath("Core.esm"));
    touch(tmp.filePath("First.esp"));
    touch(tmp.filePath("Second.esp"));

    const qint64 base = 1000000000000LL;   // 2001, well in the past
    const QStringList order{"Core.esm", "First.esp", "Second.esp"};
    const StampResult r = applyTimestampOrder(tmp.path(), order, base, 2000);

    check("all three stamped", r.stamped == 3, QString::number(r.stamped));
    check("no errors", r.errors.isEmpty(), r.errors.join(','));

    auto mtime = [&](const QString &n) {
        return QFileInfo(tmp.filePath(n)).lastModified().toMSecsSinceEpoch();
    };
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    check("first plugin stamped into the past (not left at creation time)",
          mtime("Core.esm") < now - 1000000000LL);
    check("load order is strictly ascending in mtime",
          mtime("Core.esm") < mtime("First.esp")
              && mtime("First.esp") < mtime("Second.esp"),
          QString("%1 < %2 < %3").arg(mtime("Core.esm"))
              .arg(mtime("First.esp")).arg(mtime("Second.esp")));

    const StampResult r2 = applyTimestampOrder(tmp.path(), {"Ghost.esp"}, base, 2000);
    check("missing file reported as an error", r2.errors == QStringList{"Ghost.esp"});
    check("missing file not counted as stamped", r2.stamped == 0);
}
} // namespace loadorder_section

static void run_bethesda_loadorder()
{
    std::cout << "=== bethesda_loadorder tests ===\n";
    loadorder_section::testMastersFirst();
    loadorder_section::testPluginsTxt();
    loadorder_section::testAsteriskPluginsTxt();
    loadorder_section::testTimestampOrder();
}

namespace archives_section {

// value after '=' on the SArchiveList line
static QString archiveListValue(const QString &ini)
{
    for (const QString &raw : ini.split('\n')) {
        QString l = raw; if (l.endsWith('\r')) l.chop(1);
        if (l.trimmed().startsWith("SArchiveList=", Qt::CaseInsensitive))
            return l.mid(l.indexOf('=') + 1);
    }
    return {};
}

static int count(const QString &hay, const QString &needle, Qt::CaseSensitivity cs)
{
    int c = 0, from = 0;
    while ((from = hay.indexOf(needle, from, cs)) != -1) { ++c; from += needle.size(); }
    return c;
}

static void testAppendToExisting()
{
    std::cout << "\n[existing [Archive]: append BSA, keep vanilla, fix invalidation]\n";
    const QString ini =
        "[General]\r\nSLanguage=ENGLISH\r\n"
        "[Archive]\r\n"
        "SArchiveList=Oblivion - Meshes.bsa, Oblivion - Textures - Compressed.bsa\r\n"
        "bInvalidateOlderFiles=0\r\n";
    const QString out = configureArchives(ini, {"CoolMod.bsa"});

    check("invalidation turned on", out.contains("bInvalidateOlderFiles=1"));
    check("old bInvalidateOlderFiles=0 replaced", !out.contains("bInvalidateOlderFiles=0"));
    check("SInvalidationFile emptied (added)", out.contains("SInvalidationFile="));
    const QString list = archiveListValue(out);
    check("vanilla BSA kept", list.contains("Oblivion - Meshes.bsa"), list);
    check("mod BSA appended", list.contains("CoolMod.bsa"), list);
    check("unrelated section preserved",
          out.contains("[General]") && out.contains("SLanguage=ENGLISH"));
    check("output is CRLF", out.contains("\r\n"));
}

static const QStringList kOblivionSeed = {
    "Oblivion - Meshes.bsa", "Oblivion - Textures - Compressed.bsa",
    "Oblivion - Sounds.bsa", "Oblivion - Voices1.bsa",
    "Oblivion - Voices2.bsa", "Oblivion - Misc.bsa",
};

static void testNoArchiveSection()
{
    std::cout << "\n[no [Archive] section: append one seeded with vanilla]\n";
    const QString ini = "[General]\r\nSLanguage=ENGLISH\r\n";
    const QString out = configureArchives(ini, {"CoolMod.bsa"}, kOblivionSeed);
    check("[Archive] section appended", out.contains("[Archive]"));
    check("invalidation keys present",
          out.contains("bInvalidateOlderFiles=1") && out.contains("SInvalidationFile="));
    const QString list = archiveListValue(out);
    check("seeded with vanilla", list.contains("Oblivion - Meshes.bsa"), list);
    check("mod BSA present", list.contains("CoolMod.bsa"), list);
    check("[General] preserved", out.contains("SLanguage=ENGLISH"));
}

// An unknown vanilla list must never be guessed at. Writing SArchiveList with
// only the mod's BSAs would leave the base game's own archives unloaded, which
// is far worse than leaving the key alone and letting the engine default.
static void testNoSeedNeverInventsArchiveList()
{
    std::cout << "\n[unknown vanilla list: SArchiveList is left absent, not invented]\n";
    const QString out = configureArchives("[General]\r\nX=1\r\n", {"CoolMod.bsa"}, {});
    check("invalidation still applied", out.contains("bInvalidateOlderFiles=1"));
    check("SArchiveList NOT invented",
          !out.contains("SArchiveList", Qt::CaseInsensitive), out);

    // With the key already present it is safe to append, seed or no seed.
    const QString existing =
        configureArchives("[Archive]\r\nSArchiveList=Base.bsa\r\n", {"CoolMod.bsa"}, {});
    const QString list = archiveListValue(existing);
    check("existing list still gets the mod BSA appended",
          list.contains("Base.bsa") && list.contains("CoolMod.bsa"), list);
}

static void testDedup()
{
    std::cout << "\n[case-insensitive de-dup: don't double-list a BSA]\n";
    const QString ini = "[Archive]\r\nSArchiveList=CoolMod.bsa\r\n";
    const QString out = configureArchives(ini, {"coolmod.bsa", "Other.bsa"});
    const QString list = archiveListValue(out);
    check("existing entry kept as-authored", list.contains("CoolMod.bsa"));
    check("case-variant duplicate not added", !list.contains("coolmod.bsa"));
    check("only one CoolMod entry", count(list, "coolmod.bsa", Qt::CaseInsensitive) == 1,
          list);
    check("the genuinely new BSA is added", list.contains("Other.bsa"), list);
}

static void testAddsMissingInvalidationKeys()
{
    std::cout << "\n[section present but missing invalidation keys -> added]\n";
    const QString ini = "[Archive]\r\nSArchiveList=Oblivion - Meshes.bsa\r\n";
    const QString out = configureArchives(ini, {});
    check("bInvalidateOlderFiles added", out.contains("bInvalidateOlderFiles=1"));
    check("SInvalidationFile added", out.contains("SInvalidationFile="));
    check("no spurious second SArchiveList",
          count(out, "SArchiveList=", Qt::CaseInsensitive) == 1);
}

static void testIdempotent()
{
    std::cout << "\n[re-running is a no-op: the ini must not grow per deploy]\n";
    const QString once  = configureArchives("[Archive]\r\nSArchiveList=A.bsa\r\n", {"Mod.bsa"});
    const QString twice = configureArchives(once, {"Mod.bsa"});
    check("second run changes nothing", once == twice, twice, once);
    check("no blank line accumulated",
          count(twice, "\r\n\r\n", Qt::CaseSensitive) == 0, twice);
}
} // namespace archives_section

static void run_bethesda_archives()
{
    std::cout << "=== bethesda_archives tests ===\n";
    archives_section::testAppendToExisting();
    archives_section::testNoArchiveSection();
    archives_section::testNoSeedNeverInventsArchiveList();
    archives_section::testDedup();
    archives_section::testAddsMissingInvalidationKeys();
    archives_section::testIdempotent();
}

// -- bethesda_custom_ini -------------------------------------------------------
namespace custom_ini_section {

static QString keyValue(const QString &ini, const QString &key)
{
    for (const QString &raw : ini.split('\n')) {
        QString l = raw; if (l.endsWith('\r')) l.chop(1);
        if (l.trimmed().startsWith(key + "=", Qt::CaseInsensitive))
            return l.mid(l.indexOf('=') + 1);
    }
    return {};
}

static void testCreatesFromNothing()
{
    std::cout << "\n[Custom.ini: created from empty input]\n";
    // The normal case: neither FO4 nor Starfield ships this file, so the deploy
    // path hands us empty text and expects a complete, valid ini back.
    const QString out = bethesda_custom_ini::configureCustomIni(QString());
    check("[Archive] section present", out.contains("[Archive]"));
    check("loose files enabled (bInvalidateOlderFiles)",
          out.contains("bInvalidateOlderFiles=1"));
    check("loose files enabled (sResourceDataDirsFinal empty)",
          out.contains("sResourceDataDirsFinal=\r\n"), out);
    check("does not open with a blank line", !out.startsWith("\r\n"), out);
    check("output is CRLF", out.contains("\r\n"));
}

// The archive-list keys REPLACE the base ini's value instead of extending it,
// so writing one that holds only the mod's archives unloads every vanilla one.
// We must never author or rewrite them.
static void testNeverTouchesArchiveLists()
{
    std::cout << "\n[Custom.ini: archive-list keys are never authored or rewritten]\n";
    const QString fresh = bethesda_custom_ini::configureCustomIni(QString());
    for (const char *key : {"sResourceIndexFileList", "sResourceArchive2List",
                            "sResourceArchiveList2", "sResourceArchiveList"}) {
        check(QString("%1 not invented").arg(key).toUtf8().constData(),
              !fresh.contains(key, Qt::CaseInsensitive), fresh);
    }

    // A user's existing list must survive byte-for-byte.
    const QString userList =
        "[Archive]\r\n"
        "sResourceIndexFileList=Starfield - Textures01.ba2, MyMod - Textures.ba2\r\n";
    const QString out = bethesda_custom_ini::configureCustomIni(userList);
    check("existing vanilla+mod archive list preserved verbatim",
          keyValue(out, "sResourceIndexFileList")
              == "Starfield - Textures01.ba2, MyMod - Textures.ba2",
          keyValue(out, "sResourceIndexFileList"));
    check("invalidation still added alongside it",
          out.contains("bInvalidateOlderFiles=1") && out.contains("sResourceDataDirsFinal="));
}

static void testExtendsExistingSection()
{
    std::cout << "\n[Custom.ini: existing [Archive] extended, not replaced]\n";
    const QString ini =
        "[General]\r\nsTestFile1=MyMod.esm\r\n"
        "[Archive]\r\nbInvalidateOlderFiles=0\r\n";
    const QString out = bethesda_custom_ini::configureCustomIni(ini);
    check("invalidation flipped on", out.contains("bInvalidateOlderFiles=1"));
    check("old value gone", !out.contains("bInvalidateOlderFiles=0"));
    check("missing data-dirs key added", out.contains("sResourceDataDirsFinal="));
    check("unrelated section preserved",
          out.contains("[General]") && out.contains("sTestFile1=MyMod.esm"));
}

static void testIdempotent()
{
    std::cout << "\n[Custom.ini: re-running is a no-op]\n";
    const QString once  = bethesda_custom_ini::configureCustomIni(QString());
    const QString twice = bethesda_custom_ini::configureCustomIni(once);
    check("second run changes nothing", once == twice, twice, once);
    check("only one invalidation key",
          archives_section::count(twice, "bInvalidateOlderFiles", Qt::CaseInsensitive) == 1,
          twice);
    check("no blank line accumulated",
          archives_section::count(twice, "\r\n\r\n", Qt::CaseSensitive) == 0, twice);
}

static void testStrayArchiveDetection()
{
    std::cout << "\n[strayArchives: exactly the archives the engine will not auto-load]\n";
    // "<Plugin> - Main.ba2" / "- Textures.ba2" / "<Plugin>.ba2" auto-load.
    const QStringList ba2s = { "MyMod - Main.ba2", "MyMod - Textures.ba2",
                               "MyMod.ba2", "Orphan.ba2" };
    const QStringList plugins = { "MyMod.esm" };
    check("plugin-matched archives are not reported",
          bethesda_custom_ini::strayArchives(ba2s, plugins) == QStringList{"Orphan.ba2"},
          bethesda_custom_ini::strayArchives(ba2s, plugins).join(", "));

    // Regression: a bare startsWith() test called this covered, so an archive
    // that never loads was silently not reported.
    const QStringList sneaky = { "MyModPatch - Main.ba2" };
    check("a name merely PREFIXED by a plugin stem is still stray",
          bethesda_custom_ini::strayArchives(sneaky, plugins) == sneaky,
          bethesda_custom_ini::strayArchives(sneaky, plugins).join(", "));

    check("no plugins means every archive is stray",
          bethesda_custom_ini::strayArchives(ba2s, {}).size() == 4);
    check("no archives means nothing stray",
          bethesda_custom_ini::strayArchives({}, plugins).isEmpty());
}
} // namespace custom_ini_section

static void run_bethesda_custom_ini()
{
    std::cout << "=== bethesda_custom_ini tests ===\n";
    custom_ini_section::testCreatesFromNothing();
    custom_ini_section::testNeverTouchesArchiveLists();
    custom_ini_section::testExtendsExistingSection();
    custom_ini_section::testIdempotent();
    custom_ini_section::testStrayArchiveDetection();
}

static void run_proton_paths()
{
    std::cout << "=== proton path tests ===\n";

    const QString root = "/home/u/.local/share/Steam/steamapps/compatdata";

    {
        const QString p = proton::prefixUserDir(root, "22330");
        check("prefixUserDir builds the steamuser profile path",
              p == root + "/22330/pfx/drive_c/users/steamuser", p);
        check("prefixUserDir empty when appId empty",
              proton::prefixUserDir(root, "").isEmpty());
        check("prefixUserDir empty when root empty",
              proton::prefixUserDir("", "22330").isEmpty());
    }

    // localAppData: where Plugins.txt lives
    {
        const QString pu = proton::prefixUserDir(root, "22330");
        check("localAppData with folder -> AppData/Local/<folder>",
              proton::localAppData(pu, "Oblivion") == pu + "/AppData/Local/Oblivion",
              proton::localAppData(pu, "Oblivion"));
        check("localAppData without folder -> AppData/Local",
              proton::localAppData(pu) == pu + "/AppData/Local");
        check("localAppData empty when prefix empty",
              proton::localAppData("", "Oblivion").isEmpty());
    }

    // myGamesDirs: where the engine .ini lives, both variants in order
    {
        const QString pu = "/p/users/steamuser";
        const QStringList d = proton::myGamesDirs(pu, "Oblivion");
        check("myGamesDirs returns two candidates", d.size() == 2,
              QString::number(d.size()));
        check("myGamesDirs[0] = Documents/My Games (newer Proton)",
              d.value(0) == pu + "/Documents/My Games/Oblivion", d.value(0));
        check("myGamesDirs[1] = My Documents/My Games (older)",
              d.value(1) == pu + "/My Documents/My Games/Oblivion", d.value(1));
        check("myGamesDirs without folder stops at My Games",
              proton::myGamesDirs(pu).value(0) == pu + "/Documents/My Games");
    }

    {
        const QStringList common = {
            "/home/u/.local/share/Steam/steamapps/common",
            "/mnt/games/SteamLibrary/steamapps/common",
            "/weird/path/not/matching",                     // skipped
            "/home/u/.local/share/Steam/steamapps/common",  // dup -> collapsed
        };
        const QStringList cd = proton::compatdataRootsFromCommon(common);
        check("compatdataRootsFromCommon maps common->compatdata + dedups",
              cd == QStringList({
                  "/home/u/.local/share/Steam/steamapps/compatdata",
                  "/mnt/games/SteamLibrary/steamapps/compatdata",
              }),
              cd.join(" | "));
        check("compatdataRootsFromCommon empty input -> empty",
              proton::compatdataRootsFromCommon({}).isEmpty());
    }
}

namespace adapters_section {

// empty id breaks profile lookup; empty displayName = blank menu entry
static void testAllAdaptersHaveBasicIdentity()
{
    std::cout << "\n-- adapters: every entry has id + displayName --\n";
    for (const GameAdapter *a : GameAdapterRegistry::all()) {
        check("non-empty id",
              !a->id().isEmpty(),
              QStringLiteral("displayName=%1").arg(a->displayName()));
        check("non-empty displayName",
              !a->displayName().isEmpty(),
              QStringLiteral("id=%1").arg(a->id()));
    }
}

// duplicate ids shadow each other - second is unreachable via find()
static void testIdsAreUnique()
{
    std::cout << "\n-- adapters: ids are unique --\n";
    QSet<QString> seen;
    bool clean = true;
    for (const GameAdapter *a : GameAdapterRegistry::all()) {
        if (seen.contains(a->id())) {
            clean = false;
            std::cout << "    duplicate id: " << a->id().toStdString() << "\n";
        }
        seen.insert(a->id());
    }
    check("no duplicate ids", clean);
}

// find()==nullptr = unknown game (fall back to file picker); known ids must resolve
static void testFindLookup()
{
    std::cout << "\n-- adapters: find() round-trips known and unknown ids --\n";
    const GameAdapter *m = GameAdapterRegistry::find("morrowind");
    check("find(morrowind) is non-null", m != nullptr);
    if (m) check("find(morrowind) returns the OpenMW adapter",
                 m->isMorrowind());

    check("find(\"\") is null",     GameAdapterRegistry::find("") == nullptr);
    check("find(unknown) is null",  GameAdapterRegistry::find("nope") == nullptr);
}

// hasLauncher() = "any layout declares a launcher". Pin known cases so a
// refactor doesn't silently flip toolbar visibility.
static void testHasLauncherDerivedFromLayouts()
{
    std::cout << "\n-- adapters: hasLauncher() reflects layout data --\n";
    const auto *fnv = GameAdapterRegistry::find("falloutnewvegas");
    check("Fallout NV declares a launcher",  fnv && fnv->hasLauncher());

    const auto *flondon = GameAdapterRegistry::find("falloutlondon");
    // total conversion forces hasLauncher() false even though its borrowed
    // Steam folder lists Fallout4Launcher.exe
    check("Fallout London hides the launcher",
          flondon && !flondon->hasLauncher());

    const auto *cyber = GameAdapterRegistry::find("cyberpunk2077");
    // Steam + GOG layouts, neither declares a launcher
    check("Cyberpunk 2077 has no launcher", cyber && !cyber->hasLauncher());
}

// pinned subset feeds the toolbar's "switch game" section; OpenMW (Morrowind)
// must be in it
static void testPinnedContainsOpenMW()
{
    std::cout << "\n-- adapters: pinned() includes OpenMW (Morrowind) --\n";
    bool foundOpenMW = false;
    for (const GameAdapter *a : GameAdapterRegistry::pinned()) {
        if (a->isMorrowind()) { foundOpenMW = true; break; }
    }
    check("Morrowind is pinned", foundOpenMW);
}

// builtin subset (first-run wizard chooser) must be a subset of all() and
// include OpenMW
static void testBuiltinIsSubsetOfAll()
{
    std::cout << "\n-- adapters: builtin() ⊆ all() --\n";
    QSet<QString> allIds;
    for (const GameAdapter *a : GameAdapterRegistry::all()) allIds.insert(a->id());

    bool subset = true;
    bool foundOpenMW = false;
    for (const GameAdapter *a : GameAdapterRegistry::builtin()) {
        if (!allIds.contains(a->id())) {
            subset = false;
            std::cout << "    builtin id not in all(): "
                      << a->id().toStdString() << "\n";
        }
        if (a->isMorrowind()) foundOpenMW = true;
    }
    check("every builtin id appears in all()", subset);
    check("builtin() includes Morrowind",      foundOpenMW);
}

// misclassification routes a game through the wrong load-order writer. One
// case per LoadOrderStyle, plus the data-dir/config-name fields Oblivion needs.
// Unclassified games must stay Unknown or they get treated as managed.
static void testLoadOrderClassification()
{
    std::cout << "\n-- adapters: load-order style classification --\n";
    const auto *mw = GameAdapterRegistry::find("morrowind");
    check("Morrowind -> OpenMW style",
          mw && mw->loadOrderStyle() == LoadOrderStyle::OpenMW);

    const auto *ob = GameAdapterRegistry::find("oblivion");
    check("Oblivion -> TimestampPluginsTxt style",
          ob && ob->loadOrderStyle() == LoadOrderStyle::TimestampPluginsTxt);
    check("Oblivion data subdir is Data",
          ob && ob->dataSubdir() == QStringLiteral("Data"));
    check("Oblivion config folder names resolve to Oblivion",
          ob && ob->localAppDataName() == QStringLiteral("Oblivion")
             && ob->myGamesName() == QStringLiteral("Oblivion"));

    const auto *se = GameAdapterRegistry::find("skyrimspecialedition");
    check("Skyrim SE -> AsteriskPluginsTxt style",
          se && se->loadOrderStyle() == LoadOrderStyle::AsteriskPluginsTxt);

    const auto *fo4 = GameAdapterRegistry::find("fallout4");
    check("Fallout 4 -> AsteriskPluginsTxt style",
          fo4 && fo4->loadOrderStyle() == LoadOrderStyle::AsteriskPluginsTxt);
    const auto *fnv = GameAdapterRegistry::find("falloutnewvegas");
    check("Fallout NV -> TimestampPluginsTxt style",
          fnv && fnv->loadOrderStyle() == LoadOrderStyle::TimestampPluginsTxt);
    const auto *fo3 = GameAdapterRegistry::find("fallout3");
    check("Fallout 3 -> TimestampPluginsTxt + FalloutNV/Fallout3 config names",
          fo3 && fo3->loadOrderStyle() == LoadOrderStyle::TimestampPluginsTxt
              && fo3->localAppDataName() == QStringLiteral("Fallout3")
              && fnv && fnv->localAppDataName() == QStringLiteral("FalloutNV"));

    const auto *cyber = GameAdapterRegistry::find("cyberpunk2077");
    check("unclassified game stays Unknown",
          cyber && cyber->loadOrderStyle() == LoadOrderStyle::Unknown);

    // Oblivion lists OBSE loaders, xOBSE first; unclassified games have none
    check("Oblivion lists OBSE loaders, xOBSE first",
          ob && ob->scriptExtenderLoaders().value(0) == QStringLiteral("xobse_loader.exe")
             && ob->scriptExtenderLoaders().contains(QStringLiteral("obse_loader.exe")));
    check("unclassified game has no script extender",
          cyber && cyber->scriptExtenderLoaders().isEmpty());
}

// Archive config used to be `if (id != "oblivion") return;` inside the deploy
// path, so every other engine deployed its mods and then loaded none of the
// loose ones. It is adapter data now, and these pin it per engine family so a
// newly classified game cannot quietly inherit "no archive config".
static void testArchiveConfigPerEngineFamily()
{
    std::cout << "\n[archiveConfig: each classified engine says how to load loose files]\n";
    using Style = GameAdapter::ArchiveConfig::Style;
    auto cfg = [](const char *id) {
        const GameAdapter *a = GameAdapterRegistry::find(id);
        return a ? a->archiveConfig() : GameAdapter::ArchiveConfig{};
    };

    // Modern family: a *Custom.ini the game does not ship, so it gets created.
    for (const char *id : {"fallout4", "starfield"}) {
        const auto c = cfg(id);
        check(QString("%1 uses the modern custom-ini style").arg(id).toUtf8().constData(),
              c.style == Style::ModernCustomIni);
        check(QString("%1 targets a Custom ini and creates it").arg(id).toUtf8().constData(),
              c.iniName.endsWith("Custom.ini") && c.createIfMissing, c.iniName);
        check(QString("%1 looks for .ba2").arg(id).toUtf8().constData(),
              c.archiveSuffix == ".ba2", c.archiveSuffix);
    }
    check("fallout4 writes Fallout4Custom.ini",
          cfg("fallout4").iniName == "Fallout4Custom.ini", cfg("fallout4").iniName);

    // Gamebryo family: edits an ini the game ships, so never created.
    for (const char *id : {"oblivion", "falloutnewvegas", "fallout3"}) {
        const auto c = cfg(id);
        check(QString("%1 uses the Gamebryo SArchiveList style").arg(id).toUtf8().constData(),
              c.style == Style::GamebryoArchiveList);
        check(QString("%1 never creates the ini").arg(id).toUtf8().constData(),
              !c.createIfMissing);
        check(QString("%1 looks for .bsa").arg(id).toUtf8().constData(),
              c.archiveSuffix == ".bsa", c.archiveSuffix);
    }
    check("New Vegas edits Fallout.ini",
          cfg("falloutnewvegas").iniName == "Fallout.ini", cfg("falloutnewvegas").iniName);
    check("New Vegas carries its own vanilla seed, not Oblivion's",
          cfg("falloutnewvegas").vanillaSeed.contains("Fallout - Textures.bsa")
              && !cfg("falloutnewvegas").vanillaSeed.contains("Oblivion - Meshes.bsa"),
          cfg("falloutnewvegas").vanillaSeed.join(", "));
    check("Fallout 3 ships no seed rather than a guessed one",
          cfg("fallout3").vanillaSeed.isEmpty());

    // Skyrim SE genuinely needs nothing: loose files and name-matched BSAs
    // both load on their own.
    check("Skyrim SE correctly has no archive config",
          cfg("skyrimspecialedition").style == Style::None);
}

static void testFallout4IsDiscoverable()
{
    std::cout << "\n[Fallout 4: fully classified AND reachable from the menu]\n";
    const GameAdapter *fo4 = GameAdapterRegistry::find("fallout4");
    check("fallout4 adapter exists", fo4 != nullptr);
    if (!fo4) return;
    check("deployable (Data/ declared)", fo4->dataSubdir() == "Data");
    check("'*'-prefixed Plugins.txt style",
          fo4->loadOrderStyle() == LoadOrderStyle::AsteriskPluginsTxt);
    check("F4SE loader listed",
          fo4->scriptExtenderLoaders().contains("f4se_loader.exe"));
    // It was fully classified but unpinned, so working support sat behind
    // Settings > Show all games.
    check("pinned, so it appears without 'Show all games'", fo4->pinned());
    check("still out of the first-run chooser (unverified on a real install)",
          !fo4->builtin());
}

// Starfield was pinned and detectable but left at every "not a managed title"
// default, so Deploy stayed hidden and no Plugins.txt was ever written. These
// pin the classification that turns the generic Bethesda machinery on.
static void testStarfieldIsFullyClassified()
{
    std::cout << "\n[Starfield: classified for deploy + Plugins.txt]\n";
    const GameAdapter *sf = GameAdapterRegistry::find("starfield");
    check("starfield adapter exists", sf != nullptr);
    if (!sf) return;
    check("uses the '*'-prefixed Plugins.txt style (as Skyrim SE / FO4)",
          sf->loadOrderStyle() == LoadOrderStyle::AsteriskPluginsTxt);
    check("declares Data/ so the deploy actions are visible",
          sf->dataSubdir() == QStringLiteral("Data"), sf->dataSubdir());
    check("AppData/Local folder set (Plugins.txt lives there)",
          sf->localAppDataName() == QStringLiteral("Starfield"), sf->localAppDataName());
    check("My Games folder set (StarfieldCustom.ini lives there)",
          sf->myGamesName() == QStringLiteral("Starfield"), sf->myGamesName());
    check("SFSE loader listed",
          sf->scriptExtenderLoaders().contains(QStringLiteral("sfse_loader.exe")),
          sf->scriptExtenderLoaders().join(", "));
    check("stays out of the first-run chooser until verified on a real install",
          !sf->builtin());
}
} // namespace adapters_section

static void run_game_adapters()
{
    std::cout << "=== GameAdapter registry tests ===\n";

    adapters_section::testAllAdaptersHaveBasicIdentity();
    adapters_section::testIdsAreUnique();
    adapters_section::testFindLookup();
    adapters_section::testHasLauncherDerivedFromLayouts();
    adapters_section::testPinnedContainsOpenMW();
    adapters_section::testBuiltinIsSubsetOfAll();
    adapters_section::testLoadOrderClassification();
    adapters_section::testStarfieldIsFullyClassified();
    adapters_section::testArchiveConfigPerEngineFamily();
    adapters_section::testFallout4IsDiscoverable();
}

// -- deployment_report --------------------------------------------------------

static void run_deployment_report()
{
    std::cout << "=== deployment_report ===\n";

    // A fully-resolved Oblivion deployment: exercise the found/missing markers,
    // the Oblivion-only ini section, the manifest count, and prefix probing.
    deployment_report::Facts f;
    f.gameName = "Oblivion"; f.gameId = "oblivion";
    f.loadOrderStyle = "timestamp + Plugins.txt (Oblivion/FO3/FNV)";
    f.steamAppId = "22330";
    f.dataFolder = { "/games/Oblivion/Data", true };
    f.installDirKnown = true; f.scriptExtender = "obse_loader.exe";
    f.pluginsTxt = { "/prefix/Plugins.txt", false };
    f.showOblivionIni = true;
    f.oblivionIni = { "/prefix/Oblivion.ini", true };
    f.manifestPath = "/state/deploy.json"; f.haveManifest = true; f.deployedFileCount = 42;
    f.backupDir = "/state/backup";
    f.enabledInstalledMods = 7; f.dataRootCount = 3;
    f.prefixCandidates = { "/a/compatdata", "/b/compatdata" };
    f.prefixExists = { "  [found]", "  [MISSING]" };

    const QString r = deployment_report::format(f);
    check("header names the game+id", r.contains("Game:        Oblivion (oblivion)"));
    check("resolved+existing data folder is [found]",
          r.contains("/games/Oblivion/Data  [found]"));
    check("resolved+missing plugins.txt is [MISSING]",
          r.contains("/prefix/Plugins.txt  [MISSING]"));
    check("script-extender loader named", r.contains("obse_loader.exe [found]"));
    check("oblivion ini section present for oblivion", r.contains("Oblivion.ini:"));
    check("manifest file count shown", r.contains("[42 files currently deployed]"));
    check("mods-to-deploy line", r.contains("7 enabled+installed mod(s), 3 data root(s)"));
    check("prefix markers rendered per candidate",
          r.contains("/a/compatdata  [found]") && r.contains("/b/compatdata  [MISSING]"));

    // Unresolved paths render the distinct *** NOT RESOLVED *** form, not
    // "  [MISSING]", and the non-oblivion path hides the ini section.
    deployment_report::Facts g;
    g.gameName = "Skyrim SE"; g.gameId = "skyrimse";
    g.loadOrderStyle = "*-prefixed Plugins.txt (Skyrim SE/FO4)";
    g.dataFolder = { QString(), false };   // unresolved
    g.pluginsTxt = { QString(), false };
    g.showOblivionIni = false;
    g.manifestPath = "/state/deploy.json"; g.haveManifest = false;
    g.backupDir = "/state/backup";
    // no prefix candidates

    const QString r2 = deployment_report::format(g);
    check("unresolved data folder uses NOT RESOLVED, not MISSING",
          r2.contains("*** NOT RESOLVED") && !r2.contains("  [MISSING]"));
    check("no manifest -> nothing deployed yet",
          r2.contains("[nothing deployed yet]"));
    check("non-oblivion hides the ini section", !r2.contains("Oblivion.ini:"));
    check("empty steam appid shows (none)", r2.contains("Steam appid: (none)"));
    check("no prefix candidates -> explanatory line",
          r2.contains("(none - no steam appid or resolved install path)"));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    run_bethesda_deploy();
    run_bethesda_loadorder();
    run_bethesda_archives();
    run_bethesda_custom_ini();
    run_deployment_report();
    run_proton_paths();
    run_game_adapters();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
