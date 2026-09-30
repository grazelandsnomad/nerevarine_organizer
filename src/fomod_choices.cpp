#include "fomod_choices.h"

#include <QList>
#include <QStringList>
#include <QUrl>

namespace fomod_choices {
namespace {

bool inRange(const QList<FomodStep> &steps, int si, int gi, int pi)
{
    return si >= 0 && si < steps.size()
        && gi >= 0 && gi < steps[si].groups.size()
        && pi >= 0 && pi < steps[si].groups[gi].plugins.size();
}

// Authors re-case and re-space names between versions ("Glow in the Dahrk" /
// "Glow In The Dahrk") without meaning another option.
bool same(const QString &a, const QString &b)
{
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

QString pct(const QString &name)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(name));
}

QString unpct(const QString &encoded)
{
    return QUrl::fromPercentEncoding(encoded.toLatin1());
}

// First option named (step, group, option); with `anyStep`, the step name is
// not compared.
bool findByName(const QList<FomodStep> &steps, const QString &step,
                const QString &group, const QString &option, bool anyStep,
                quint64 *out)
{
    for (int si = 0; si < steps.size(); ++si) {
        if (!anyStep && !same(steps[si].name, step)) continue;
        for (int gi = 0; gi < steps[si].groups.size(); ++gi) {
            const FomodGroup &g = steps[si].groups[gi];
            if (!same(g.name, group)) continue;
            for (int pi = 0; pi < g.plugins.size(); ++pi)
                if (same(g.plugins[pi].name, option)) {
                    *out = key(si, gi, pi);
                    return true;
                }
        }
    }
    return false;
}

} // namespace

QString encode(const QList<FomodStep> &steps, const QSet<quint64> &checked)
{
    QStringList entries;
    for (int si = 0; si < steps.size(); ++si) {
        const FomodStep &step = steps[si];
        for (int gi = 0; gi < step.groups.size(); ++gi) {
            const FomodGroup &group = step.groups[gi];
            for (int pi = 0; pi < group.plugins.size(); ++pi) {
                if (!checked.contains(key(si, gi, pi))) continue;
                entries << QStringLiteral("%1:%2:%3:%4/%5/%6")
                               .arg(si).arg(gi).arg(pi)
                               .arg(pct(step.name), pct(group.name),
                                    pct(group.plugins[pi].name));
            }
        }
    }
    return entries.join(QLatin1Char(';'));
}

QSet<quint64> decode(const QList<FomodStep> &steps, const QString &stored)
{
    QSet<quint64> out;
    QList<quint64> legacy;
    bool legacyFits = true;

    for (const QString &rec : stored.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QStringList f = rec.split(QLatin1Char(':'));
        if (f.size() != 3 && f.size() != 4) continue;
        bool ok1 = false, ok2 = false, ok3 = false;
        const int si = f[0].toInt(&ok1), gi = f[1].toInt(&ok2), pi = f[2].toInt(&ok3);
        if (!ok1 || !ok2 || !ok3) continue;

        if (f.size() == 3) {
            if (inRange(steps, si, gi, pi)) legacy << key(si, gi, pi);
            else                             legacyFits = false;
            continue;
        }

        const QStringList names = f[3].split(QLatin1Char('/'));
        if (names.size() != 3) continue;
        const QString step = unpct(names[0]), group = unpct(names[1]),
                      option = unpct(names[2]);

        if (inRange(steps, si, gi, pi)
            && same(steps[si].name, step)
            && same(steps[si].groups[gi].name, group)
            && same(steps[si].groups[gi].plugins[pi].name, option)) {
            out.insert(key(si, gi, pi));
            continue;
        }
        quint64 found = 0;
        if (findByName(steps, step, group, option, /*anyStep=*/false, &found)
            || findByName(steps, step, group, option, /*anyStep=*/true, &found))
            out.insert(found);
    }

    if (legacyFits)
        for (quint64 k : std::as_const(legacy)) out.insert(k);
    return out;
}

} // namespace fomod_choices
