#include "bethesda_loadorder.h"

#include <QSet>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace bethesda_loadorder {

static bool isMaster(const QString &p)
{
    return p.endsWith(QLatin1String(".esm"), Qt::CaseInsensitive)
        || p.endsWith(QLatin1String(".esl"), Qt::CaseInsensitive);
}

QStringList mastersFirst(const QStringList &plugins)
{
    QStringList masters, regular;
    for (const QString &p : plugins) {
        if (isMaster(p)) masters << p;
        else            regular << p;
    }
    return masters + regular;
}

Settled settle(const QStringList &active,
               const QHash<QString, QStringList> &mastersOf,
               const QSet<QString> &presentLower)
{
    // Held: a master in no file, or held itself - to a fixed point, since
    // holding one can strand another that needs it.
    QSet<QString> held;   // lower-cased
    for (bool changed = true; changed;) {
        changed = false;
        for (const QString &p : active) {
            const QString lp = p.toLower();
            if (held.contains(lp)) continue;
            for (const QString &m : mastersOf.value(lp)) {
                const QString lm = m.toLower();
                if (!presentLower.contains(lm) || held.contains(lm)) {
                    held.insert(lp);
                    changed = true;
                    break;
                }
            }
        }
    }

    // Why, against the final set: a plugin held early for a missing file
    // may also need one held later, and the summary tells the two apart.
    Settled out;
    QHash<QString, QString> kept;   // lower-cased -> as listed
    for (const QString &p : active) {
        const QString lp = p.toLower();
        if (!held.contains(lp)) {
            kept.insert(lp, p);
            continue;
        }
        Held h{p, {}, {}};
        for (const QString &m : mastersOf.value(lp)) {
            const QString lm = m.toLower();
            if (!presentLower.contains(lm)) h.missing << m;
            else if (held.contains(lm))     h.heldMasters << m;
        }
        out.held.append(std::move(h));
    }

    // Each kept plugin after its masters. Depth-first, so a master's own
    // masters go first too; `onPath` stops a cycle (a broken pair of files
    // naming each other) from recursing forever without dropping either.
    QSet<QString> placed, onPath;
    const auto place = [&](const auto &self, const QString &lp) -> void {
        if (placed.contains(lp) || onPath.contains(lp)) return;
        onPath.insert(lp);
        for (const QString &m : mastersOf.value(lp)) {
            const QString lm = m.toLower();
            if (kept.contains(lm)) self(self, lm);
        }
        onPath.remove(lp);
        placed.insert(lp);
        out.order << kept.value(lp);
    };
    for (const QString &p : active) {
        const QString lp = p.toLower();
        if (kept.contains(lp)) place(place, lp);
    }
    return out;
}

QList<MissingMaster> missingMasters(const QList<Held> &held)
{
    QHash<QString, const Held *> byName;   // lower-cased plugin
    for (const Held &h : held) byName.insert(h.plugin.toLower(), &h);

    QList<MissingMaster> out;
    QHash<QString, int> at;                // lower-cased file -> index in out
    for (const Held &h : held) {
        // The missing files this plugin comes down to, through every master
        // held for it. Breadth-first with a seen-set: no memo, because a
        // pair of held plugins naming each other would cache a half answer.
        QStringList files;
        QSet<QString> seenFiles;
        QSet<QString> seenPlugins{h.plugin.toLower()};
        QList<const Held *> queue{&h};
        while (!queue.isEmpty()) {
            const Held *cur = queue.takeFirst();
            for (const QString &m : cur->missing) {
                const QString lm = m.toLower();
                if (seenFiles.contains(lm)) continue;
                seenFiles.insert(lm);
                files << m;
            }
            for (const QString &m : cur->heldMasters) {
                const QString lm = m.toLower();
                if (seenPlugins.contains(lm)) continue;
                seenPlugins.insert(lm);
                if (const Held *next = byName.value(lm)) queue << next;
            }
        }
        for (const QString &f : std::as_const(files)) {
            const QString lf = f.toLower();
            auto it = at.constFind(lf);
            if (it == at.cend()) {
                it = at.insert(lf, int(out.size()));
                out.append({f, {}});
            }
            out[it.value()].neededBy << h.plugin;
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const MissingMaster &a, const MissingMaster &b) {
        if (a.neededBy.size() != b.neededBy.size()) return a.neededBy.size() > b.neededBy.size();
        return a.master.compare(b.master, Qt::CaseInsensitive) < 0;
    });
    return out;
}

QString pluginsTxtContent(const QStringList &activeInOrder)
{
    QString out;
    for (const QString &p : activeInOrder)
        out += p + QLatin1String("\r\n");
    return out;
}

QString asteriskPluginsTxtContent(const QStringList &activeInOrder)
{
    QString out;
    for (const QString &p : activeInOrder)
        out += QLatin1Char('*') + p + QLatin1String("\r\n");
    return out;
}

QString mergedPluginsTxtContent(const QString &original, const QStringList &modsInOrder)
{
    QStringList lines;
    QSet<QString> listed;
    for (const QString &raw : original.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        if (listed.contains(line.toLower())) continue;
        lines << line;
        listed.insert(line.toLower());
    }
    for (const QString &p : mastersFirst(modsInOrder)) {
        if (listed.contains(p.toLower())) continue;
        lines << p;
        listed.insert(p.toLower());
    }
    return pluginsTxtContent(lines);
}

StampResult applyTimestampOrder(const QString &dataDir,
                                const QStringList &pluginsInOrder,
                                qint64 baseEpochMs, qint64 stepMs)
{
    StampResult r;
    const QDir data(dataDir);
    for (int i = 0; i < pluginsInOrder.size(); ++i) {
        const QString path = data.filePath(pluginsInOrder[i]);
        QFile f(path);
        if (!f.exists()) { r.errors << pluginsInOrder[i]; continue; }

        const QDateTime when =
            QDateTime::fromMSecsSinceEpoch(baseEpochMs + qint64(i) * stepMs);

        // setFileTime needs the file open; a read-only handle suffices to touch
        // mtime on a file we own. For a hardlinked deploy this also bumps the
        // source's mtime (same inode), which is harmless.
        const bool opened = f.open(QIODevice::ReadWrite) || f.open(QIODevice::ReadOnly);
        if (opened && f.setFileTime(when, QFileDevice::FileModificationTime))
            ++r.stamped;
        else
            r.errors << pluginsInOrder[i];
        if (f.isOpen()) f.close();
    }
    return r;
}

} // namespace bethesda_loadorder
