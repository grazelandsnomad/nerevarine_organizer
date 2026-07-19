#include "openmwconfigwriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

#include "test_harness.h"

// renderOpenMWConfig

using openmw::ConfigMod;
using openmw::renderOpenMWConfig;

static ConfigMod plugin(const QString &dir, const QStringList &files,
                        bool enabled = true)
{
    ConfigMod m;
    m.enabled   = enabled;
    m.installed = true;
    m.pluginDirs = {{dir, files}};
    return m;
}

static ConfigMod resourceOnly(const QString &root, bool enabled = true)
{
    ConfigMod m;
    m.enabled   = enabled;
    m.installed = true;
    m.resourceRoots << root;
    return m;
}

static void testMinimalRender()
{
    std::cout << "testMinimalRender\n";

    const QString out = renderOpenMWConfig(
        {plugin("/mods/A", {"A.esp"})},
        {"A.esp"},
        /*existingCfg=*/{});

    const QString want =
        "# --- Nerevarine Organizer BEGIN ---\n"
        "data=\"/mods/A\"\n"
        "content=A.esp\n"
        "# --- Nerevarine Organizer END ---\n";

    check("empty cfg → managed section only", out == want, out, want);
}

static void testReorderBeatsExistingCfg()
{
    std::cout << "testReorderBeatsExistingCfg\n";

    const QString existing =
        "# --- Nerevarine Organizer BEGIN ---\n"
        "data=\"/mods/A\"\n"
        "data=\"/mods/B\"\n"
        "content=Morrowind.esm\n"
        "content=A.esp\n"
        "content=B.esp\n"
        "# --- Nerevarine Organizer END ---\n";

    const QList<ConfigMod> mods = {
        plugin("/mods/B", {"B.esp"}),
        plugin("/mods/A", {"A.esp"}),
    };
    const QStringList loadOrder = {"B.esp", "A.esp"};

    const QString out = renderOpenMWConfig(mods, loadOrder, existing);

    const QString want =
        "# --- Nerevarine Organizer BEGIN ---\n"
        "data=\"/mods/B\"\n"
        "data=\"/mods/A\"\n"
        "content=Morrowind.esm\n"
        "content=B.esp\n"
        "content=A.esp\n"
        "# --- Nerevarine Organizer END ---\n";

    check("managed content follows loadOrder, not prior cfg",
          out == want, out, want);
    check("external Morrowind.esm keeps its leading position",
          out.indexOf("content=Morrowind.esm")
              < out.indexOf("content=B.esp"));
}

static void testResourceModEmitsData()
{
    std::cout << "testResourceModEmitsData\n";

    const QList<ConfigMod> mods = {
        plugin      ("/mods/Bloodmoon", {"Bloodmoon.esm"}),
        resourceOnly("/mods/Retexture-HD"),
    };
    const QStringList loadOrder = {"Bloodmoon.esm"};

    const QString out = renderOpenMWConfig(mods, loadOrder, /*existing=*/{});

    const QString want =
        "# --- Nerevarine Organizer BEGIN ---\n"
        "data=\"/mods/Bloodmoon\"\n"
        "data=\"/mods/Retexture-HD\"\n"
        "content=Bloodmoon.esm\n"
        "# --- Nerevarine Organizer END ---\n";

    check("resource-only mod contributes data=", out == want, out, want);
    check("resource-only mod does NOT contribute content=",
          !out.contains("content=Retexture-HD"));
}

static void testDisabledResourceModDropped()
{
    std::cout << "testDisabledResourceModDropped\n";

    const QList<ConfigMod> mods = {
        resourceOnly("/mods/Retexture-HD", /*enabled=*/false),
    };
    const QString out = renderOpenMWConfig(mods, {}, {});

    check("disabled resource mod emits no data=",
          !out.contains("data=\"/mods/Retexture-HD\""));
}

static void testPreambleAndExternalDataPreserved()
{
    std::cout << "testPreambleAndExternalDataPreserved\n";

    const QString existing =
        "# user config\n"
        "fallback=FontColor_color_normal,202,165,96\n"
        "data=\"/usr/share/games/openmw/data\"\n"
        "# --- Nerevarine Organizer BEGIN ---\n"
        "data=\"/mods/Old\"\n"
        "content=Morrowind.esm\n"
        "# --- Nerevarine Organizer END ---\n";

    const QList<ConfigMod> mods = {plugin("/mods/New", {"New.esp"})};
    const QString out = renderOpenMWConfig(mods, {"New.esp"}, existing);

    check("preamble comment kept",        out.contains("# user config"));
    check("fallback= line kept",          out.contains("fallback=FontColor_color_normal,202,165,96"));
    check("external data= line kept",     out.contains("data=\"/usr/share/games/openmw/data\""));
    check("external data= is above BEGIN",
          out.indexOf("data=\"/usr/share/games/openmw/data\"")
              < out.indexOf("# --- Nerevarine Organizer BEGIN ---"));
    check("old managed data= dropped",    !out.contains("data=\"/mods/Old\""));
    check("new managed data= present",    out.contains("data=\"/mods/New\""));
    check("Morrowind.esm retained",       out.contains("content=Morrowind.esm"));
}

