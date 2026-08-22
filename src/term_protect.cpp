#include "term_protect.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace term_protect {
namespace {

// Words common enough in English item and place names that a phrase made only
// of them is describing a thing, not naming one. Deliberately short: it only
// has to cover what actually recurs across one mod's strings, and anything it
// misses costs a term being left untranslated rather than mistranslated.
const QSet<QString> &ordinaryWords()
{
    static const QSet<QString> kWords = {
        // grammar
        "the","of","and","a","an","in","for","with","to","from","on","at",
        // containers and gear
        "key","chest","door","gate","sword","greatsword","dagger","axe","mace",
        "bow","shield","helmet","helm","armor","armour","boots","gauntlets",
        "ring","amulet","potion","book","note","journal","scroll","robe",
        "wardrobe","barrel","sack","urn","strongbox","satchel","pouch","cupboard",
        // places
        "cave","mine","tower","ruins","ruin","camp","fort","temple","shrine",
        "hall","house","home","depths","lair","catacombs","crypt","barrow",
        "keep","tomb","cellar","basement","entrance","exit","room","chamber",
        // adjectives and materials
        "ancient","old","new","great","greater","lesser","small","large",
        "iron","steel","gold","golden","silver","glass","ebony","leather",
        "north","south","east","west","upper","lower","inner","outer",
        // Creatures, ranks and materials that recur across the Morrowind
        // mods here. They earn their place through looksLikeName(): without
        // them "Ash Slave" and "House Brother" read as somebody's name and
        // never reach the translator at all.
        "ash","blight","slave","zombie","ghoul","sleeper","dreamer","prophet",
        "guard","trader","savant","brother","sister","father","mother",
        "cousin","officer","peer","elf","elves","orc","human","humanoid",
        "banner","staff","spear","arrow","cloak","hood","hooded",
        "common","fine","expensive","extravagant","exquisite","raw","short",
        "long","wooden","twin","clan","blood","bone","skull",
    };
    return kWords;
}

bool isOrdinary(const QString &word, const QSet<QString> &extra)
{
    const QString w = word.toLower();
    return ordinaryWords().contains(w) || extra.contains(w);
}

// Capitalised runs of a string: "Forfeoranna Heim Catacombs" is one run,
// "Greatsword of the Succubi" is two ("Greatsword", "Succubi"). Apostrophes
// stay inside a word so "JK's" survives whole.
QList<QStringList> capitalisedRuns(const QString &text)
{
    static const QRegularExpression kSplit(QStringLiteral("[^\\p{L}\\p{N}']+"));
    QList<QStringList> runs;
    QStringList current;
    for (const QString &tok : text.split(kSplit, Qt::SkipEmptyParts)) {
        if (!tok.isEmpty() && tok.front().isUpper()) {
            current << tok;
        } else {
            if (current.size() > 0) runs << current;
            current.clear();
        }
    }
    if (current.size() > 0) runs << current;
    return runs;
}

} // namespace

QStringList findNames(const QStringList &sources)
{
    return findNames(sources, {}, {});
}

QStringList findNames(const QStringList &sources,
                      const QStringList &alwaysProtect,
                      const QSet<QString> &extraOrdinary)
{
    // Every capitalised n-gram, counted by how many DISTINCT strings hold it.
    // Distinct strings, not occurrences: a name repeated three times in one
    // description is still one piece of evidence.
    QHash<QString, int> seenIn;
    for (const QString &src : sources) {
        QSet<QString> here;
        for (const QStringList &run : capitalisedRuns(src)) {
            for (int i = 0; i < run.size(); ++i) {
                QString gram;
                for (int j = i; j < run.size(); ++j) {
                    if (!gram.isEmpty()) gram += QLatin1Char(' ');
                    gram += run[j];
                    here.insert(gram);
                }
            }
        }
        for (const QString &g : here) ++seenIn[g];
    }

    QStringList candidates;
    for (auto it = seenIn.cbegin(); it != seenIn.cend(); ++it) {
        if (it.value() < 2) continue;                 // must repeat
        const QStringList words = it.key().split(QLatin1Char(' '));
        // At least one word that is not ordinary English, or this is a
        // description ("Iron Key") rather than a name.
        bool named = false;
        for (const QString &w : words)
            if (!isOrdinary(w, extraOrdinary)) { named = true; break; }
        if (!named) continue;
        candidates << it.key();
    }

    // Longest first, then drop any that is contained in a longer kept one:
    // "Forfeoranna Heim" subsumes "Forfeoranna" and "Heim", and masking the
    // whole phrase is what keeps the translation consistent.
    std::sort(candidates.begin(), candidates.end(),
              [](const QString &a, const QString &b) {
                  if (a.size() != b.size()) return a.size() > b.size();
                  return a < b;
              });

    // The user's own names go in first and unconditionally, so a longer
    // automatic phrase cannot swallow one and a single-appearance name still
    // gets protected.
    QStringList kept;
    for (const QString &p : alwaysProtect)
        if (!p.trimmed().isEmpty() && !kept.contains(p.trimmed()))
            kept << p.trimmed();
    std::sort(kept.begin(), kept.end(),
              [](const QString &a, const QString &b) { return a.size() > b.size(); });

    for (const QString &c : candidates) {
        bool covered = false;
        for (const QString &k : kept) {
            if (k.contains(c)) { covered = true; break; }
        }
        if (!covered) kept << c;
    }
    return kept;
}

