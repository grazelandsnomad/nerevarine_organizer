// tests/test_openmw_config_writer.cpp
//
// Golden-file round-trip tests for openmw::renderOpenMWConfig.
//
// The writer is pure: (mods, loadOrder, existingCfg) → new cfg text.
// That makes the regressions that lived in the old 7k-line syncOpenMWConfig
// trivially reproducible here.  Two specific regressions motivated the
// extraction and are pinned by the tests below:
//
//   1. "stale load order clobbers launcher changes after a mod reorder"
//      - reordering mods in the UI must reflect in content= order.
//
//   2. "missing data= for pure resource mods"
//      - mods with no plugins (retextures, main-menu replacers, sound packs)
//        must still contribute a data= line pointing at their resource root.
//
// Build + run:
//   ./build/tests/test_openmw_config_writer

#include "openmwconfigwriter.h"

#include <QCoreApplication>

#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok,
                  const QString &got = {}, const QString &want = {})
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name << "\n";
        if (!want.isEmpty() || !got.isEmpty()) {
            std::cout << "    --- want ---\n" << want.toStdString() << "\n";
            std::cout << "    ---  got ---\n" << got.toStdString()  << "\n";
        }
        ++s_failed;
    }
}

using openmw::ConfigMod;
using openmw::renderOpenMWConfig;

// -- Fixture helpers ---

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

// -- Tests ---

// Minimal happy path: single enabled mod, empty cfg, should produce
// a managed section with data= and content= lines bracketed by BEGIN/END.
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

// Regression #1 - stale load order clobbering launcher changes.
//
// Scenario: user reorders B above A in the organizer UI, and the load order
// authoritatively puts B.esp before A.esp.  The PREVIOUS cfg has Morrowind.esm
// (launcher-owned) + A.esp, B.esp (old modlist order).  After re-sync:
//   - Morrowind.esm keeps its spot (external plugin, not managed).
//   - Managed content comes out in loadOrder order: B.esp then A.esp.
// A buggy writer that preserves the OLD cfg's content= order would emit
// A.esp before B.esp, silently undoing the user's reorder - that's the
// regression this test pins.
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

    // Modlist order: B above A (user drag-drop).
    // Load order authoritative: B.esp then A.esp.
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

// Regression #2 - missing data= for pure resource mods.
//
// Pure resource mod (no plugins, just textures/meshes) MUST contribute
// its resourceRoots as data= entries.  A buggy writer that only emits
// data= when pluginDirs is non-empty would drop the retexture entirely
// and OpenMW wouldn't load the textures - that's the regression this
// test pins.
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

// Resource roots are ignored when the mod is installed but NOT enabled -
// otherwise disabling a retexture in the UI wouldn't actually disable it.
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

// Preamble (settings, fallback=) and external data= (base game install)
// from the existing cfg must be preserved verbatim outside the managed
// section, so the OpenMW launcher finds them where it expects.
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

// Disabling a mod whose content was present in the prior cfg MUST drop
// its content= line.  This is Pass-3 rule C - managed-and-now-disabled
// content is discarded, not preserved from the old cfg.
static void testDisabledManagedContentDropped()
{
    std::cout << "testDisabledManagedContentDropped\n";

    const QString existing =
        "# --- Nerevarine Organizer BEGIN ---\n"
        "data=\"/mods/A\"\n"
        "content=A.esp\n"
        "# --- Nerevarine Organizer END ---\n";

    // Mod still installed so the writer knows A.esp is "ours" (in
    // allManagedContent) - but disabled, so it should be dropped.
    const QList<ConfigMod> mods = {
        plugin("/mods/A", {"A.esp"}, /*enabled=*/false),
    };

    const QString out = renderOpenMWConfig(mods, {"A.esp"}, existing);

    check("disabled managed content dropped", !out.contains("content=A.esp"));
    check("disabled mod emits no data=",      !out.contains("data=\"/mods/A\""));
}