static void testDisabledManagedContentDropped()
{
    std::cout << "testDisabledManagedContentDropped\n";

    const QString existing =
        "# --- Nerevarine Organizer BEGIN ---\n"
        "data=\"/mods/A\"\n"
        "content=A.esp\n"
        "# --- Nerevarine Organizer END ---\n";

    const QList<ConfigMod> mods = {
        plugin("/mods/A", {"A.esp"}, /*enabled=*/false),
    };

    const QString out = renderOpenMWConfig(mods, {"A.esp"}, existing);

    check("disabled managed content dropped", !out.contains("content=A.esp"));
    check("disabled mod emits no data=",      !out.contains("data=\"/mods/A\""));
}

static void testEsmBeforeEsp()
{
    std::cout << "testEsmBeforeEsp\n";

    const QList<ConfigMod> mods = {
        plugin("/mods/Pack", {"zPatch.esp", "aMaster.esm"}),
    };
    const QString out = renderOpenMWConfig(mods, {}, {});

    const int esmPos = out.indexOf("content=aMaster.esm");
    const int espPos = out.indexOf("content=zPatch.esp");
    check("master esm listed",     esmPos >= 0);
    check("patch esp listed",      espPos >= 0);
    check(".esm emitted before .esp within mod", esmPos < espPos);
}

static void testCrlfInputTolerated()
{
    std::cout << "testCrlfInputTolerated\n";

    const QString existing =
        "# crlf header\r\n"
        "# --- Nerevarine Organizer BEGIN ---\r\n"
        "data=\"/mods/Old\"\r\n"
        "content=Morrowind.esm\r\n"
        "# --- Nerevarine Organizer END ---\r\n";

    const QString out = renderOpenMWConfig({}, {}, existing);

    check("no \\r in output",        !out.contains('\r'));
    check("BEGIN marker recognised under CRLF",
          out.count("# --- Nerevarine Organizer BEGIN ---") == 1);
    check("external Morrowind.esm preserved despite CRLF",
          out.contains("content=Morrowind.esm"));
}

static void testNoPhantomBlankLine()
{
    std::cout << "testNoPhantomBlankLine\n";

    const QString existing = "# preamble\n";
    const QString out = renderOpenMWConfig({}, {}, existing);

    check("no phantom blank line from trailing \\n",
          out.startsWith("# preamble\n# --- Nerevarine Organizer BEGIN ---\n"));
}

static void testMultiplePluginDirs()
{
    std::cout << "testMultiplePluginDirs\n";

    ConfigMod m;
    m.enabled = m.installed = true;
    m.pluginDirs = {
        {"/mods/Pack/00 Core",     {"Core.esm"}},
        {"/mods/Pack/01 Optional", {"Optional.esp"}},
        {"/mods/Pack/02 Patches",  {"PatchA.esp", "PatchB.esp"}},
    };
    const QString out = renderOpenMWConfig(
        {m}, {"Core.esm", "Optional.esp", "PatchA.esp", "PatchB.esp"}, {});

    check("each pluginDir produces a data= line",
          out.contains("data=\"/mods/Pack/00 Core\"")
          && out.contains("data=\"/mods/Pack/01 Optional\"")
          && out.contains("data=\"/mods/Pack/02 Patches\""));
    check("data= emitted in pluginDirs order",
          out.indexOf("/00 Core") < out.indexOf("/01 Optional")
          && out.indexOf("/01 Optional") < out.indexOf("/02 Patches"));
    check("all plugins surfaced as content=",
          out.contains("content=Core.esm")
          && out.contains("content=Optional.esp")
          && out.contains("content=PatchA.esp")
          && out.contains("content=PatchB.esp"));
}

static void testMultipleResourceRoots()
{
    std::cout << "testMultipleResourceRoots\n";

    ConfigMod m;
    m.enabled = m.installed = true;
    m.resourceRoots = {"/mods/Pack/textures-hd", "/mods/Pack/sounds-hd"};

    const QString out = renderOpenMWConfig({m}, {}, {});
    check("first root emitted",
          out.contains("data=\"/mods/Pack/textures-hd\""));
    check("second root emitted",
          out.contains("data=\"/mods/Pack/sounds-hd\""));
    check("resource-only mod still emits no content=",
          !out.contains("content="));
}

static void testLoadOrderReferencesUnknownPlugin()
{
    std::cout << "testLoadOrderReferencesUnknownPlugin\n";

    const QList<ConfigMod> mods = {plugin("/mods/A", {"A.esp"})};
    const QString out = renderOpenMWConfig(
        mods, {"A.esp", "GhostOfPluginPast.esp"}, {});

    check("known plugin still emitted",         out.contains("content=A.esp"));
    check("unknown plugin silently dropped",
          !out.contains("content=GhostOfPluginPast.esp"));
}

static void testIdempotentReRun()
{
    std::cout << "testIdempotentReRun\n";

    const QList<ConfigMod> mods = {
        plugin("/mods/A", {"A.esp"}),
        plugin("/mods/B", {"B.esm", "B.esp"}),
    };
    const QStringList lo = {"B.esm", "A.esp", "B.esp"};
    const QString existing =
        "# user preamble\n"
        "fallback=GMST_FontColor,255,255,255\n"
        "data=\"/usr/share/morrowind/Data Files\"\n"
        "content=Morrowind.esm\n";

    const QString first  = renderOpenMWConfig(mods, lo, existing);
    const QString second = renderOpenMWConfig(mods, lo, first);
    check("second render equals first (fixed point)", first == second);

    const QString third  = renderOpenMWConfig(mods, lo, second);
    check("third render also equals first", first == third);
}

