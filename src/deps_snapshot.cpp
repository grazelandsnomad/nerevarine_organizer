#include "deps_snapshot.h"
#include "modlist_model_widget_bridge.h"
#include "modroles.h"

#include <QListWidget>

namespace deps {

QList<ModEntry> snapshot(const QList<::ModEntry> &rows)
{
    QList<ModEntry> out;
    out.reserve(rows.size());

    // The section a mod belongs to is the nearest separator above it - the list
    // has no other notion of grouping - so it is tracked while walking down.
    QString section;
    for (int i = 0; i < rows.size(); ++i) {
        const ::ModEntry &r = rows.at(i);
        ModEntry e;
        e.idx = i;
        if (r.isSeparator()) {
            section = r.displayName.trimmed();
        } else if (r.isMod()) {
            e.section     = section;
            e.nexusUrl    = r.nexusUrl;
            e.displayName = r.effectiveName().trimmed();
            e.dependsOn   = r.dependsOn;
            e.isUtility   = r.isUtility;
            e.enabled     = r.checked;
            e.installed   = (r.installStatus == int(InstallStatus::Installed));
        }
        out.append(e);
    }
    return out;
}

QList<ModEntry> snapshot(const QListWidget *list)
{
    return snapshot(modlist::snapshotEntries(list));
}

} // namespace deps