// Within a single mod folder, .esm files must sort before .esp, then
// case-insensitive alphabetical.  This is how a mod that ships a master
// + patch .esp gets the master loaded first without the user doing
// anything special.
static void testEsmBeforeEsp()
{
    std::cout << "testEsmBeforeEsp\n";

    // NOTE: loadOrder is empty here, so the writer falls back to the
    // modlist-derived (sorted) order - exactly what we want to test.
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

// CRLF line endings in existing cfg must not leak into the output
// (nor into preamble-line equality checks for BEGIN/END markers).
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

// Trailing newline in existing cfg must not produce a phantom blank
// line in the output preamble (QString::split('\n') quirk).
static void testNoPhantomBlankLine()
{
    std::cout << "testNoPhantomBlankLine\n";

    const QString existing = "# preamble\n";
    const QString out = renderOpenMWConfig({}, {}, existing);

    // Exactly one newline after "# preamble" before BEGIN - not two.
    check("no phantom blank line from trailing \\n",
          out.startsWith("# preamble\n# --- Nerevarine Organizer BEGIN ---\n"));
}

// A mod with multiple pluginDirs (FOMOD-style "00 Core" + "01 Optional"
// both enabled) must emit EACH as its own data= line, in the order the
// dirs appear.  Regression target: a writer that emits only the first
// folder would silently drop optional content.
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

// Pure resource mod with multiple resourceRoots - common when a mod ships
// both "Textures High" and "Sound Replacer" as sibling folders and both
// are marked as installable resources.  Each root must contribute a data=.
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

// A loadOrder entry that isn't provided by any enabled mod must be
// silently ignored.  Defensive path: happens whenever reconcileLoadOrder
// hasn't caught up to an uninstall yet.
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

// Running the renderer on its own output should be a fixed point: feeding
// (mods, loadOrder, previousOutput) back in produces byte-identical bytes.
// This is what the "sync → re-open app → sync again" round-trip does in
// production, and divergence here is how cfg files slowly accumulate drift.
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

    // Sanity: rendering once more after the fixed point also produces
    // the same output - belt-and-braces against a two-pass oscillation.
    const QString third  = renderOpenMWConfig(mods, lo, second);
    check("third render also equals first", first == third);
}

// Installed but disabled mod: its plugins are part of allManagedContent
// (so they're classified as "ours" rather than "external" / base-game),
// but they must NOT leak into the output.  Earlier versions of the writer
// had a bug where a plugin appearing in loadOrder AND installed-but-
// disabled would still be emitted.
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

// Empty mod list against a rich existing cfg should preserve the preamble
// and external data= lines verbatim, and emit an empty managed section.
// Regression target: a writer that accidentally drops the preamble when
// it has nothing managed to emit.
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

// Uninstalled (but enabled in the UI) mod: its plugins are NOT in
// allManagedContent, and any previously-written content= lines for them
// should be preserved as "external" so a half-migrated state doesn't
// silently drop base-game-neighbouring plugins.
static void testUninstalledEnabledBehavesAsExternal()
{
    std::cout << "testUninstalledEnabledBehavesAsExternal\n";

    // UI: user sees the mod in the list with its box checked, but it's
    // not actually installed (ModPath doesn't exist yet - download
    // pending, or filesystem outage).  pluginDirs is empty because
    // collectDataFolders found nothing.
    ConfigMod uninst;
    uninst.enabled = true;
    uninst.installed = false;
    // Previously-written cfg still mentions the plugin.
    const QString existing =
        "# --- Nerevarine Organizer BEGIN ---\n"
        "content=Phantom.esp\n"
        "# --- Nerevarine Organizer END ---\n";
    const QString out = renderOpenMWConfig({uninst}, {"Phantom.esp"}, existing);

    // Phantom.esp isn't in allManagedContent (pluginDirs empty), so Pass 3
    // Phase A keeps it as "external" and re-emits it.  This is the behaviour
    // that prevents dropping Morrowind.esm/Tribunal.esm if their mod rows
    // temporarily show "not installed" due to a path blip.
    check("external-looking plugin preserved",
          out.contains("content=Phantom.esp"));
}

// Groundcover plugins should be emitted as groundcover= instead of content=.
// Their data= path must still be present for meshes/textures.
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

// Disabled groundcover mod should not emit groundcover= lines.
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

