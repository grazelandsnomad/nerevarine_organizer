#include "fomod_hint.h"

#include <QRegularExpression>
#include <QSet>
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

namespace {

// A run of consecutive Title-Case words, e.g. "Static Mesh Improvement Mod".
// Stops at the first lower-case word, which is what makes "Required for the
// mod to function" yield nothing at all.
// Lower-case words that sit INSIDE a mod name rather than ending it:
// "Complete Alchemy and Cooking Overhaul", "Patch for Purists", "Legacy of
// the Dragonborn". Anything else in lower case ends the name.
bool isConnector(const QString &w)
{
    static const QSet<QString> kJoin = {"and", "of", "the", "for", "a", "an", "in"};
    return kJoin.contains(w.toLower());
}

// A run of Title-Case words, allowing the connectors above between them.
// `stopAtConnector` returns only the part before the first connector.
//
// Both are wanted, because a connector is genuinely ambiguous: in "Complete
// Alchemy and Cooking Overhaul" the "and" is inside ONE name, while in
// "Gourmet and Eating Animations and Sounds SE" it separates TWO. Taking the
// long form alone invents a mod nobody has; taking the short form alone loses
// the real name. So the caller gets both and matches on either.
// One word of a description with its punctuation stripped, plus whether that
// punctuation ENDED A SENTENCE. Stripping without recording it let a name run
// straight into the next sentence: "...Favor Jobs Overhaul. Use it only if..."
// yielded "Favor Jobs Overhaul Use".
struct Word {
    QString text;
    bool    endsSentence = false;
};

QString titleRun(const QList<Word> &words, int from, bool stopAtConnector)
{
    QStringList run;
    for (int i = from; i < words.size(); ++i) {
        const QString w = words[i].text;
        if (w.isEmpty()) break;
        if (!w.front().isUpper()) {
            if (stopAtConnector || !isConnector(w)) break;
            // A connector only stays if a title-cased word follows it, and
            // never across a sentence end.
            if (words[i].endsSentence) break;
            if (i + 1 >= words.size() || words[i + 1].text.isEmpty()
                || !words[i + 1].text.front().isUpper()) break;
            run << w;
            continue;
        }
        run << w;
        if (words[i].endsSentence) break;   // the name cannot span a full stop
    }
    // Never end on a connector.
    while (!run.isEmpty() && isConnector(run.last())) run.removeLast();
    return run.join(QLatin1Char(' '));
}

// Does this read as a mod name rather than a sentence fragment? Either several
// title-cased words, or an acronym - "Materials" on its own does not qualify,
// which is what keeps "requires Materials set for OpenMW" quiet.
bool looksLikeModName(const QString &phrase)
{
    const QStringList words = phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() >= 2) return true;
    return words.size() == 1 && words[0].size() >= 3
        && words[0] == words[0].toUpper();
}

} // namespace

QStringList requiredMods(const QString &description)
{
    if (description.isEmpty()) return {};

    // Split on whitespace but keep punctuation attached, so a sentence end can
    // be detected and a trailing "." trimmed off the name.
    const QStringList raw = description.simplified()
                                .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    static const QRegularExpression kKeyword(
        QStringLiteral("^\\(?(?:requires?|required|requiring|needs?)$"),
        QRegularExpression::CaseInsensitiveOption);
    // "a patch for X", "an integration patch for X", "compatibility with X".
    // An option that exists only to patch another mod is as dependent on it as
    // one that says so outright, and this phrasing is how a patch group
    // actually reads: every entry under Lively Farms' "Patches" heading is one
    // of these.
    static const QRegularExpression kPatchWord(
        QStringLiteral("^\\(?(?:patch|patches|compatibility|compatible)$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kPatchJoin(
        QStringLiteral("^(?:for|with)$"), QRegularExpression::CaseInsensitiveOption);

    QStringList out;
    for (int i = 0; i < raw.size(); ++i) {
        const bool plainKeyword = kKeyword.match(raw[i]).hasMatch();
        const bool patchPhrase  = kPatchWord.match(raw[i]).hasMatch()
                               && i + 1 < raw.size()
                               && kPatchJoin.match(raw[i + 1]).hasMatch();
        if (!plainKeyword && !patchPhrase) continue;
        if (patchPhrase) ++i;   // step over the "for" / "with"

        // Skip an article: "requires the Static Mesh Improvement Mod".
        int start = i + 1;
        if (start < raw.size()) {
            const QString a = raw[start].toLower();
            if (a == QLatin1String("the") || a == QLatin1String("a")
                || a == QLatin1String("an"))
                ++start;
        }

        // Strip punctuation from the tail of each word so "Mod -" and "Mod."
        // do not become part of a name, but REMEMBER a sentence end: without
        // that the name runs on into the next sentence.
        QList<Word> words;
        for (int j = start; j < raw.size(); ++j) {
            QString w = raw[j];
            bool ends = false;
            while (!w.isEmpty() && !w.back().isLetterOrNumber()) {
                const QChar c = w.back();
                if (c == u'.' || c == u'!' || c == u'?' || c == u';'
                    || c == u':' || c == u',')
                    ends = true;
                w.chop(1);
            }
            words.append({w, ends});
        }

        // The long form first - it is the one worth showing the user - then
        // the part before any connector, which is what actually matches when
        // the long form spanned two mods.
        const QString name  = titleRun(words, 0, /*stopAtConnector=*/false);
        const QString short_ = titleRun(words, 0, /*stopAtConnector=*/true);
        if (name.isEmpty() || !looksLikeModName(name)) continue;
        if (!out.contains(name)) out << name;
        if (!short_.isEmpty() && short_ != name && looksLikeModName(short_)
            && !out.contains(short_))
            out << short_;

        // Authors routinely follow the full name with its acronym:
        // "Static Mesh Improvement Mod - SMIM by Brumbek". Take that too - it
        // is often what the mod is actually called in a modlist.
        const int after = name.split(QLatin1Char(' ')).size();
        for (int j = after; j < words.size() && j < after + 3; ++j) {
            const QString w = words[j].text;
            if (w.size() >= 3 && w == w.toUpper() && !out.contains(w)) {
                out << w;
                break;
            }
            if (words[j].endsSentence) break;
        }
    }
    return out;
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

QString betterRuntimeFile(const QString &chosen, const QStringList &candidates,
                          SkyrimRuntime pref)
{
    if (pref == SkyrimRuntime::None) return {};

    const SkyrimRuntime got = classifyRuntimeVariant(chosen);
    // Unclassifiable, or already right: nothing to say. Silence when the file
    // name carries no runtime marking is the whole reason this is safe to run
    // on every download.
    if (got == SkyrimRuntime::None || got == pref) return {};

    // Among the siblings, the first that suits the profile. First rather than
    // best: Nexus lists newest first, and for a runtime that is the build a
    // user wants.
    for (const QString &c : candidates) {
        if (c == chosen) continue;
        if (classifyRuntimeVariant(c) == pref) return c;
    }
    return {};
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
