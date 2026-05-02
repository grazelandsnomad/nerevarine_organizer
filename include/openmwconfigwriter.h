#ifndef OPENMW_CONFIG_WRITER_H
#define OPENMW_CONFIG_WRITER_H

// OpenMWConfigWriter - pure renderer for openmw.cfg, extracted from
// MainWindow::syncOpenMWConfig.  No Qt widgets, no filesystem I/O, no
// MainWindow state.  Inputs are plain structs; output is a QString that
// callers write to disk.  Pure function → golden-file testable.
//
// Two regressions motivated this extraction and should stay covered by
// tests/test_openmw_config_writer.cpp:
//   1. Stale load order clobbering launcher changes after a mod reorder.
//   2. Missing data= lines for pure resource mods (no plugins, only
//      textures/ meshes/ etc.).

#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

namespace openmw {

// One mod entry in modlist order, with its plugin layout already resolved
// from the filesystem by the caller.  Keeping the writer free of FS I/O is
// what makes golden-file tests trivial: construct these structs directly.
struct ConfigMod {
    bool enabled   = false;
    bool installed = false;

    // Subdirectories of the mod that hold plugins, paired with the plugin
    // filenames found there.  Typically produced by plugins::collectDataFolders.
    QList<QPair<QString, QStringList>> pluginDirs;

    // Fallback roots for pure resource mods (textures/meshes/sound packs).
    // Only consulted when pluginDirs is empty AND the mod is enabled+installed.
    // Typically produced by plugins::collectResourceFolders, with modPath as
    // a last-ditch fallback if that turns up nothing.
    QStringList resourceRoots;

    // Plugin filenames (basenames) that should be emitted as groundcover=
    // instead of content=.  OpenMW renders these as instanced grass/flora
    // with no collision; listing them as content= makes them inert statics.
    QSet<QString> groundcoverFiles;

    // Plugins to exclude from BOTH content= and groundcover= output.
    // Used for groundcover plugins whose masters aren't satisfied - emitting
    // them as content= produces untextured geometry, and as groundcover=
    // causes a fatal crash.
    QSet<QString> suppressedPlugins;

    // BSA basenames found anywhere under this mod's data roots.  Each one
    // becomes a `fallback-archive=<name>` line in the managed section so
    // OpenMW can resolve textures/meshes packed inside.  Without this,
    // BSA-only mods (Authentic Signs IT was the canonical bug report)
    // load their .esp references but render with [None] texture binds
    // because the fallback-archive lookup never sees the file.  Only
    // consulted when the mod is enabled+installed.
    QStringList bsaFiles;
};

// Render the new openmw.cfg contents.
//   mods         modlist order (top → bottom of the UI); determines insertion
//                order for newly-seen content and supplies data= paths.
//   loadOrder    authoritative order for managed plugins.  External / base-game
//                plugins keep their encounter order from existingCfg.
//   existingCfg  current openmw.cfg contents (empty string if absent).
// Returns the full text to write back to openmw.cfg, terminated with a newline.
QString renderOpenMWConfig(const QList<ConfigMod> &mods,
                           const QStringList   &loadOrder,
                           const QString       &existingCfg);

// Rewrite the current profile's data=/content= lines in the OpenMW Launcher's
// own state file (~/.config/openmw/launcher.cfg).  Without this the launcher's
// Data Files tab keeps displaying uninstalled mods, because it reads from its
// per-profile cache rather than from openmw.cfg every time it opens.
//
//   existingLauncherCfg  current file contents.  Empty string → returns {} so
//                        the caller treats it as "no file yet, nothing to do"
//                        (fresh install, launcher never run).
//   dataPaths            absolute paths (unquoted) in load-priority order,
//                        same set we emit as `data=` lines in openmw.cfg -
//                        base-game root included.
//   contentFiles         plugin basenames in activation order, matching the
//                        final `content=` order of the rendered openmw.cfg.
//
// Returns the rewritten file text, or {} when there is no `[Profiles]`
// section or no `currentprofile=` key (both no-ops - don't clobber a fresh
// or structurally unexpected file).  Everything outside the current profile's
// data=/content= block - other sections, other profiles, `fallback-archive=`
// entries, comments, blank lines - is preserved byte-for-byte.
QString renderLauncherCfg(const QString     &existingLauncherCfg,
                          const QStringList &dataPaths,
                          const QStringList &contentFiles);

// Extract the current profile's `content=` filenames, in the order the
// launcher wrote them.  This is what powers absorbExternalLoadOrder's
// "user reordered in the launcher's Data Files tab" recovery: the
// launcher updates its per-profile content= list in launcher.cfg on
// every reorder - even when the user closes the launcher without
// clicking Save/Play (so openmw.cfg stays on the old order).  Without
// reading THIS signal, nerevarine's next sync rewrites openmw.cfg from
// a stale m_loadOrder and silently clobbers the reorder.
//
// Returns an empty list when there's no `[Profiles]` section, no
// `currentprofile=` key, or that profile has no `<ts>/content=` lines.
QStringList readLauncherCfgContentOrder(const QString &existingLauncherCfg);

// Extract the current profile's `data=` paths in encounter order.  Mirror
// of readLauncherCfgContentOrder for the data= side, used by syncOpenMWConfig
// to discover paths the user added directly through the OpenMW Launcher
// (vanilla Morrowind / Tribunal / Bloodmoon Data Files most often) so they
// can be preserved when openmw.cfg / launcher.cfg are rewritten.  Without
// this, a launcher-side-only data= path was treated as orphaned and the
// next sync wiped its content= companions out of launcher.cfg, leaving the
// Content List empty except for Nerevarine-managed mods.
//
// Paths are returned UNQUOTED (the launcher writes them that way on disk).
// Returns an empty list when there's no `[Profiles]` section, no
// `currentprofile=` key, or that profile has no `<ts>/data=` lines.
QStringList readLauncherCfgDataPaths(const QString &existingLauncherCfg);

} // namespace openmw

#endif // OPENMW_CONFIG_WRITER_H