static void testInstalledDisabledModFullyDropped()
{
    std::cout << "testInstalledDisabledModFullyDropped\n";

    const QList<ConfigMod> mods = {
        plugin("/mods/On",  {"On.esp"}),
        plugin("/mods/Off", {"Off.esp"}, /*enabled=*/false),
    };
    const QStringList lo = {"Off.esp", "On.esp"};
    const QString out = renderOpenMWConfig(mods, lo, {});

    check("enabled mod emitted",   out.contains("content=On.esp"));
    check("disabled mod data= gone",
          !out.contains("data=\"/mods/Off\""));
    check("disabled mod content= gone (even when in loadOrder)",
          !out.contains("content=Off.esp"));
}

static void testEmptyModsPreservesPreamble()
{
    std::cout << "testEmptyModsPreservesPreamble\n";

    const QString existing =
        "# hand-tuned settings\n"
        "fallback=GMST_x,1,2,3\n"
        "data=\"/usr/share/morrowind/Data Files\"\n";
    const QString out = renderOpenMWConfig({}, {}, existing);

    check("preamble comment kept",
          out.contains("# hand-tuned settings"));
    check("fallback setting kept",
          out.contains("fallback=GMST_x,1,2,3"));
    check("external data= kept",
          out.contains("data=\"/usr/share/morrowind/Data Files\""));
    check("BEGIN marker emitted",
          out.contains("# --- Nerevarine Organizer BEGIN ---"));
    check("END marker emitted",
          out.contains("# --- Nerevarine Organizer END ---"));
    check("no content= lines (empty managed section)",
          !out.contains("content="));
}

static void testUninstalledEnabledBehavesAsExternal()
{
    std::cout << "testUninstalledEnabledBehavesAsExternal\n";

    ConfigMod uninst;
    uninst.enabled = true;
    uninst.installed = false;
    const QString existing =
        "# --- Nerevarine Organizer BEGIN ---\n"
        "content=Phantom.esp\n"
        "# --- Nerevarine Organizer END ---\n";
    const QString out = renderOpenMWConfig({uninst}, {"Phantom.esp"}, existing);

    check("external-looking plugin preserved",
          out.contains("content=Phantom.esp"));
}

static void testGroundcoverEmittedCorrectly()
{
    std::cout << "testGroundcoverEmittedCorrectly\n";

    ConfigMod gc;
    gc.enabled   = true;
    gc.installed = true;
    gc.pluginDirs = {{"/mods/VurtsGC", {"Vurt's Groundcover - BC.esp",
                                         "Vurt's Groundcover - Reeds.esp"}}};
    gc.groundcoverFiles = {"Vurt's Groundcover - BC.esp",
                           "Vurt's Groundcover - Reeds.esp"};

    ConfigMod regular = plugin("/mods/RegularMod", {"Regular.esp"});

    const QString out = renderOpenMWConfig(
        {regular, gc},
        {"Regular.esp"},
        "");

    check("data= emitted for groundcover mod",
          out.contains("data=\"/mods/VurtsGC\""));
    check("groundcover= emitted (not content=) for groundcover plugin",
          out.contains("groundcover=Vurt's Groundcover - BC.esp"));
    check("second groundcover plugin also emitted",
          out.contains("groundcover=Vurt's Groundcover - Reeds.esp"));
    check("groundcover plugin NOT in content= lines",
          !out.contains("content=Vurt's Groundcover"));
    check("regular plugin still in content=",
          out.contains("content=Regular.esp"));
}

static void testDisabledGroundcoverDropped()
{
    std::cout << "testDisabledGroundcoverDropped\n";

    ConfigMod gc;
    gc.enabled   = false;
    gc.installed = true;
    gc.pluginDirs = {{"/mods/VurtsGC", {"Vurt's Groundcover - BC.esp"}}};
    gc.groundcoverFiles = {"Vurt's Groundcover - BC.esp"};

    const QString out = renderOpenMWConfig({gc}, {}, "");

    check("disabled groundcover emits no groundcover= line",
          !out.contains("groundcover="));
    check("disabled groundcover emits no data= line",
          !out.contains("data="));
}

static void testSuppressedPluginsExcludedFromContent()
{
    std::cout << "testSuppressedPluginsExcludedFromContent\n";

    ConfigMod mod;
    mod.enabled   = true;
    mod.installed = true;
    mod.pluginDirs = {{"/mods/HlaaluSeydaNeen",
                       {"Hlaalu Seyda Neen.esp",
                        "HlaaluSeydaNeen_AFFresh_Patch.ESP"}}};
    mod.suppressedPlugins = {"HlaaluSeydaNeen_AFFresh_Patch.ESP"};

    const QString prior =
        "# --- Nerevarine Organizer BEGIN ---\n"
        "content=Hlaalu Seyda Neen.esp\n"
        "content=HlaaluSeydaNeen_AFFresh_Patch.ESP\n"
        "# --- Nerevarine Organizer END ---\n";

    const QString out = renderOpenMWConfig(
        {mod},
        {"Hlaalu Seyda Neen.esp", "HlaaluSeydaNeen_AFFresh_Patch.ESP"},
        prior);

    check("non-suppressed plugin still emitted",
          out.contains("content=Hlaalu Seyda Neen.esp"));
    check("suppressed plugin NOT in content= lines",
          !out.contains("content=HlaaluSeydaNeen_AFFresh_Patch"));
    check("suppressed plugin NOT in groundcover= lines",
          !out.contains("groundcover=HlaaluSeydaNeen_AFFresh_Patch"));
}

