#include "asset_collisions.h"

#include <QMap>
#include <QSet>

namespace openmw {

AssetCollisionReport findAssetCaseCollisions(const QList<AssetCaseInput> &inputs)
{
    AssetCollisionReport report;

    for (const AssetCaseInput &in : inputs) {
        QMap<QString, QStringList> byLower; // lowercased rel → actual spellings

        for (const QString &rel : in.relPaths) {
            if (rel.isEmpty()) continue;
            ++report.totalFilesChecked;
            byLower[rel.toLower()] << rel;
        }

        AssetCaseModReport modRep;
        modRep.modLabel = in.modLabel;
        modRep.dataRoot = in.dataRoot;

        for (auto it = byLower.constBegin(); it != byLower.constEnd(); ++it) {
            // Deduplicate exact-same spellings before counting; a caller that
            // double-passes the same path must not trigger a false positive.
            const QSet<QString> unique(it.value().cbegin(), it.value().cend());
            if (unique.size() < 2) continue;
            AssetCaseHit hit;
            hit.lowercasedRel = it.key();
            hit.spellings     = QStringList(unique.cbegin(), unique.cend());
            hit.spellings.sort(Qt::CaseInsensitive);
            modRep.hits.append(hit);
        }

        if (!modRep.hits.isEmpty())
            report.mods.append(modRep);
    }

    return report;
}

} // namespace openmw
