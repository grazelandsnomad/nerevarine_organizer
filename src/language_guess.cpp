#include "language_guess.h"

#include "target_language.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace language_guess {
namespace {

// Function words only, matched whole-word so "de" cannot fire inside
// "Deathlord". Accented LETTERS were tried as a second signal and removed:
// measured on the two real cases, they point the wrong way.
//
//                          function words   accented letters
//   Better Crowd Citizens ES   33 (20%)          5  (3%)
//   Bonemold weapons (Russian)  0  (0%)         23 (74%)
//
// The Russian mod stores Cyrillic in CP1251, which read as CP1252 becomes
// "Ñóíäóê" - a string full of characters that are also Spanish. Letters made
// it look 74% Spanish while the genuinely Spanish mod scored 3%. Languages
// whose script IS the signal keep working because their function words are in
// that script too ("и", "в", "на").
struct Markers {
    QStringList words;
};

const QHash<QString, Markers> &markerTable()
{
    static const QHash<QString, Markers> kTable = {
        {QStringLiteral("english"),
         {{"the","of","and","to","a","in","for","with","from","your","you"}}},
        {QStringLiteral("spanish"),
         {{"de","la","el","los","las","del","y","en","para","con","por","un","una"}}},
        {QStringLiteral("french"),
         {{"de","la","le","les","des","du","et","en","pour","avec","un","une"}}},
        {QStringLiteral("german"),
         {{"der","die","das","und","von","mit","für","ein","eine","im","zum"}}},
        {QStringLiteral("italian"),
         {{"di","la","il","le","dei","del","e","in","per","con","un","una"}}},
        {QStringLiteral("portuguese"),
         {{"de","da","do","os","as","dos","e","em","para","com","um","uma"}}},
        {QStringLiteral("catalan"),
         {{"de","la","el","els","les","dels","i","en","per","amb","un","una"}}},
        {QStringLiteral("polish"),
         {{"i","w","na","do","z","dla","od","po"}}},
        {QStringLiteral("czech"),
         {{"a","v","na","do","z","pro","od","se"}}},
        {QStringLiteral("russian"),
         {{"и","в","на","с","для","из","по"}}},
    };
    return kTable;
}

// How many of `samples` carry a marker of this language.
int hits(const QStringList &samples, const Markers &m)
{
    QSet<QString> words;
    for (const QString &w : m.words) words.insert(w);

    static const QRegularExpression kSplit(QStringLiteral("[^\\p{L}]+"));

    int n = 0;
    for (const QString &s : samples) {
        const QStringList toks = s.toLower().split(kSplit, Qt::SkipEmptyParts);
        for (const QString &t : toks)
            if (words.contains(t)) { ++n; break; }
    }
    return n;
}

} // namespace

bool textLooksLike(const QStringList &samples, const QString &token)
{
    if (samples.isEmpty()) return false;

    const auto it = markerTable().constFind(token.trimmed().toLower());
    if (it == markerTable().constEnd()) return false;   // no data: cannot tell
    if (token.trimmed().toLower() == QLatin1String("english"))
        return false;   // "already English" is the default state, not a finding

    const int target  = hits(samples, *it);
    const int english = hits(samples, markerTable().value(QStringLiteral("english")));

    // Three independent hits AND a tenth of the sample AND more evidence than
    // for English. A lone "Casa de Vivec" in an English mod clears none of
    // these; Better Crowd Citizens ES scores 20%, twice the bar.
    return target >= 3
        && target * 10 >= samples.size()
        && target > english;
}

bool nameSuggestsLanguage(const QString &modName, const QString &token)
{
    const QString display = target_language::displayName(token);
    if (display.isEmpty()) return false;

    // Whole word, so "Spanish" does not fire inside some longer word, and
    // case-insensitive because mod names are typed by hand.
    const QRegularExpression re(
        QStringLiteral("\\b") + QRegularExpression::escape(display) + QStringLiteral("\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(modName).hasMatch();
}

bool alreadyInLanguage(const QString &modName, const QStringList &samples,
                       const QString &token)
{
    if (samples.isEmpty()) return false;
    if (textLooksLike(samples, token)) return true;

    // The name agreeing lowers the bar, but does not remove it: a mod called
    // "... Spanish" still has to show SOME target-language text, or a mod
    // named for the language it translates FROM would silence itself.
    if (!nameSuggestsLanguage(modName, token)) return false;

    const auto it = markerTable().constFind(token.trimmed().toLower());
    if (it == markerTable().constEnd()) return false;
    return hits(samples, *it) >= 1;
}

} // namespace language_guess
