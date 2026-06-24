#include "mod_sharing.h"

#include <QDir>

#include <utility>

namespace mod_sharing {

QString cleanModPath(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
}

ModEntry makeSharedRow(const ModEntry &source, bool copyConfig)
{
    ModEntry e;
    e.itemType    = QStringLiteral("mod");
    // Keep install identity verbatim so both profiles point at the same
    // folder / Nexus page.
    e.modPath     = source.modPath;
    e.nexusUrl    = source.nexusUrl;
    e.nexusId     = source.nexusId;
    e.nexusTitle  = source.nexusTitle;
    e.customName  = source.customName;
    e.displayName = source.displayName;
    e.dateAdded   = source.dateAdded;
    e.videoUrl    = source.videoUrl;
    e.sourceUrl   = source.sourceUrl;
    e.installStatus = 1;            // files already exist on disk

    if (copyConfig) {
        e.checked      = source.checked;
        e.fomodChoices = source.fomodChoices;
        e.bainChoices  = source.bainChoices;
        e.annotation   = source.annotation;
        e.dependsOn    = source.dependsOn;
        e.isUtility    = source.isUtility;
        e.isFavorite   = source.isFavorite;
    } else {
        e.checked = false;          // start disabled at default config
    }
    // Tokens and transient conflict/dependency/missing-master fields stay at
    // their ModEntry defaults: a shared row is fresh and independent.
    return e;
}

int findExistingRow(const QList<ModEntry> &target, const ModEntry &shared)
{
    const QString wantPath = cleanModPath(shared.modPath);
    const QString wantUrl  = shared.nexusUrl;
    for (int i = 0; i < target.size(); ++i) {
        const ModEntry &e = target[i];
        if (!e.isMod()) continue;
        if (!wantPath.isEmpty() && cleanModPath(e.modPath) == wantPath) return i;
        if (!wantUrl.isEmpty()  && e.nexusUrl == wantUrl)               return i;
    }
    return -1;
}

AppendResult appendSharedRow(QList<ModEntry> target, const ModEntry &shared)
{
    AppendResult r;
    if (findExistingRow(target, shared) >= 0) {
        r.entries = std::move(target);
        r.added   = false;
        return r;
    }
    target.append(shared);
    r.entries = std::move(target);
    r.added   = true;
    return r;
}

bool pathReferencedIn(
    const QString &cleanPath,
    const QList<QPair<QString, QList<ModEntry>>> &profiles)
{
    if (cleanPath.isEmpty()) return false;
    for (const auto &pr : profiles) {
        for (const ModEntry &e : pr.second) {
            if (!e.isMod()) continue;
            if (cleanModPath(e.modPath) == cleanPath) return true;
        }
    }
    return false;
}

} // namespace mod_sharing
