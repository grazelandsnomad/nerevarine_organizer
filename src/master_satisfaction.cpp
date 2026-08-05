#include "master_satisfaction.h"

#include "pluginparser.h"

#include <QHash>

namespace openmw {

QSet<QString> findUnsatisfiedMasters(
    const QList<PluginRef> &plugins,
    QSet<QString>           availableLower)
{
    // Cache of masters per fullPath - cheap per call but avoids re-reading
    // the same plugin across fixpoint iterations on big modlists.
    QHash<QString, QStringList> mastersCache;
    auto mastersOf = [&](const PluginRef &pr) -> const QStringList& {
        auto it = mastersCache.constFind(pr.fullPath);
        if (it != mastersCache.constEnd()) return it.value();
        return *mastersCache.insert(pr.fullPath,
                                    plugins::readTes3Masters(pr.fullPath));
    };

    QSet<QString> unsatisfied;

    // Fixpoint loop: each pass may add plugins to `unsatisfied`, which
    // removes them from availableLower so their dependents are caught next.
    bool changed = true;
    while (changed) {
        changed = false;
        for (const PluginRef &pr : plugins) {
            if (unsatisfied.contains(pr.filename)) continue;
            for (const QString &m : mastersOf(pr)) {
                if (!availableLower.contains(m.toLower())) {
                    unsatisfied.insert(pr.filename);
                    availableLower.remove(pr.filename.toLower());
                    changed = true;
                    break;
                }
            }
        }
    }
    return unsatisfied;
}

} // namespace openmw
