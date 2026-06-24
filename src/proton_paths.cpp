#include "proton_paths.h"

#include <QDir>

namespace proton {

QString prefixUserDir(const QString &compatdataRoot, const QString &appId)
{
    if (compatdataRoot.isEmpty() || appId.isEmpty()) return {};
    return QDir::cleanPath(compatdataRoot + "/" + appId
                           + "/pfx/drive_c/users/steamuser");
}

QString localAppData(const QString &prefixUserDir, const QString &folder)
{
    if (prefixUserDir.isEmpty()) return {};
    QString p = prefixUserDir + "/AppData/Local";
    if (!folder.isEmpty()) p += "/" + folder;
    return QDir::cleanPath(p);
}

QStringList myGamesDirs(const QString &prefixUserDir, const QString &folder)
{
    if (prefixUserDir.isEmpty()) return {};
    // Newer Proton uses "Documents/My Games"; older prefixes used the XP-era
    // "My Documents/My Games". Probe both.
    const QStringList bases = {
        prefixUserDir + "/Documents/My Games",
        prefixUserDir + "/My Documents/My Games",
    };
    QStringList out;
    for (const QString &b : bases) {
        const QString p = folder.isEmpty() ? b : b + "/" + folder;
        out << QDir::cleanPath(p);
    }
    return out;
}

QStringList compatdataRootsFromCommon(const QStringList &commonRoots)
{
    static const QString kSuffix = QStringLiteral("/steamapps/common");
    QStringList out;
    for (const QString &c : commonRoots) {
        if (!c.endsWith(kSuffix)) continue;             // unexpected shape
        const QString lib = c.left(c.size() - kSuffix.size());
        const QString cd = QDir::cleanPath(lib + "/steamapps/compatdata");
        if (!out.contains(cd)) out << cd;
    }
    return out;
}

} // namespace proton
