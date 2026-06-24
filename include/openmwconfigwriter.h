#ifndef OPENMW_CONFIG_WRITER_H
#define OPENMW_CONFIG_WRITER_H

// Pure renderer for openmw.cfg, pulled out of MainWindow::syncOpenMWConfig.
// No widgets, no FS I/O, no MainWindow state: plain structs in, QString out,
// so it's golden-file testable.
//
// Keep tests/test_openmw_config_writer.cpp covering two past regressions:
//   1. Stale load order clobbering launcher changes after a reorder.
//   2. Missing data= lines for resource-only mods (textures/meshes, no plugins).

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

namespace openmw {

// One mod entry in modlist order, plugin layout already resolved from disk by
// the caller. The writer stays FS-free so tests can build these structs directly.
struct ConfigMod {
    bool enabled   = false;
    bool installed = false;

    // Mod subdirs holding plugins, paired with the plugins found there.
    // Usually from plugins::collectDataFolders.
    QList<QPair<QString, QStringList>> pluginDirs;

    // Fallback roots for resource-only mods (textures/meshes/sound packs).
    // Only used when pluginDirs is empty and the mod is enabled+installed.
    // Usually from plugins::collectResourceFolders, with modPath as a last
    // resort if that finds nothing.
    QStringList resourceRoots;

    // Plugins to emit as groundcover= instead of content=. OpenMW renders
    // these as instanced grass/flora with no collision; as content= they're
    // inert statics.
    QSet<QString> groundcoverFiles;

    // Plugins to exclude from both content= and groundcover=. For groundcover
    // plugins with unsatisfied masters: content= gives untextured geometry,
    // groundcover= crashes hard.
    QSet<QString> suppressedPlugins;

