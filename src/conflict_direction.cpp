#include "conflict_direction.h"

#include <algorithm>

namespace conflict_direction {

namespace {

QStringList capFiles(QStringList files, int maxShown)
{
    files.sort(Qt::CaseInsensitive);
    if (maxShown <= 0 || files.size() <= maxShown) return files;
    const int hidden = files.size() - maxShown;
    QStringList out = files.mid(0, maxShown);
    out << QStringLiteral("+%1 more").arg(hidden);
    return out;
}

} // namespace

QHash<QString, Directions> resolve(const QList<Mod> &mods,
                                   const QHash<QString, QList<int>> &owners,
                                   int maxShownFiles)
{
    // Shared files per ordered pair, keyed (lower index << 32 | higher index).
    // Sorting the owners of each file is what makes the pair ordered, and the
    // higher index is by definition the winner.
    QHash<quint64, QStringList> pairFiles;
    for (auto it = owners.constBegin(); it != owners.constEnd(); ++it) {
        QList<int> idx = it.value();
        std::sort(idx.begin(), idx.end());
        idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
        if (idx.size() < 2) continue;
        for (int i = 0; i < idx.size(); ++i) {
            if (idx[i] < 0 || idx[i] >= mods.size()) continue;
            for (int j = i + 1; j < idx.size(); ++j) {
                if (idx[j] >= mods.size()) break;
                pairFiles[(quint64(idx[i]) << 32) | quint64(idx[j])].append(it.key());
            }
        }
    }

    QHash<QString, Directions> out;
    for (auto it = pairFiles.constBegin(); it != pairFiles.constEnd(); ++it) {
        const int above = int(it.key() >> 32);            // earlier, loses
        const int below = int(it.key() & 0xffffffffULL);  // later, wins
        const QStringList files = capFiles(it.value(), maxShownFiles);

        out[mods[below].path].overwrites
            .append({mods[above].path, mods[above].name, files});
        out[mods[above].path].overwrittenBy
            .append({mods[below].path, mods[below].name, files});
    }

    auto byName = [](const Counterpart &a, const Counterpart &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    };
    for (Directions &d : out) {
        std::sort(d.overwrites.begin(),    d.overwrites.end(),    byName);
        std::sort(d.overwrittenBy.begin(), d.overwrittenBy.end(), byName);
    }
    return out;
}

} // namespace conflict_direction