// Plugins listed in suppressedPlugins must not appear in content= OR
// groundcover=, and must not be re-emitted even if the previous cfg had
// them as an external content= line.  Regression test for the
// "File X asks for parent file Y, but it is not available" fatal error
// that fired when a mod bundled an optional patch ESP for a companion
// mod the user didn't install (Hlaalu Seyda Neen shipping
// HlaaluSeydaNeen_AFFresh_Patch.ESP with AFFresh.esm as a master).
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

    // Simulate a prior cfg that had the bad patch enabled (launcher
    // would have written it; absorb would have pulled it into loadOrder).
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

// Suppressed plugins that were listed as external content= in the old cfg
// (i.e. their owning mod looked uninstalled at the time the launcher ran)
// must also get dropped - Phase A of the writer re-emits external-looking
// content unless it's explicitly suppressed.  Without this check the
// suppressed-patch scenario above can slip through the "external" path.
static void testSuppressedPluginsNotResurrectedAsExternal()
{
    std::cout << "testSuppressedPluginsNotResurrectedAsExternal\n";

    ConfigMod mod;
    mod.enabled   = true;
    mod.installed = true;
    mod.pluginDirs = {{"/mods/HlaaluSeydaNeen",
                       {"BadPatch.esp"}}};
    mod.suppressedPlugins = {"BadPatch.esp"};

    // Prior cfg had BadPatch as a content= entry BEFORE our managed
    // section - looks external to the parser.
    const QString prior =
        "content=BadPatch.esp\n"
        "# --- Nerevarine Organizer BEGIN ---\n"
        "# --- Nerevarine Organizer END ---\n";

    const QString out = renderOpenMWConfig({mod}, {"BadPatch.esp"}, prior);

    check("suppressed plugin not resurrected from external content=",
          !out.contains("content=BadPatch.esp"));
}

// External groundcover= lines that duplicate a plugin already being emitted
// inside the managed section must be dropped, not preserved in the preamble.
//
// Context: the OpenMW Launcher rewrites openmw.cfg when the user opens it,
// occasionally promoting our managed `groundcover=X.esp` lines into the
// preamble (outside BEGIN/END).  The writer used to preserve every non-
// managed groundcover= line unconditionally, so on the next sync we ended
// up with the plugin listed twice - once at the top, once in managed.
// OpenMW loads duplicate groundcover entries as additional content files,
// bumping every subsequent plugin's content-file index; the savegame's
// stored (content-file, refNum) tuples then don't resolve and every grass
// ref becomes UNKNOWN_GRASS rendered with marker_error meshes.
static void testDuplicatePreambleGroundcoverDropped()
{
    std::cout << "testDuplicatePreambleGroundcoverDropped\n";

    ConfigMod mod;
    mod.enabled   = true;
    mod.installed = true;
    mod.pluginDirs = {{"/mods/Grass", {"Rem_WG.esp"}}};
    mod.groundcoverFiles = {"Rem_WG.esp"};

    // Prior cfg has the managed groundcover= line ALSO in the preamble -
    // exactly the shape the launcher leaves behind after it rewrites the
    // file and breaks up the managed section.
    const QString prior =
        "groundcover=Rem_WG.esp\n"
        "# --- Nerevarine Organizer BEGIN ---\n"
        "data=\"/mods/Grass\"\n"
        "groundcover=Rem_WG.esp\n"
        "# --- Nerevarine Organizer END ---\n";

    const QString out = renderOpenMWConfig({mod}, {}, prior);

    // Exactly one groundcover=Rem_WG.esp line must remain in the output.
    int hits = 0;
    for (const QString &line : out.split('\n'))
        if (line == "groundcover=Rem_WG.esp") ++hits;
    check("duplicate preamble groundcover= collapsed to one line", hits == 1,
          QString::number(hits), QStringLiteral("1"));

    // And the surviving one is inside the managed section, not above it.
    const int beginIx = out.indexOf("# --- Nerevarine Organizer BEGIN ---");
    const int endIx   = out.indexOf("# --- Nerevarine Organizer END ---");
    const int gcIx    = out.indexOf("groundcover=Rem_WG.esp");
    check("surviving groundcover= lives inside BEGIN/END",
          beginIx >= 0 && endIx > beginIx && gcIx > beginIx && gcIx < endIx);
}