static void testSuppressedPluginsNotResurrectedAsExternal()
{
    std::cout << "testSuppressedPluginsNotResurrectedAsExternal\n";

    ConfigMod mod;
    mod.enabled   = true;
    mod.installed = true;
    mod.pluginDirs = {{"/mods/HlaaluSeydaNeen",
                       {"BadPatch.esp"}}};
    mod.suppressedPlugins = {"BadPatch.esp"};

    const QString prior =
        "content=BadPatch.esp\n"
        "# --- Nerevarine Organizer BEGIN ---\n"
        "# --- Nerevarine Organizer END ---\n";

    const QString out = renderOpenMWConfig({mod}, {"BadPatch.esp"}, prior);

    check("suppressed plugin not resurrected from external content=",
          !out.contains("content=BadPatch.esp"));
}

static void testDuplicatePreambleGroundcoverDropped()
{
    std::cout << "testDuplicatePreambleGroundcoverDropped\n";

    ConfigMod mod;
    mod.enabled   = true;
    mod.installed = true;
    mod.pluginDirs = {{"/mods/Grass", {"Rem_WG.esp"}}};
    mod.groundcoverFiles = {"Rem_WG.esp"};

    const QString prior =
        "groundcover=Rem_WG.esp\n"
        "# --- Nerevarine Organizer BEGIN ---\n"
        "data=\"/mods/Grass\"\n"
        "groundcover=Rem_WG.esp\n"
        "# --- Nerevarine Organizer END ---\n";

    const QString out = renderOpenMWConfig({mod}, {}, prior);

    int hits = 0;
    for (const QString &line : out.split('\n'))
        if (line == "groundcover=Rem_WG.esp") ++hits;
    check("duplicate preamble groundcover= collapsed to one line", hits == 1,
          QString::number(hits), QStringLiteral("1"));

    const int beginIx = out.indexOf("# --- Nerevarine Organizer BEGIN ---");
    const int endIx   = out.indexOf("# --- Nerevarine Organizer END ---");
    const int gcIx    = out.indexOf("groundcover=Rem_WG.esp");
    check("surviving groundcover= lives inside BEGIN/END",
          beginIx >= 0 && endIx > beginIx && gcIx > beginIx && gcIx < endIx);
}

static void testExternalGroundcoverPreservedWhenNotManaged()
{
    std::cout << "testExternalGroundcoverPreservedWhenNotManaged\n";

    ConfigMod mod;
    mod.enabled   = true;
    mod.installed = true;
    mod.pluginDirs = {{"/mods/SomethingElse", {"Different.esp"}}};
    mod.groundcoverFiles = {"Different.esp"};

    const QString prior =
        "groundcover=BaseGameGrass.esp\n"
        "# --- Nerevarine Organizer BEGIN ---\n"
        "# --- Nerevarine Organizer END ---\n";

    const QString out = renderOpenMWConfig({mod}, {}, prior);

    check("truly-external groundcover= still preserved",
          out.contains("groundcover=BaseGameGrass.esp"));
    check("managed groundcover= emitted too",
          out.contains("groundcover=Different.esp"));
}

static void testBsaEmitsFallbackArchive()
{
    std::cout << "testBsaEmitsFallbackArchive\n";

    ConfigMod m = plugin("/mods/AuthenticSigns",
                          {"Authentic Signs IT 1.1.esp"});
    m.bsaFiles = {"Authentic Signs IT.bsa"};

    const QString out = renderOpenMWConfig({m}, {"Authentic Signs IT 1.1.esp"}, {});

    check("fallback-archive= line is in the managed block",
          out.contains("fallback-archive=Authentic Signs IT.bsa"));
    check("fallback-archive= sits AFTER the BEGIN marker",
          out.indexOf("fallback-archive=Authentic Signs IT.bsa")
            > out.indexOf("# --- Nerevarine Organizer BEGIN ---"));
    check("fallback-archive= sits BEFORE the matching content=",
          out.indexOf("fallback-archive=Authentic Signs IT.bsa")
            < out.indexOf("content=Authentic Signs IT 1.1.esp"));
}

static void testDisabledModBsaNotEmitted()
{
    std::cout << "testDisabledModBsaNotEmitted\n";

    ConfigMod m = plugin("/mods/AuthSigns", {"AuthSigns.esp"}, /*enabled=*/false);
    m.bsaFiles = {"AuthSigns.bsa"};

    const QString out = renderOpenMWConfig({m}, {}, {});
    check("disabled mod's BSA is NOT registered",
          !out.contains("fallback-archive=AuthSigns.bsa"));
}

