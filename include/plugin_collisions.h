#pragma once

// plugin_collisions - pure detector for "same plugin filename shipped by more
// than one enabled+installed mod".  Surfaces the class of bug where
// Rocky_WG_Base_1.1.esp lives in three mods at once (base mod + a couple of
// Caldera Priory FOMOD patch subfolders), OpenMW's VFS silently picks
// whichever data= path comes last, and the user has no way to tell why the
// in-game ESP isn't behaving like the one they thought they enabled.
//
// No Qt Widgets, no filesystem I/O, no MainWindow state - same pattern as
// openmwconfigwriter.h.  The caller walks m_modList once, packs the
// per-mod plugin layout into CollisionInput, and feeds it in.  Comparison
// is case-insensitive ("Foo.esp" collides with "foo.ESP") because OpenMW's
// VFS folds case on Linux and Windows filesystems are already insensitive.

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace openmw {

struct CollisionInput {
    QString modLabel;   // display name of the mod in the modlist
    // Same shape as ConfigMod::pluginDirs: { (dataRoot, [basename, ...]), ... }
    QList<QPair<QString, QStringList>> pluginDirs;
};

struct PluginCollisionProvider {
    QString modLabel;   // owning mod, as the caller labelled it
    QString dataRoot;   // absolute path of the data= folder the plugin lives in
};

struct PluginCollision {
    QString basename;   // case-preserved (first-seen spelling) for display
    QList<PluginCollisionProvider> providers;  // size() >= 2 by construction
};

struct PluginCollisionReport {
    QList<PluginCollision> collisions;  // sorted by basename for deterministic output
    int totalPluginsChecked = 0;
};

// Walk `mods`, bucket plugin basenames case-insensitively, and return every
// basename that showed up in more than one (modLabel, dataRoot) combination.
// Same mod appearing under the same dataRoot twice (pathological caller
// input) is de-duplicated; same mod under TWO different dataRoots is
// reported - that's a real within-mod FOMOD-extract bug we want surfaced.
PluginCollisionReport findPluginBasenameCollisions(const QList<CollisionInput> &mods);

} // namespace openmw