// Counterpart: a truly external groundcover= (one whose plugin is NOT in
// the current modlist at all) must STILL be preserved in the preamble -
// otherwise we'd silently clobber base-game groundcover entries or those
// added by another tool.  The dedup only kicks in when we'd duplicate.
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

// -- BSA → fallback-archive= emission ---
//
// Authentic Signs IT (Nexus 52508) was the canonical bug report: the mod's
// .esp + .bsa loaded but every sign rendered with [None] textures because
// `fallback-archive=Authentic Signs IT.bsa` was never written into
// openmw.cfg.  The writer now harvests `ConfigMod::bsaFiles` and emits
// each as a fallback-archive= line inside the managed section.
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

// A disabled installed mod's BSAs must NOT be registered (mirrors how
// content= is gated on enabled).  But the writer still has to know about
// them so a stale preamble fallback-archive= for the same name gets
// dropped when re-running on an existing cfg.
static void testDisabledModBsaNotEmitted()
{
    std::cout << "testDisabledModBsaNotEmitted\n";

    ConfigMod m = plugin("/mods/AuthSigns", {"AuthSigns.esp"}, /*enabled=*/false);
    m.bsaFiles = {"AuthSigns.bsa"};

    const QString out = renderOpenMWConfig({m}, {}, {});
    check("disabled mod's BSA is NOT registered",
          !out.contains("fallback-archive=AuthSigns.bsa"));
}

// Vanilla BSAs in the preamble (Morrowind/Tribunal/Bloodmoon) MUST NOT
// be touched - they're not in any managed mod's bsaFiles, so the dedup
// rule leaves them alone.  This is the test that makes sure the preamble
// scrub is conservative.
static void testVanillaFallbackArchivePreserved()
{
    std::cout << "testVanillaFallbackArchivePreserved\n";

    const QString existing =
        "fallback-archive=Morrowind.bsa\n"
        "fallback-archive=Tribunal.bsa\n"
        "fallback-archive=Bloodmoon.bsa\n";

    ConfigMod m = plugin("/mods/Foo", {"Foo.esp"});
    m.bsaFiles = {"Foo.bsa"};   // no overlap with vanilla names

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

// Pre-existing preamble fallback-archive= for a name we now manage must
// be DROPPED so the mod's BSA isn't registered twice.  Two sites of
// "Authentic Signs IT.bsa" was the actual on-disk bug a user could hit
// after manually editing openmw.cfg before the writer learned about
// BSAs.
static void testPreambleBsaDuplicateDropped()
{
    std::cout << "testPreambleBsaDuplicateDropped\n";

    const QString existing =
        "fallback-archive=Morrowind.bsa\n"
        "fallback-archive=Authentic Signs IT.bsa\n";

    ConfigMod m = plugin("/mods/AS", {"AS.esp"});
    m.bsaFiles = {"Authentic Signs IT.bsa"};

    const QString out = renderOpenMWConfig({m}, {"AS.esp"}, existing);

    // Exactly ONE occurrence of the BSA line in the output - the preamble
    // copy was scrubbed so the managed-section emission isn't a dup.
    int count = 0, from = 0;
    while ((from = out.indexOf("fallback-archive=Authentic Signs IT.bsa",
                                from)) >= 0) {
        ++count;
        ++from;
    }
    check("BSA registered exactly once (preamble dup scrubbed)",
          count == 1);
}

// Idempotence: re-rendering the writer's own previous output should
// produce identical bytes.  Locks in the round-trip property for the
// new fallback-archive= section.
static void testBsaIdempotent()
{
    std::cout << "testBsaIdempotent\n";

    ConfigMod m = plugin("/mods/Auth", {"Auth.esp"});
    m.bsaFiles = {"Auth.bsa"};

    const QString first  = renderOpenMWConfig({m}, {"Auth.esp"}, {});
    const QString second = renderOpenMWConfig({m}, {"Auth.esp"}, first);
    check("re-render is byte-identical", first == second, second, first);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

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

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
