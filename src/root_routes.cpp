#include "root_routes.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>

#include <algorithm>

namespace root_routes {
namespace {

bool isPakPart(const QString &name)
{
    return name.endsWith(QLatin1String(".pak"),  Qt::CaseInsensitive)
        || name.endsWith(QLatin1String(".ucas"), Qt::CaseInsensitive)
        || name.endsWith(QLatin1String(".utoc"), Qt::CaseInsensitive);
}

// The shallowest folder named OBSE inside the mod, or empty.
QString extenderFolder(const QString &modPath)
{
    QString best;
    QDirIterator it(modPath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString d = it.next();
        if (QFileInfo(d).fileName().compare(QLatin1String("OBSE"), Qt::CaseInsensitive) != 0)
            continue;
        if (best.isEmpty() || d.count(QLatin1Char('/')) < best.count(QLatin1Char('/')))
            best = d;
    }
    return best;
}

bool hasAnyExt(const QString &name, const QStringList &exts)
{
    for (const QString &e : exts)
        if (name.endsWith(e, Qt::CaseInsensitive)) return true;
    return false;
}

// Loose files of one kind, gathered to one folder of the game root. Files
// already sitting exactly at that folder are left alone: the overlay pass
// places those itself, and claiming them would deploy the same file twice.
// Anything deeper or shallower IS claimed, including a mod that wrapped the
// whole correct path in a folder of its own.
void looseRoute(const QString &label, const QString &modPath,
                const QStringList &exts, const QString &dest,
                QList<bethesda_deploy::DeploySource> &out,
                QStringList *claimedRels)
{
    if (dest.isEmpty()) return;
    const QDir base(modPath);
    QHash<QString, QStringList> byDir;
    QStringList dirs;   // first-seen order, sorted below for a stable result
    QDirIterator it(modPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo fi(it.next());
        if (!hasAnyExt(fi.fileName(), exts)) continue;
        const QString relDir = base.relativeFilePath(fi.absolutePath());
        if (relDir.compare(dest, Qt::CaseInsensitive) == 0) continue;
        const QString dir = fi.absolutePath();
        if (!byDir.contains(dir)) dirs << dir;
        byDir[dir] << fi.fileName();
        if (claimedRels) *claimedRels << base.relativeFilePath(fi.absoluteFilePath());
    }
    std::sort(dirs.begin(), dirs.end());
    for (const QString &dir : dirs) {
        QStringList names = byDir.value(dir);
        names.sort();
        out.append({label, dir, names, {}, dest});
    }
}

} // namespace

QList<bethesda_deploy::DeploySource> sourcesFor(const QString &label,
                                                const QString &modPath,
                                                const Routes &routes,
                                                QStringList *claimedRels)
{
    QList<bethesda_deploy::DeploySource> out;
    if (modPath.isEmpty() || !QDir(modPath).exists()) return out;

    if (!routes.pakModsSubdir.isEmpty()) {
        // Grouped by the folder they sit in, since a DeploySource names files
        // relative to one folder. Flattened into ~mods: where the mod kept
        // them says nothing the engine reads.
        QHash<QString, QStringList> byDir;
        QStringList dirs;   // first-seen order, so the result is stable
        QDirIterator it(modPath, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo fi(it.next());
            if (!isPakPart(fi.fileName())) continue;
            const QString dir = fi.absolutePath();
            if (!byDir.contains(dir)) dirs << dir;
            byDir[dir] << fi.fileName();
        }
        std::sort(dirs.begin(), dirs.end());
        const QString paksParent = QFileInfo(routes.pakModsSubdir).path();
        for (const QString &dir : dirs) {
            QStringList names = byDir.value(dir);
            names.sort();
            const bool logic = QFileInfo(dir).fileName()
                .compare(QLatin1String("LogicMods"), Qt::CaseInsensitive) == 0;
            out.append({label, dir, names, {},
                        logic ? paksParent + QStringLiteral("/LogicMods")
                              : routes.pakModsSubdir});
        }
    }

    if (!routes.extenderPluginsSubdir.isEmpty()) {
        const QString obse = extenderFolder(modPath);
        if (!obse.isEmpty())
            out.append({label, obse, {}, {}, routes.extenderPluginsSubdir});
    }

    looseRoute(label, modPath, {QStringLiteral(".archive"), QStringLiteral(".xl")},
               routes.looseArchiveSubdir, out, claimedRels);
    looseRoute(label, modPath, {QStringLiteral(".reds")},
               routes.looseScriptSubdir, out, claimedRels);
    return out;
}

bool onlyRoutedContent(const QString &modPath, const Routes &routes)
{
    if (routes.pakModsSubdir.isEmpty() && routes.extenderPluginsSubdir.isEmpty())
        return false;
    const QString obse = routes.extenderPluginsSubdir.isEmpty()
        ? QString() : extenderFolder(modPath);
    bool routed = false;
    QDirIterator it(modPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString name = QFileInfo(f).fileName();
        if (!routes.pakModsSubdir.isEmpty() && isPakPart(name)) { routed = true; continue; }
        if (!obse.isEmpty() && f.startsWith(obse + QLatin1Char('/'))) { routed = true; continue; }
        // Paperwork rides along with every kind of mod and decides nothing.
        const QString low = name.toLower();
        if (low.endsWith(QLatin1String(".txt")) || low.endsWith(QLatin1String(".md"))
            || low.endsWith(QLatin1String(".pdf")) || low.endsWith(QLatin1String(".png"))
            || low.endsWith(QLatin1String(".jpg")) || low.endsWith(QLatin1String(".url")))
            continue;
        return false;
    }
    return routed;
}

} // namespace root_routes