static void testVanillaFallbackArchivePreserved()
{
    std::cout << "testVanillaFallbackArchivePreserved\n";

    const QString existing =
        "fallback-archive=Morrowind.bsa\n"
        "fallback-archive=Tribunal.bsa\n"
        "fallback-archive=Bloodmoon.bsa\n";

    ConfigMod m = plugin("/mods/Foo", {"Foo.esp"});
    m.bsaFiles = {"Foo.bsa"};

    const QString out = renderOpenMWConfig({m}, {"Foo.esp"}, existing);

    check("Morrowind.bsa preserved",
          out.contains("fallback-archive=Morrowind.bsa"));
    check("Tribunal.bsa preserved",
          out.contains("fallback-archive=Tribunal.bsa"));
    check("Bloodmoon.bsa preserved",
          out.contains("fallback-archive=Bloodmoon.bsa"));
    check("New mod BSA also added",
          out.contains("fallback-archive=Foo.bsa"));
}

static void testPreambleBsaDuplicateDropped()
{
    std::cout << "testPreambleBsaDuplicateDropped\n";

    const QString existing =
        "fallback-archive=Morrowind.bsa\n"
        "fallback-archive=Authentic Signs IT.bsa\n";

    ConfigMod m = plugin("/mods/AS", {"AS.esp"});
    m.bsaFiles = {"Authentic Signs IT.bsa"};

    const QString out = renderOpenMWConfig({m}, {"AS.esp"}, existing);

    int count = 0, from = 0;
    while ((from = out.indexOf("fallback-archive=Authentic Signs IT.bsa",
                                from)) >= 0) {
        ++count;
        ++from;
    }
    check("BSA registered exactly once (preamble dup scrubbed)",
          count == 1);
}

static void testBsaIdempotent()
{
    std::cout << "testBsaIdempotent\n";

    ConfigMod m = plugin("/mods/Auth", {"Auth.esp"});
    m.bsaFiles = {"Auth.bsa"};

    const QString first  = renderOpenMWConfig({m}, {"Auth.esp"}, {});
    const QString second = renderOpenMWConfig({m}, {"Auth.esp"}, first);
    check("re-render is byte-identical", first == second, second, first);
}

// importer parser

static void testParseConfigEntries_basic()
{
    std::cout << "testParseConfigEntries_basic\n";
    const QString cfg =
        "# generated by openmw-launcher\n"
        "data=\"/home/user/Games/Morrowind/Data Files\"\n"
        "data=/home/user/openmw_mods/OAAB_Data\n"
        "content=Morrowind.esm\n"
        "content=Tribunal.esm\n"
        "content=OAAB_Data.esm\n"
        "groundcover=AURA-Grass.esp\n"
        "fallback=Pickaxe::Damage,300\n"
        "\n"
        "resolution x=2560\n";
    const auto e = openmw::parseConfigEntries(cfg);
    check("3 data lines parsed", e.dataPaths.size() == 2);
    check("first data unquoted",
          e.dataPaths.value(0) == "/home/user/Games/Morrowind/Data Files");
    check("second data preserved", e.dataPaths.value(1)
          == "/home/user/openmw_mods/OAAB_Data");
    check("3 content lines preserved + ordered",
          e.contentFiles == QStringList{"Morrowind.esm", "Tribunal.esm",
                                        "OAAB_Data.esm"});
    check("groundcover captured separately",
          e.groundcoverFiles == QStringList{"AURA-Grass.esp"});
}

static void testParseConfigEntries_emptyAndComments()
{
    std::cout << "testParseConfigEntries_emptyAndComments\n";
    const auto e1 = openmw::parseConfigEntries({});
    check("empty input → empty struct",
          e1.dataPaths.isEmpty() && e1.contentFiles.isEmpty()
          && e1.groundcoverFiles.isEmpty());

    const QString cfg =
        "# only comments here\n"
        "  # indented comment\n"
        "\r\n"
        "\n";
    const auto e2 = openmw::parseConfigEntries(cfg);
    check("comments-and-blanks → empty",
          e2.dataPaths.isEmpty() && e2.contentFiles.isEmpty());
}

static void testParseConfigEntries_crlfTolerated()
{
    std::cout << "testParseConfigEntries_crlfTolerated\n";
    const QString cfg = "data=/foo\r\ncontent=Bar.esp\r\n";
    const auto e = openmw::parseConfigEntries(cfg);
    check("CRLF data= parses",     e.dataPaths    == QStringList{"/foo"});
    check("CRLF content= parses",  e.contentFiles == QStringList{"Bar.esp"});
}

static void testExternalDataPaths()
{
    std::cout << "testExternalDataPaths\n";
    const QString cfg =
        "data=\"/usr/share/games/openmw/data\"\n"
        "data=/home/u/Morrowind/Data Files\n"
        "# --- Nerevarine Organizer BEGIN ---\n"
        "data=\"/managed/mod1\"\n"
        "data=\"/managed/mod2\"\n"
        "# --- Nerevarine Organizer END ---\n"
        "content=Morrowind.esm\n";

    const QStringList ext = openmw::externalDataPaths(cfg);
    check("parses both external data= lines", ext.size() == 2);
    check("strips surrounding quotes",  ext.value(0) == "/usr/share/games/openmw/data");
    check("keeps unquoted path as-is",  ext.value(1) == "/home/u/Morrowind/Data Files");
    check("managed-block data= excluded", !ext.contains("/managed/mod1")
                                       && !ext.contains("/managed/mod2"));
}

