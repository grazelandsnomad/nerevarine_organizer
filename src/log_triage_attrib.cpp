#include "log_triage_attrib.h"

#include <QFile>
#include <QSet>

namespace openmw {

namespace {

// Record ids are stored in the plugin with the author's casing, which is also
// what OpenMW prints. Matching both that and the lowered form covers the
// engine's own case-folding without lowering the haystack, which would double
// the memory a chunk costs.
QList<QByteArray> needlesFor(const QString &id)
{
    QList<QByteArray> out;
    const QByteArray asIs = id.toLatin1();
    out << asIs;
    const QByteArray lowered = id.toLower().toLatin1();
    if (lowered != asIs) out << lowered;
    return out;
}

} // namespace

QHash<QString, QString> modsMentioningIds(const QList<AttribSource> &sources,
                                          const QStringList         &ids,
                                          qint64                     maxBytes)
{
    QHash<QString, QString> owner;
    if (ids.isEmpty() || sources.isEmpty()) return owner;

    // id -> what to look for, kept alongside the lowered key it answers to.
    QList<QPair<QString, QList<QByteArray>>> wanted;
    QSet<QString> keys;
    for (const QString &id : ids) {
        const QString key = id.toLower();
        if (id.isEmpty() || keys.contains(key)) continue;
        keys.insert(key);
        wanted.append({key, needlesFor(id)});
    }

    // The longest id decides the overlap: a match straddling two chunks must
    // still be seen, so each chunk keeps the tail of the one before it.
    int longest = 0;
    for (const auto &w : wanted)
        for (const QByteArray &n : w.second) longest = qMax(longest, int(n.size()));
    const qint64 kChunk = 4 << 20;

    qint64 budget = maxBytes;
    for (const AttribSource &src : sources) {
        if (owner.size() == wanted.size() || budget <= 0) break;
        for (const QString &path : src.pluginPaths) {
            if (owner.size() == wanted.size() || budget <= 0) break;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) continue;

            QByteArray tail;
            while (!f.atEnd() && budget > 0) {
                const QByteArray chunk = f.read(kChunk);
                if (chunk.isEmpty()) break;
                budget -= chunk.size();
                const QByteArray hay = tail + chunk;
                for (const auto &w : wanted) {
                    if (owner.contains(w.first)) continue;
                    for (const QByteArray &needle : w.second)
                        if (hay.contains(needle)) {
                            owner.insert(w.first, src.modName);
                            break;
                        }
                }
                if (owner.size() == wanted.size()) break;
                tail = longest > 1 ? hay.right(longest - 1) : QByteArray();
            }
            f.close();
        }
    }
    return owner;
}

} // namespace openmw
