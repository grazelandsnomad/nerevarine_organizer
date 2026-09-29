#ifndef ROOT_ROUTES_H
#define ROOT_ROUTES_H

// root_routes - the parts of a mod that belong outside Data/.
//
// Deploy overlays a mod's data root onto Data/, and that is the whole story
// for a Bethesda game whose engine reads nothing else. Oblivion Remastered is
// an Unreal 5 game with Bethesda data inside it, and two kinds of mod for it
// load from elsewhere:
//
//   - Unreal .pak mods (.pak + .ucas + .utoc) load only from
//     Content/Paks/~mods. Mods ship them loose, in ~mods/, or in the full
//     OblivionRemastered/Content/Paks/~mods/ path; all three land the same.
//     Blueprint mods for UE4SS go in Paks/LogicMods instead, and say so by
//     shipping that folder.
//   - OBSE64 plugins load from Binaries/Win64/OBSE/Plugins, not
//     Data/OBSE/Plugins as SKSE's and F4SE's do.
//
// Each becomes a DeploySource for the game-root pass (its own manifest, so
// undeploy takes it back out). Qt Core only; tested off a QTemporaryDir.

#include "bethesda_deploy.h"

#include <QList>
#include <QString>

namespace root_routes {

struct Routes {
    QString pakModsSubdir;          // "" = no pak mods for this game
    QString extenderPluginsSubdir;  // "" = the extender folder stays in Data/
};

// What of `modPath` goes where, relative to the game root.
QList<bethesda_deploy::DeploySource> sourcesFor(const QString &label,
                                                const QString &modPath,
                                                const Routes &routes);

// True when the mod holds nothing BUT routed content (no plugin, no archive,
// no loose asset outside a routed folder) - so the Data/ pass must not fall
// back to deploying the whole folder into Data/.
bool onlyRoutedContent(const QString &modPath, const Routes &routes);

} // namespace root_routes

#endif // ROOT_ROUTES_H