static void testVanillaFolderDetection()
{
    std::cout << "testVanillaFolderDetection\n";
    QTemporaryDir tmp;
    tmp.setAutoRemove(true);
    if (!tmp.isValid()) {
        check("temp dir created", false);
        return;
    }
    QDir d(tmp.path());
    auto touch = [&](const QString &name) {
        QFile f(d.filePath(name));
        if (f.open(QIODevice::WriteOnly)) f.close();
    };

    check("empty dir is not vanilla",
          !openmw::looksLikeVanillaDataFolder(tmp.path()));

    touch("Morrowind.esm");
    check("Morrowind.esm alone is not enough",
          !openmw::looksLikeVanillaDataFolder(tmp.path()));

    touch("Tribunal.esm");
    check("Morrowind.esm + Tribunal.esm flags as vanilla",
          openmw::looksLikeVanillaDataFolder(tmp.path()));

    QTemporaryDir tmp2;
    QDir d2(tmp2.path());
    QFile f(d2.filePath("Tribunal.esm"));
    if (f.open(QIODevice::WriteOnly)) f.close();
    check("expansion alone (no Morrowind.esm) is not vanilla",
          !openmw::looksLikeVanillaDataFolder(tmp2.path()));
}

static void run_config_writer()
{
    testMinimalRender();
    testReorderBeatsExistingCfg();
    testResourceModEmitsData();
    testDisabledResourceModDropped();
    testPreambleAndExternalDataPreserved();
    testDisabledManagedContentDropped();
    testEsmBeforeEsp();
    testCrlfInputTolerated();
    testNoPhantomBlankLine();
    testMultiplePluginDirs();
    testMultipleResourceRoots();
    testLoadOrderReferencesUnknownPlugin();
    testIdempotentReRun();
    testInstalledDisabledModFullyDropped();
    testEmptyModsPreservesPreamble();
    testUninstalledEnabledBehavesAsExternal();
    testGroundcoverEmittedCorrectly();
    testDisabledGroundcoverDropped();
    testSuppressedPluginsExcludedFromContent();
    testSuppressedPluginsNotResurrectedAsExternal();
    testDuplicatePreambleGroundcoverDropped();
    testExternalGroundcoverPreservedWhenNotManaged();
    testBsaEmitsFallbackArchive();
    testDisabledModBsaNotEmitted();
    testVanillaFallbackArchivePreserved();
    testPreambleBsaDuplicateDropped();
    testBsaIdempotent();
    testParseConfigEntries_basic();
    testParseConfigEntries_emptyAndComments();
    testParseConfigEntries_crlfTolerated();
    testExternalDataPaths();
    testVanillaFolderDetection();
}

// renderLauncherCfg + readers

using openmw::renderLauncherCfg;
using openmw::readLauncherCfgContentOrder;
using openmw::readLauncherCfgDataPaths;

static void testEmptyInput()
{
    std::cout << "testEmptyInput\n";
    const QString out = renderLauncherCfg({}, {"/a"}, {"A.esp"});
    check("empty input returns empty string", out.isEmpty(), out, QStringLiteral("<empty>"));
}

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
    const QString in =
        "[Profiles]\n"
        "currentprofile=TS\n"
        "TS/data=\"/home/user/Games/Morrowind/Data Files\"\n";
    check("quoted data= path is unquoted on read",
          readLauncherCfgDataPaths(in)
            == QStringList{"/home/user/Games/Morrowind/Data Files"});
}

static void run_launcher_cfg()
{
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
}

// prepareForSync

