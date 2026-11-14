#include "data_folder_choice.h"

#include <QDir>
#include <QFileInfo>

namespace data_folder_choice {

QString key(const QString &modPath, const QString &folderPath)
{
    const QString root = QDir::cleanPath(modPath);
    const QString full = QDir::cleanPath(folderPath);
    QString rel = QDir(root).relativeFilePath(full);
    // Outside the mod, or the mod folder itself: fall back to the name, which
    // is what the older key format used.
    if (rel.isEmpty() || rel == QStringLiteral(".") || rel.startsWith(QStringLiteral("..")))
        rel = QFileInfo(full).fileName();
    return root + QLatin1Char('\t') + rel;
}

QList<Folder> folders(const QString &modPath,
                      const QList<QPair<QString, QStringList>> &pluginDirs,
                      const QStringList &resourceRoots,
                      const QSet<QString> &declined)
{
    const QString root = QDir::cleanPath(modPath);
    QList<Folder> out;
    QSet<QString> seen;

    const auto add = [&](const QString &path, const QStringList &plugins) {
        const QString full = QDir::cleanPath(path);
        if (full.isEmpty() || seen.contains(full)) return;
        seen.insert(full);
        Folder f;
        f.path    = full;
        f.plugins = plugins;
        f.label   = QDir(root).relativeFilePath(full);
        if (f.label.isEmpty() || f.label.startsWith(QStringLiteral("..")))
            f.label = QFileInfo(full).fileName();
        f.registered = !declined.contains(key(modPath, full));
        out.append(f);
    };

    for (const auto &p : pluginDirs) add(p.first, p.second);
    // The game only hears about these when no folder of this mod holds a
    // plugin, which is how the config writer decides; listing them alongside
    // would offer a choice that changes nothing.
    if (pluginDirs.isEmpty())
        for (const QString &r : resourceRoots) add(r, {});
    return out;
}

} // namespace data_folder_choice
