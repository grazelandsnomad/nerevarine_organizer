#include "mod_match.h"
#include "mod_aliases.h"

#include <QRegularExpression>
#include <QSet>

namespace mod_match {

QStringList needlesFor(const QString &modName)
{
    static const QStringList kSeps{
        QStringLiteral(" - "), QStringLiteral(" ("), QStringLiteral("_")};
    QStringList needles;
    const QString n = modName.trimmed();
    // Three, not four: the scene's shorthand is routinely three letters (BOS,
    // MOP), and dropping those meant a mod installed under its acronym alone
    // could not be found at all. The start-anchoring below eight characters is
    // what keeps a short needle honest. Two is still too little to anchor.
    if (n.length() < 3) return needles;
    needles << n;
    if (n.contains(u'_')) {             // "OAAB_Data" -> "OAAB Data"
        QString sp = n;
        sp.replace(u'_', u' ');
        needles << sp;
    }
    int firstSep = -1;                  // earliest separator position
    for (const QString &sep : kSeps) {
        const int idx = n.indexOf(sep);
        if (idx >= 4 && (firstSep < 0 || idx < firstSep)) firstSep = idx;
    }
    if (firstSep >= 4) {
        const QString prefix = n.left(firstSep).trimmed();
        if (prefix.length() >= 4) needles << prefix;
    }
    return needles;
}

QString installedUnderAnyName(const QString &name, const QStringList &installed)
{
    for (const QString &cand : mod_aliases::expand({name})) {
        const bool shortNeedle = cand.length() < 8;
        const QString pat = shortNeedle
            ? (QLatin1String("^") + QRegularExpression::escape(cand) + QLatin1String("\\b"))
            : (QLatin1String("\\b") + QRegularExpression::escape(cand) + QLatin1String("\\b"));
        const QRegularExpression re(pat, QRegularExpression::CaseInsensitiveOption);
        for (const QString &mod : installed)
            if (re.match(mod).hasMatch()) return mod;
    }
    return {};
}

bool isConnector(const QString &w)
{
    static const QSet<QString> kJoin = {"and", "of", "the", "for", "a", "an", "in"};
    return kJoin.contains(w.toLower());
}

QString titleRun(const QString &phrase)
{
    const QStringList words = phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList run;
    for (int i = 0; i < words.size(); ++i) {
        const QString &w = words[i];
        if (w.isEmpty()) break;
        if (!w.front().isUpper()) {
            // A connector only stays if a title-cased word follows it.
            if (!isConnector(w)) break;
            if (i + 1 >= words.size() || words[i + 1].isEmpty()
                || !words[i + 1].front().isUpper()) break;
            run << w;
            continue;
        }
        run << w;
    }
    while (!run.isEmpty() && isConnector(run.last())) run.removeLast();
    return run.join(QLatin1Char(' '));
}

bool looksLikeModName(const QString &phrase)
{
    const QStringList words = phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() >= 2) return true;
    return words.size() == 1 && words[0].size() >= 3
        && words[0] == words[0].toUpper();
}

} // namespace mod_match
