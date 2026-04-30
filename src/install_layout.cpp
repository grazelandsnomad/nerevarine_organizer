#include "install_layout.h"

#include <QSet>

namespace install_layout {

QString diveTarget(const QStringList &topSubdirs,
                   const QStringList &topFiles)
{
    if (topSubdirs.size() != 1) return {};
    if (!topFiles.isEmpty())    return {};

    // Folder names that ARE the data root, not a wrapper around one.
    // Mirrors collectResourceFolders' kAssetDirs plus the FOMOD installer
    // directory.  Kept in lowercase; caller normalises before lookup.
    static const QSet<QString> kDataRootNames {
        // OpenMW asset directories - must live directly under data=.
        "textures", "meshes", "splash", "fonts", "sound", "music",
        "icons", "bookart", "mwscript", "video", "shaders", "scripts",
        "grass", "lod", "distantland",
        // FOMOD installer config - must live directly under modPath so
        // FomodWizard::hasFomod() finds <modPath>/fomod/ModuleConfig.xml
        // (diving would push it to <modPath>/fomod/fomod/… and the
        // wizard would silently skip).
        "fomod",
    };

    if (kDataRootNames.contains(topSubdirs.first().toLower()))
        return {};

    return topSubdirs.first();
}

} // namespace install_layout
