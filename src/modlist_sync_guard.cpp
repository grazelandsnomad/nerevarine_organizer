#include "modlist_sync_guard.h"

#include <QChar>

namespace openmw {

QString canonicalizePathText(const QString &path)
{
    QString s = path;
    // Unix separators only. toNativeSeparators would normalize against the
    // host, wrong for a cross-machine check.
    s.replace(QStringLiteral("\\"), QStringLiteral("/"));
    // Collapse "/./". Leave "." and ".." alone - resolving them needs stat,
    // and we never touch the filesystem.
    while (s.contains(QStringLiteral("/./")))
        s.replace(QStringLiteral("/./"), QStringLiteral("/"));
    // Collapse "//" but keep a single leading "/" so absolute stays absolute.
    while (s.contains(QStringLiteral("//")))
        s.replace(QStringLiteral("//"), QStringLiteral("/"));
    // Strip trailing separators except root "/".
    while (s.size() > 1 && s.endsWith('/'))
        s.chop(1);
    return s;
}

ModlistSyncReport findModlistPathDrift(
    const QList<SyncGuardInput> &mods,
    const QStringList           &canonicalRoots)
{
    ModlistSyncReport report;
    report.totalModsChecked = mods.size();
    for (const QString &r : canonicalRoots) {
        const QString cr = canonicalizePathText(r);
        if (!cr.isEmpty()) report.canonicalRoots << cr;
    }

    // No roots → can't measure drift. Empty driftEntries means "guard
    // inactive", not "all good"; the canonicalRoots echo lets the caller tell
    // the two apart.
    if (report.canonicalRoots.isEmpty())
        return report;

    for (const SyncGuardInput &m : mods) {
        if (m.modPath.isEmpty()) {
            report.driftEntries.append({m.modLabel, m.modPath,
                QStringLiteral("path is empty")});
            continue;
        }

        const QString mp = canonicalizePathText(m.modPath);
        bool underCanonical = false;
        for (const QString &root : report.canonicalRoots) {
            if (mp == root) {
                // Mod path equals the root: odd but not drift, accept it.
                underCanonical = true;
                break;
            }
            // '/' boundary after root so "/home/x/mods" doesn't accept
            // "/home/x/mods_backup/Foo".
            const QString rootWithSep = root + QChar('/');
            if (mp.startsWith(rootWithSep)) {
                underCanonical = true;
                break;
            }
        }
        if (!underCanonical) {
            report.driftEntries.append({m.modLabel, m.modPath,
                QStringLiteral("not under canonical root")});
        }
    }

    return report;
}

} // namespace openmw