QString tokenFor(int index)
{
    // Letters, never digits. Measured against the live endpoint: a token
    // carrying a digit is read as a CODE and both the word order and the word
    // sense suffer, while a letters-only one is read as the place name it
    // stands for.
    //
    //   "Nrv0 Depths" -> "Nrv0 Profundidades"      (English order)
    //   "Nrvaa Depths" -> "Profundidades de Nrvaa" (Spanish order)
    //   "Nrv0 Key"    -> "Clave Nrv0"              (a password)
    //   "Nrvaa Key"   -> "Llave Nrvaa"             (a door key)
    //
    // Two letters give 676 distinct names, far more than one mod ever needs.
    const int i = qMax(0, index);
    return QStringLiteral("Nrv")
         + QChar(u'a' + (i / 26) % 26)
         + QChar(u'a' + i % 26);
}

QString mask(const QString &text, const QStringList &terms)
{
    QString out = text;
    // terms arrive longest first from findNames; honour that so a longer name
    // is substituted before any shorter one nested inside it.
    for (int i = 0; i < terms.size(); ++i)
        out.replace(terms[i], tokenFor(i), Qt::CaseSensitive);
    return out;
}

namespace {

// "aá" -> "aa". The translator sometimes decorates a bare token with the
// target language's orthography, and the decoration is not part of our name.
QString stripMarks(const QString &s)
{
    const QString d = s.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(d.size());
    for (const QChar c : d)
        if (c.category() != QChar::Mark_NonSpacing) out.append(c);
    return out.toLower();
}

} // namespace

QString unmask(const QString &text, const QStringList &terms)
{
    if (terms.isEmpty()) return text;

    // Match the token SHAPE, then decide which term it was by comparing the
    // letters with any accents removed. Matching each token literally missed
    // "Nrvaá" and left it in the finished translation.
    // UseUnicodePropertiesOption is not optional here: without it PCRE2's \b
    // uses ASCII word characters, so the accented tail of "Nrvaá" is not a
    // letter, the trailing boundary never matches, and the token survives into
    // the finished translation.
    static const QRegularExpression re(
        QStringLiteral("\\bNrv\\s*(\\p{L}\\p{L})\\b"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::UseUnicodePropertiesOption);

    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString letters = stripMarks(m.captured(1));

        int idx = -1;
        for (int i = 0; i < terms.size(); ++i)
            if (stripMarks(tokenFor(i).mid(3)) == letters) { idx = i; break; }
        if (idx < 0) continue;   // not one of ours; leave it alone

        out += text.mid(last, m.capturedStart() - last);
        out += terms[idx];
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

bool looksLikeName(const QString &text, const QStringList &terms,
                   const QSet<QString> &extraOrdinary)
{
    const QString subject = text.trimmed();
    if (subject.isEmpty()) return false;

    // Words already covered by a found term are names by definition; the
    // question is only about what is left over.
    QString rest = subject;
    for (const QString &t : terms) {
        if (t.trimmed().isEmpty()) continue;
        rest.remove(t, Qt::CaseInsensitive);
    }

    static const QRegularExpression kSplit(QStringLiteral("[^\\p{L}\\p{N}']+"),
                                           QRegularExpression::UseUnicodePropertiesOption);
    const QStringList words = rest.split(kSplit, Qt::SkipEmptyParts);

    // Nothing left but the terms: isOnlyNames territory, and a name.
    if (words.isEmpty()) return !terms.isEmpty();

    // Evidence has to come from THIS row. One unknown word on its own says
    // nothing - it is as likely to be "Dreamer" as "Balen" - so it takes
    // either a found term standing beside it or a second unknown word. That
    // some other row in the mod contained a name is not evidence about this
    // one, which is what keying this off `terms` being non-empty would mean.
    if (rest.size() == subject.size() && words.size() < 2) return false;

    for (const QString &w : words) {
        if (isOrdinary(w, extraOrdinary)) return false;   // a description
        // A lowercase leftover is prose, not part of a name.
        if (!w.isEmpty() && !w.front().isUpper()) return false;
    }
    return true;
}

bool isOnlyNames(const QString &masked, int termCount)
{
    QString rest = masked;
    for (int i = 0; i < termCount; ++i)
        rest.remove(tokenFor(i), Qt::CaseInsensitive);
    // Whatever is left has to be worth translating; punctuation and spaces
    // are not.
    static const QRegularExpression kNoise(QStringLiteral("[^\\p{L}\\p{N}]+"));
    rest.remove(kNoise);
    return rest.isEmpty();
}

} // namespace term_protect
