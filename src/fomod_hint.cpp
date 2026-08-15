#include "fomod_hint.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace fomod {

bool asksAboutAnotherMod(const QString &stepName, const QString &groupName)
{
    // ModuleConfig.xml text carries the author's line breaks and indentation, so
    // fold whitespace first: "Do you\n    use X?" is the same question.
    const QString question =
        (stepName + u' ' + groupName).simplified().toLower();

    // Phrases that can only be asking after something the user already has.
    // Deliberately short of "patch", "addon" and "compatibility": plenty of
    // options patch the mod's own files, and a wrong "not in your modlist" is
    // worse than no hint at all.
    static const QStringList kSetupPhrases = {
        QStringLiteral("do you have"),
        QStringLiteral("do you use"),
        QStringLiteral("do you run"),
        QStringLiteral("do you own"),
        QStringLiteral("are you using"),
        QStringLiteral("are using"),        // "if you are using X"
        QStringLiteral("if you have"),
        QStringLiteral("if you use"),
        QStringLiteral("if using"),
        QStringLiteral("users of"),
        QStringLiteral("already have"),
        QStringLiteral("already installed"),
        QStringLiteral("in your load order"),
        QStringLiteral("in your modlist"),
        QStringLiteral("in your mod list"),
    };
    for (const QString &phrase : kSetupPhrases)
        if (question.contains(phrase)) return true;

    // "Is X installed?" - the mod name sits in the middle, where no fixed phrase
    // can reach it, so match the end of the clause instead. Guarded against
    // "files to be installed", which describes what this installer is about to
    // do rather than what the user already runs.
    static const QRegularExpression kAlreadyInstalled(
        QStringLiteral("\\binstalled\\b\\s*(?:[?.!,]|$)"));
    static const QRegularExpression kAboutToInstall(
        QStringLiteral("\\b(?:be|been|being|get|gets|getting)\\s+installed\\b"));
    return kAlreadyInstalled.match(question).hasMatch()
        && !kAboutToInstall.match(question).hasMatch();
}

QList<NexusModRef> citedMods(const QString &text)
{
    QList<NexusModRef> out;
    if (text.isEmpty()) return out;

    // Pull out candidate URLs and hand each to the shared parser rather than
    // re-deriving the game/modId split here (nxmurl.h is the one home for it).
    // Trailing ")" and "." are stripped by the character class: descriptions
    // wrap these in parentheses and end sentences on them.
    // Delimited raw string: the pattern contains )" , which would end a plain
    // R"(...)" early and silently truncate the character class.
    static const QRegularExpression kUrl(
        QStringLiteral(R"RX((?:https?://)?(?:www\.)?nexusmods\.com/[^\s)\]<>"']+)RX"),
        QRegularExpression::CaseInsensitiveOption);

    auto it = kUrl.globalMatch(text);
    while (it.hasNext()) {
        QString url = it.next().captured();
        // Sentence punctuation that survived the character class.
        while (!url.isEmpty()
               && (url.endsWith(u'.') || url.endsWith(u',') || url.endsWith(u';')))
            url.chop(1);

        // Descriptions often drop the scheme ("see www.nexusmods.com/..."),
        // and without one QUrl reads the host as the first path segment. Add
        // it here rather than loosening the shared parser, which reads stored
        // modlist URLs that always carry one.
        if (!url.contains(QLatin1String("://")))
            url.prepend(QLatin1String("https://"));

        const auto ref = parseNexusModUrl(url);
        if (!ref) continue;
        bool dup = false;
        for (const NexusModRef &seen : std::as_const(out)) {
            if (seen.modId == ref->modId
                && seen.game.compare(ref->game, Qt::CaseInsensitive) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup) out.append(*ref);
    }
    return out;
}

QString missingModLabel(const QStringList &optionNames, const QString &groupName)
{
    // Longest common prefix of the option names, cut back to a word boundary
    // so "Ashfall" + "Ashfall (HD)" gives "Ashfall" rather than "Ashfall (".
    QString prefix;
    bool first = true;
    for (const QString &name : optionNames) {
        const QString trimmed = name.trimmed();
        if (trimmed.isEmpty()) continue;
        if (first) { prefix = trimmed; first = false; continue; }
        int i = 0;
        const int lim = std::min(prefix.size(), trimmed.size());
        while (i < lim && prefix[i].toLower() == trimmed[i].toLower()) ++i;
        prefix.truncate(i);
        if (prefix.isEmpty()) break;
    }
    // Strip the separator debris a truncated prefix leaves behind, then
    // require something long enough to read as a name: "HD"/"1K"-length
    // fragments are variant labels, not mods.
    while (!prefix.isEmpty()
           && (prefix.back().isSpace() || prefix.back() == u'('
               || prefix.back() == u'-' || prefix.back() == u'_'))
        prefix.chop(1);
    if (prefix.size() >= 4) return prefix;

    // The options share nothing usable, so the group is carrying the name.
    return groupName.trimmed();
}

SkyrimRuntime classifyRuntimeVariant(const QString &optionName)
{
    const QString n = optionName.toLower();

    bool ae = false, se = false;

    // Version numbers first: "v1.6.629+", "1.6.xxx", "1.6.1170" / "1.5.97",
    // "1.5.x". Word-bounded so "11.6" or a date can't match.
    static const QRegularExpression k16(QStringLiteral("\\b1[._]6\\b|\\b1[._]6[._]"));
    static const QRegularExpression k15(QStringLiteral("\\b1[._]5\\b|\\b1[._]5[._]"));
    if (k16.match(n).hasMatch()) ae = true;
    if (k15.match(n).hasMatch()) se = true;

    // Then the words. "special edition" as a phrase only: "SSE" alone names
    // the game, not a runtime, and appears on both sides of every pair.
    if (n.contains(QLatin1String("anniversary")))     ae = true;
    if (n.contains(QLatin1String("special edition"))) se = true;

    // Bare AE/SE word-tokens last. \b keeps "SSE", "USE", "BASE" out.
    static const QRegularExpression kAeTok(QStringLiteral("\\bae\\b"));
    static const QRegularExpression kSeTok(QStringLiteral("\\bse\\b"));
    if (!ae && !se) {
        if (kAeTok.match(n).hasMatch()) ae = true;
        if (kSeTok.match(n).hasMatch()) se = true;
    }

    // A name carrying both signals ("1.5.97 - 1.6.317 bridge build") is not a
    // side of a pair; say nothing rather than guess.
    if (ae == se) return SkyrimRuntime::None;
    return ae ? SkyrimRuntime::AE : SkyrimRuntime::SE;
}

SkyrimRuntime runtimePreferenceForGame(const QString &gameId)
{
    if (gameId == QLatin1String("skyrimanniversaryedition"))
        return SkyrimRuntime::AE;
    if (gameId == QLatin1String("skyrimspecialedition")
        || gameId == QLatin1String("enderalspecialedition"))
        return SkyrimRuntime::SE;
    return SkyrimRuntime::None;
}

} // namespace fomod
