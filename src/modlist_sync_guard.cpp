#include "modlist_sync_guard.h"

#include <QChar>

namespace openmw {

QString canonicalizePathText(const QString &path)
{
    QString s = path;
    // Unix separator only - openmw.cfg and modlist are Unix-style in this
    // project, and pulling in QDir::toNativeSeparators would canonicalise
    // AGAINST the host, which is the opposite of what a cross-machine
    // check wants.
    s.replace(QStringLiteral("\\"), QStringLiteral("/"));
    // Collapse "/./" runs to a single "/".  Keeps "." and ".." otherwise
    // untouched - interpreting those would require stat calls and we
    // explicitly don't touch the filesystem.
    while (s.contains(QStringLiteral("/./")))
        s.replace(QStringLiteral("/./"), QStringLiteral("/"));
    // Collapse duplicate slashes, but preserve a single leading "/" so
    // absolute paths stay absolute.
    while (s.contains(QStringLiteral("//")))
        s.replace(QStringLiteral("//"), QStringLiteral("/"));
    // Strip trailing separators except the literal root "/".
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

    // No canonical roots declared → nothing to measure drift against.  The
    // caller is expected to interpret an empty driftEntries as "guard
    // inactive" rather than "all good" - the canonicalRoots echo tells
    // them which state they're in.
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
                // The mod IS the root.  Pathological but not drift -
                // accept it so the caller isn't nagged about an edge
                // case they'd just have to suppress later.
                underCanonical = true;
                break;
            }
            // Require a '/' boundary after the root so "/home/x/mods"
            // doesn't spuriously accept "/home/x/mods_backup/Foo".
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