    // BSA basenames found under this mod's data roots. Each becomes a
    // `fallback-archive=<name>` line so OpenMW can resolve packed
    // textures/meshes. Without it, BSA-only mods (Authentic Signs IT)
    // load their .esp refs but render with [None] texture binds because
    // the fallback-archive lookup never sees the file. Enabled+installed only.
    QStringList bsaFiles;
};

// Render the new openmw.cfg.
//   mods         modlist order (top to bottom of the UI); sets insertion order
//                for newly-seen content and supplies data= paths.
//   loadOrder    order for managed plugins. External/base-game plugins keep
//                their encounter order from existingCfg.
//   existingCfg  current openmw.cfg (empty string if absent).
// Returns the full text to write back, newline-terminated.
QString renderOpenMWConfig(const QList<ConfigMod> &mods,
                           const QStringList   &loadOrder,
                           const QString       &existingCfg);

// Rewrite the current profile's data=/content= lines in the launcher's own
// state file (~/.config/openmw/launcher.cfg). Without it the launcher's Data
// Files tab keeps showing uninstalled mods: it reads its per-profile cache,
// not openmw.cfg, on open.
//
//   existingLauncherCfg  current contents. Empty string returns {}, i.e. "no
//                        file yet, nothing to do" (launcher never run).
//   dataPaths            absolute unquoted paths in load-priority order, same
//                        set as the openmw.cfg `data=` lines, base-game included.
//   contentFiles         plugin basenames in activation order, matching the
//                        rendered openmw.cfg `content=` order.
//
// Returns {} when there's no `[Profiles]` section or `currentprofile=` key
// (don't clobber a fresh or odd file). Everything outside the current
// profile's data=/content= block is preserved byte-for-byte.
QString renderLauncherCfg(const QString     &existingLauncherCfg,
                          const QStringList &dataPaths,
                          const QStringList &contentFiles);

// Current profile's `content=` filenames in launcher-written order. Powers
// absorbExternalLoadOrder's "user reordered in the Data Files tab" recovery:
// the launcher rewrites its per-profile content= list on every reorder, even
// when closed without Save/Play (so openmw.cfg stays stale). Miss this and the
// next sync rewrites openmw.cfg from a stale m_loadOrder, clobbering the reorder.
//
// Empty list when there's no `[Profiles]` section, no `currentprofile=` key,
// or the profile has no `<ts>/content=` lines.
QStringList readLauncherCfgContentOrder(const QString &existingLauncherCfg);

// Current profile's `data=` paths in encounter order. The data= counterpart
// of readLauncherCfgContentOrder; syncOpenMWConfig uses it to find paths the
// user added through the launcher (usually vanilla Tribunal/Bloodmoon Data
// Files) so they survive a rewrite. Without it a launcher-only data= path
// read as orphaned and the next sync wiped its content= companions from
// launcher.cfg, leaving the Content List with only managed mods.
//
// Paths returned UNQUOTED (the launcher stores them that way). Empty list when
// there's no `[Profiles]` section, no `currentprofile=` key, or no `<ts>/data=`.
QStringList readLauncherCfgDataPaths(const QString &existingLauncherCfg);

// Inputs for the orchestration layer between MainWindow's modlist snapshot and
// the final openmw.cfg/launcher.cfg writes. Carved out of syncOpenMWConfig so
// the orphan-rescue + launcher-augment + master-satisfaction + scrub logic
// (the riskiest surface in 0.4) is unit-testable.
//
// QtCore-only: FS access is internal via QFileInfo/QDir, no widgets, no
// QSettings, no MainWindow state. Tests use QTemporaryDir fixtures.
struct SyncPrepareInputs {
    QString          existingCfg;        // current openmw.cfg text
    QString          launcherCfgText;    // launcher.cfg text (empty = absent)
    QList<ConfigMod> mods;               // built from modlist UI state
    QSet<QString>    managedModPaths;    // paths Nerevarine manages
    QString          modsRoot;           // m_modsDir, QDir::cleanPath'd
    QStringList      loadOrder;          // current managed load order
};

// Output of prepareForSync, fed into renderOpenMWConfig + the async writers.
// Caller sets m_loadOrder = effectiveLoadOrder when changed, posts a status
// message when droppedOrphans > 0.
struct SyncPrepareResult {
    QString          scrubbedExisting;     // input to renderOpenMWConfig
    QList<ConfigMod> mods;                 // mutated copy (suppressedPlugins set)
    QStringList      effectiveLoadOrder;   // groundcover + suppressed dropped
    int              droppedOrphans = 0;   // count for status bar
};

// Run orphan-managed rescue, launcher-only-externals augment, orphan-plugin
// scrub, master-satisfaction pass, and the load-order filter. Pure on top of
// FS reads. See the per-block comments in the .cpp for the bug each step covers.
SyncPrepareResult prepareForSync(const SyncPrepareInputs &in);

// Decoded openmw.cfg bits we need for the "import existing setup" flow.
// Encounter order is preserved for all three lists: openmw loads data= and
// walks content=/groundcover= in declaration order, so the importer can feed
// these straight into m_loadOrder.
struct ImportEntries {
    QStringList dataPaths;        // unquoted, encounter-ordered
    QStringList contentFiles;     // plugin basenames as they appear
    QStringList groundcoverFiles; // groundcover plugin basenames
};

// Parse an openmw.cfg. Strips wrapping double quotes from data= paths (the
// launcher quotes paths with spaces). Comments and blank lines ignored,
// unrelated keys dropped. Pure: caller reads the file, this parses.
ImportEntries parseConfigEntries(const QString &cfgText);

// External (non-managed) data= dirs from openmw.cfg text: data= lines outside
// the "# --- Nerevarine Organizer BEGIN/END ---" managed block, quotes
// stripped, in encounter order. Kept here so the block markers + data= shape
// stay in one place; used to find the base game's Splash/ when offering to
// clear default splash screens. Pure: caller reads the file, this parses.
QStringList externalDataPaths(const QString &cfgText);

// True if `dirPath` looks like the vanilla Bethesda game-data folder (the one
// with Morrowind.esm and friends). The importer uses it to skip that path as a
// managed mod row: it's the base game, not a mod. Conservative: only fires when
// the base .esm files sit directly inside, so renamed mod folders don't trip it.
bool looksLikeVanillaDataFolder(const QString &dirPath);

} // namespace openmw

#endif // OPENMW_CONFIG_WRITER_H
