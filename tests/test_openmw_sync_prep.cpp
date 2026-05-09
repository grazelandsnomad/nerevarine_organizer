// tests/test_openmw_sync_prep.cpp
//
// Pins openmw::prepareForSync, the orchestration layer carved out of
// MainWindow::syncOpenMWConfig in 0.4.  This is the highest-risk
// uncovered code in the project until now: the function that decides
// what `data=`/`content=`/`groundcover=` lines survive into the
// rendered openmw.cfg, and what gets dropped as orphan / unsatisfied /
// rescued.  The historical bugs the existing block-comments enumerate
// (orphan data= leaving stale content=, launcher.cfg silently wiped,
// missing-master crash on launch) all flow through prepareForSync,
// so a regression here breaks user setups in ways the
// renderOpenMWConfig fuzz tests can't catch.
//
// Tests use real on-disk fixtures via QTemporaryDir because the
// orchestration walks the filesystem (rescue check, external data=
// scan, master satisfaction).  Fixtures stay small - one or two .esp
// files per scenario - and lean on plugins::readTes3Masters with
// hand-built TES3 headers when master satisfaction matters.

#include "openmwconfigwriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

#define QVERIFY_EXIT(cond, code) \
    do { if (!(cond)) { std::cerr << "Setup failed: " #cond "\n"; std::exit(code); } } while (0)

namespace {

int s_passed = 0;
int s_failed = 0;

void check(const char *name, bool ok, const QString &hint = {})
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name;
        if (!hint.isEmpty()) std::cout << " (" << hint.toStdString() << ")";
        std::cout << "\n";
        ++s_failed;
    }
}

QString writePlugin(const QString &dir, const QString &name,
                     const QStringList &masters = {})
{
    QDir().mkpath(dir);
    QFile f(dir + "/" + name);
    QVERIFY_EXIT(f.open(QIODevice::WriteOnly), 2);

    if (masters.isEmpty()) {
        // Minimal TES3 header with empty body - readTes3Masters returns {}.
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

// ---------------------------------------------------------------------

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
        "content=OldGhost.esp\n";  // OldGhost has no provider

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
    QDir().mkpath(orphanDir);   // empty dir; rescue won't trigger

    openmw::SyncPrepareInputs in;
    in.modsRoot         = modsRoot;
    in.managedModPaths  = { QDir::cleanPath(claimedDir) };
    in.mods             = { modWith(claimedDir, {"Active.esp"}) };
    in.loadOrder        = { "Active.esp" };
    in.existingCfg      =
        "data=\"" + claimedDir + "\"\n"
        "data=\"" + orphanDir + "\"\n"      // outside managed section
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
    // Note: managedModPaths does NOT include rescueDir - the user
    // removed the row but the folder is still on disk.

    openmw::SyncPrepareInputs in;
    in.modsRoot         = modsRoot;
    in.managedModPaths  = {};       // nothing managed
    in.mods             = {};       // empty modlist
    in.loadOrder        = { "Survivor.esp" };
    in.existingCfg      =
        "# --- Nerevarine Organizer BEGIN ---\n"
        "data=\"" + rescueDir + "\"\n"
        "content=Survivor.esp\n"
        "# --- Nerevarine Organizer END ---\n";

    auto out = openmw::prepareForSync(in);

    // The data= line for the rescued path appears OUTSIDE the BEGIN/END
    // section in scrubbedExisting (the writer's Pass 2 will then preserve
    // it as externalDataLines).  Verify the rescued data= line precedes
    // the BEGIN marker in the scrubbed text.
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

    // Vanilla data dir lives OUTSIDE modsRoot so the augmentation
    // recognises it as external.  Put a Morrowind.esm there so the
    // orphan-plugin scrub finds the .esm and keeps content=Morrowind.esm.
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
    // launcher.cfg shape mirrors how openmw-launcher writes it (current
    // profile section under [Profiles]).
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
    in.existingCfg      = "";  // openmw.cfg empty
    in.launcherCfgText  =
        "[Profiles]\n"
        "currentprofile=Default\n"
        "Default/data=" + modPath + "\n"      // managed mod path
        "Default/content=MyMod.esp\n";

    auto out = openmw::prepareForSync(in);

    // The managed data= path must NOT appear synthesized in scrubbed
    // existing - the writer is responsible for emitting it inside
    // BEGIN/END from the modlist.  If we synthesized it here it would
    // round-trip into the preamble forever.
    const int dataCount = out.scrubbedExisting.count(
        QStringLiteral("data=") + modPath);
    check("managed mod's data= NOT synthesized into preamble",
          dataCount == 0);
    // MyMod.esp is in allManagedFilenames so the launcher synthesizer
    // skips it - the writer emits content= from the modlist anyway.
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
    // Patch.esp declares MAST=Required.esm, but Required.esm exists
    // nowhere in the input.  prepareForSync must lift Patch.esp into
    // suppressedPlugins so the writer drops it from content=.
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
    // Morrowind.esm is in baseMasters - master satisfaction passes.
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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

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

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
