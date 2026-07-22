#include "mod_cleanup.h"

#include <QDir>
#include <QSet>

namespace mod_cleanup {

QStringList unreferencedFolders(const QString     &modsDir,
                                const QStringList &onDiskDirs,
                                const QStringList &referencedPaths)
{
    if (modsDir.isEmpty() || onDiskDirs.isEmpty()) return {};
    const QString root = QDir::cleanPath(modsDir);
    if (root.isEmpty()) return {};

    // Reduce every reference to the top-level folder under modsDir that it
    // lives in, so a row pointing deep inside a wrapper keeps the wrapper.
    QSet<QString> live;
    for (const QString &ref : referencedPaths) {
        if (ref.isEmpty()) continue;
        const QString clean = QDir::cleanPath(ref);
        if (!clean.startsWith(root + QLatin1Char('/'))) continue;
        const QString rel = clean.mid(root.length() + 1);
        const int slash = rel.indexOf(QLatin1Char('/'));
        live.insert(slash < 0 ? rel : rel.left(slash));
    }

    QStringList orphans;
    for (const QString &name : onDiskDirs) {
        if (name.isEmpty()) continue;
        if (!live.contains(name)) orphans << name;
    }
    return orphans;
}

} // namespace mod_cleanup