#define QVERIFY_EXIT(cond, code) \
    do { if (!(cond)) { std::cerr << "Setup failed: " #cond "\n"; std::exit(code); } } while (0)

namespace {

QString writePlugin(const QString &dir, const QString &name,
                     const QStringList &masters = {})
{
    QDir().mkpath(dir);
    QFile f(dir + "/" + name);
    QVERIFY_EXIT(f.open(QIODevice::WriteOnly), 2);

    if (masters.isEmpty()) {
        // empty TES3 header -> no masters
        const quint32 recSize = 0;
        f.write("TES3", 4);
        f.write(reinterpret_cast<const char *>(&recSize), 4);
        f.write(QByteArray(8, '\0'));
    } else {
        QByteArray body;
        for (const QString &m : masters) {
            const QByteArray name8 = m.toLatin1();
            body.append("MAST", 4);
            const quint32 sz = name8.size() + 1;
            body.append(reinterpret_cast<const char *>(&sz), 4);
            body.append(name8);
            body.append('\0');
        }
        const quint32 recSize = body.size();
        f.write("TES3", 4);
        f.write(reinterpret_cast<const char *>(&recSize), 4);
        f.write(QByteArray(8, '\0'));
        f.write(body);
    }
    return dir + "/" + name;
}

openmw::ConfigMod modWith(const QString &modPath, const QStringList &plugins,
                            bool enabled = true)
{
    openmw::ConfigMod cm;
    cm.enabled   = enabled;
    cm.installed = true;
    if (!plugins.isEmpty()) cm.pluginDirs.append({modPath, plugins});
    return cm;
}

void testOrphanContentScrubbed()
{
    std::cout << "\n[content= for plugin no managed mod provides → dropped + counted]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);
    const QString modsRoot = tmp.path() + "/mods";
    const QString modPath  = modsRoot + "/MyMod";
    writePlugin(modPath, "MyMod.esp");

    openmw::SyncPrepareInputs in;
    in.modsRoot         = modsRoot;
    in.managedModPaths  = { QDir::cleanPath(modPath) };
    in.mods             = { modWith(modPath, {"MyMod.esp"}) };
    in.loadOrder        = { "MyMod.esp", "OldGhost.esp" };
    in.existingCfg      =
        "data=\"" + modPath + "\"\n"
        "content=MyMod.esp\n"
        "content=OldGhost.esp\n";  // no mod provides this

    auto out = openmw::prepareForSync(in);

    check("droppedOrphans counts the missing provider",
          out.droppedOrphans == 1);
    check("scrubbedExisting drops the orphan content= line",
          !out.scrubbedExisting.contains("content=OldGhost.esp"));
    check("scrubbedExisting keeps the satisfied content= line",
          out.scrubbedExisting.contains("content=MyMod.esp"));
    check("effective load order drops the orphan",
          out.effectiveLoadOrder == QStringList{"MyMod.esp"});
}

void testOrphanDataPathScrubbed()
{
    std::cout << "\n[data= under modsRoot with no managed claim → scrubbed]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);
    const QString modsRoot   = tmp.path() + "/mods";
    const QString claimedDir = modsRoot + "/Active";
    const QString orphanDir  = modsRoot + "/Stale";
    writePlugin(claimedDir, "Active.esp");
    QDir().mkpath(orphanDir);   // empty, so nothing to rescue

    openmw::SyncPrepareInputs in;
    in.modsRoot         = modsRoot;
    in.managedModPaths  = { QDir::cleanPath(claimedDir) };
    in.mods             = { modWith(claimedDir, {"Active.esp"}) };
    in.loadOrder        = { "Active.esp" };
    in.existingCfg      =
        "data=\"" + claimedDir + "\"\n"
        "data=\"" + orphanDir + "\"\n"      // outside BEGIN/END
        "content=Active.esp\n";

    auto out = openmw::prepareForSync(in);

    check("orphan data= path inside modsRoot is dropped",
          !out.scrubbedExisting.contains(orphanDir));
    check("claimed data= path is preserved",
          out.scrubbedExisting.contains(claimedDir));
}

void testOrphanRescuePreservesPluginsOnDisk()
{
    std::cout << "\n[orphan-managed dir with plugins on disk → rescued outside BEGIN/END]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);
    const QString modsRoot     = tmp.path() + "/mods";
    const QString rescueDir    = modsRoot + "/StillOnDisk";
    writePlugin(rescueDir, "Survivor.esp");
    // not managed anymore, but folder + plugin still on disk

    openmw::SyncPrepareInputs in;
    in.modsRoot         = modsRoot;
    in.managedModPaths  = {};
    in.mods             = {};
    in.loadOrder        = { "Survivor.esp" };
    in.existingCfg      =
        "# --- Nerevarine Organizer BEGIN ---\n"
        "data=\"" + rescueDir + "\"\n"
        "content=Survivor.esp\n"
        "# --- Nerevarine Organizer END ---\n";

    auto out = openmw::prepareForSync(in);

    const int rescuedIdx = out.scrubbedExisting.indexOf(
        QStringLiteral("data=\"") + rescueDir);
    const int beginIdx   = out.scrubbedExisting.indexOf(
        QStringLiteral("# --- Nerevarine Organizer BEGIN ---"));
    check("rescued data= appears before BEGIN marker",
          rescuedIdx >= 0 && beginIdx >= 0 && rescuedIdx < beginIdx);
    check("Survivor.esp content= survives orphan scrub",
          out.scrubbedExisting.contains("content=Survivor.esp"));
    check("Survivor.esp survives in effective load order",
          out.effectiveLoadOrder.contains("Survivor.esp"));
}

void testLauncherOnlyExternalsAugmented()
{
    std::cout << "\n[launcher.cfg has data=/content= absent from openmw.cfg → synthesized]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);
    const QString modsRoot   = tmp.path() + "/mods";
    const QString modPath    = modsRoot + "/MyMod";
    writePlugin(modPath, "MyMod.esp");

    const QString vanillaDir = tmp.path() + "/vanilla/Data Files";
    writePlugin(vanillaDir, "Morrowind.esm");

    openmw::SyncPrepareInputs in;
    in.modsRoot         = modsRoot;
    in.managedModPaths  = { QDir::cleanPath(modPath) };
    in.mods             = { modWith(modPath, {"MyMod.esp"}) };
    in.loadOrder        = { "MyMod.esp" };
    in.existingCfg      =
        "data=\"" + modPath + "\"\n"
        "content=MyMod.esp\n";
    in.launcherCfgText  =
        "[Profiles]\n"
        "currentprofile=Default\n"
        "Default/data=" + vanillaDir + "\n"
        "Default/content=Morrowind.esm\n";

    auto out = openmw::prepareForSync(in);

    check("vanilla data= line synthesized into existing",
          out.scrubbedExisting.contains(vanillaDir));
    check("Morrowind.esm content= synthesized",
          out.scrubbedExisting.contains("content=Morrowind.esm"));
    check("Morrowind.esm survives the orphan-content scrub (.esm on disk)",
          out.scrubbedExisting.contains("content=Morrowind.esm"));
    check("MyMod.esp content= preserved",
          out.scrubbedExisting.contains("content=MyMod.esp"));
}

void testLauncherSyntheticUnderModsRootNotRecreated()
{
    std::cout << "\n[launcher.cfg data= under modsRoot → NOT synthesized (managed)]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);
    const QString modsRoot = tmp.path() + "/mods";
    const QString modPath  = modsRoot + "/MyMod";
    writePlugin(modPath, "MyMod.esp");

    openmw::SyncPrepareInputs in;
    in.modsRoot         = modsRoot;
    in.managedModPaths  = { QDir::cleanPath(modPath) };
    in.mods             = { modWith(modPath, {"MyMod.esp"}) };
    in.loadOrder        = { "MyMod.esp" };
    in.existingCfg      = "";
    in.launcherCfgText  =
        "[Profiles]\n"
        "currentprofile=Default\n"
        "Default/data=" + modPath + "\n"      // a managed path
        "Default/content=MyMod.esp\n";

    auto out = openmw::prepareForSync(in);

    const int dataCount = out.scrubbedExisting.count(
        QStringLiteral("data=") + modPath);
    check("managed mod's data= NOT synthesized into preamble",
          dataCount == 0);
    check("managed mod's content= NOT synthesized",
          !out.scrubbedExisting.contains("content=MyMod.esp"));
}

void testUnsatisfiedMasterSuppressesPlugin()
{
    std::cout << "\n[plugin whose master isn't on disk → moved to suppressedPlugins]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);
    const QString modsRoot = tmp.path() + "/mods";
    const QString modPath  = modsRoot + "/Patch";
    writePlugin(modPath, "Patch.esp", {"Required.esm"});

    openmw::SyncPrepareInputs in;
    in.modsRoot         = modsRoot;
    in.managedModPaths  = { QDir::cleanPath(modPath) };
    in.mods             = { modWith(modPath, {"Patch.esp"}) };
    in.loadOrder        = { "Patch.esp" };
    in.existingCfg      = "";

    auto out = openmw::prepareForSync(in);

    QVERIFY_EXIT(out.mods.size() == 1, 3);
    check("Patch.esp landed in suppressedPlugins",
          out.mods[0].suppressedPlugins.contains("Patch.esp"));
    check("Patch.esp is dropped from effective load order",
          !out.effectiveLoadOrder.contains("Patch.esp"));
}

void testSatisfiedMasterPreserved()
{
    std::cout << "\n[plugin master in vanilla baseline → not suppressed]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);
    const QString modsRoot = tmp.path() + "/mods";
    const QString modPath  = modsRoot + "/Mod";
    writePlugin(modPath, "Mod.esp", {"Morrowind.esm"});

    openmw::SyncPrepareInputs in;
    in.modsRoot         = modsRoot;
    in.managedModPaths  = { QDir::cleanPath(modPath) };
    in.mods             = { modWith(modPath, {"Mod.esp"}) };
    in.loadOrder        = { "Mod.esp" };
    in.existingCfg      = "";

    auto out = openmw::prepareForSync(in);

    QVERIFY_EXIT(out.mods.size() == 1, 3);
    check("Mod.esp NOT suppressed (vanilla master available)",
          !out.mods[0].suppressedPlugins.contains("Mod.esp"));
    check("Mod.esp survives in effective load order",
          out.effectiveLoadOrder.contains("Mod.esp"));
}

void testGroundcoverDroppedFromLoadOrder()
{
    std::cout << "\n[groundcover plugin in input load order → dropped from effective]\n";
    QTemporaryDir tmp;
    QVERIFY_EXIT(tmp.isValid(), 1);
    const QString modsRoot = tmp.path() + "/mods";
    const QString modPath  = modsRoot + "/Grass";
    writePlugin(modPath, "GrassMod.esp");

    openmw::ConfigMod cm = modWith(modPath, {"GrassMod.esp"});
    cm.groundcoverFiles = { "GrassMod.esp" };

    openmw::SyncPrepareInputs in;
    in.modsRoot         = modsRoot;
    in.managedModPaths  = { QDir::cleanPath(modPath) };
    in.mods             = { cm };
    in.loadOrder        = { "GrassMod.esp" };
    in.existingCfg      = "";

    auto out = openmw::prepareForSync(in);

    check("GrassMod.esp dropped from effective load order",
          out.effectiveLoadOrder.isEmpty());
}

void testEmptyInputs()
{
    std::cout << "\n[empty inputs → empty outputs, no crash]\n";
    openmw::SyncPrepareInputs in;
    auto out = openmw::prepareForSync(in);
    check("scrubbedExisting empty",        out.scrubbedExisting.isEmpty());
    check("effectiveLoadOrder empty",      out.effectiveLoadOrder.isEmpty());
    check("mods empty",                    out.mods.isEmpty());
    check("droppedOrphans is zero",        out.droppedOrphans == 0);
}

} // namespace anon

static void run_sync_prep()
{
    std::cout << "=== openmw::prepareForSync ===\n";
    testOrphanContentScrubbed();
    testOrphanDataPathScrubbed();
    testOrphanRescuePreservesPluginsOnDisk();
    testLauncherOnlyExternalsAugmented();
    testLauncherSyntheticUnderModsRootNotRecreated();
    testUnsatisfiedMasterSuppressesPlugin();
    testSatisfiedMasterPreserved();
    testGroundcoverDroppedFromLoadOrder();
    testEmptyInputs();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    run_config_writer();
    run_launcher_cfg();
    run_sync_prep();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
