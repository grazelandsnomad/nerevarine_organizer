#include "plugin_collisions.h"

#include <QMap>
#include <QSet>

namespace openmw {

namespace {

struct Bucket {
    QString originalBasename;  // first spelling seen, for display
    QList<PluginCollisionProvider> providers;
    QSet<QString> seenKeys;    // "modLabel|dataRoot" - dedup within a bucket
};

} // namespace

PluginCollisionReport findPluginBasenameCollisions(const QList<CollisionInput> &mods)
{
    // QMap so iteration order is alphabetical - deterministic output makes
    // both the Inspector dialog and the tests easy to reason about.
    QMap<QString, Bucket> byBasename;
    int total = 0;

    for (const CollisionInput &m : mods) {
        for (const auto &pair : m.pluginDirs) {
            const QString &dataRoot = pair.first;
            for (const QString &basename : pair.second) {
                if (basename.isEmpty()) continue;
                ++total;

                const QString key = basename.toLower();
                Bucket &b = byBasename[key];
                if (b.originalBasename.isEmpty())
                    b.originalBasename = basename;

                // Dedup exact (modLabel, dataRoot) repeats - those happen
                // only when the caller double-lists, not when there's a
                // real collision to surface.
                const QString providerKey = m.modLabel + QChar('|') + dataRoot;
                if (b.seenKeys.contains(providerKey)) continue;
                b.seenKeys.insert(providerKey);
                b.providers.append({m.modLabel, dataRoot});
            }
        }
    }

    PluginCollisionReport report;
    report.totalPluginsChecked = total;

    for (auto it = byBasename.constBegin(); it != byBasename.constEnd(); ++it) {
        if (it.value().providers.size() < 2) continue;
        PluginCollision c;
        c.basename  = it.value().originalBasename;
        c.providers = it.value().providers;
        report.collisions.append(c);
    }
    // QMap already iterates in lowercase-key order; collisions list is
    // therefore sorted case-insensitively by basename.
    return report;
}

} // namespace openmw
