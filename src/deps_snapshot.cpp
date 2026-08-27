#include "deps_snapshot.h"
#include "modroles.h"

#include <QListWidget>
#include <QListWidgetItem>

namespace deps {

QList<ModEntry> snapshot(const QListWidget *list)
{
    QList<ModEntry> out;
    if (!list) return out;
    out.reserve(list->count());

    // The section a mod belongs to is the nearest separator above it - the list
    // has no other notion of grouping - so it is tracked while walking down.
    QString section;
    for (int i = 0; i < list->count(); ++i) {
        const QListWidgetItem *it = list->item(i);
        ModEntry e;
        e.idx = i;
        const QString type = it->data(ModRole::ItemType).toString();
        if (type == ItemType::Separator) {
            section = it->text().trimmed();
        } else if (type == ItemType::Mod) {
            e.section     = section;
            e.nexusUrl    = it->data(ModRole::NexusUrl).toString();
            e.displayName = it->data(ModRole::CustomName).toString();
            if (e.displayName.isEmpty()) e.displayName = it->text().trimmed();
            e.dependsOn   = it->data(ModRole::DependsOn).toStringList();
            e.isUtility   = it->data(ModRole::IsUtility).toBool();
            e.enabled     = (it->checkState() == Qt::Checked);
            e.installed   = (it->data(ModRole::InstallStatus).toInt() == 1);
        }
        out.append(e);
    }
    return out;
}

} // namespace deps
