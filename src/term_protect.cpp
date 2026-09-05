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
        "bow","quiver","shield","helmet","helm","armor","armour","boots",
        "gauntlets",
        "ring","amulet","potion","book","note","journal","scroll","robe",
        "wardrobe","barrel","sack","urn","strongbox","satchel","pouch","cupboard",
        // places
        "cave","mine","tower","ruins","ruin","camp","fort","temple","shrine",
        "hall","house","home","depths","lair","catacombs","crypt","barrow",
        "keep","tomb","cellar","basement","entrance","exit","room","chamber",
        // adjectives and materials
        "ancient","old","new","great","greater","lesser","small","large",
        "iron","steel","gold","golden","silver","glass","ebony","leather",
        // Morrowind's own materials, which recur across every armour and
        // weapon name in these mods: "Chitin Quiver" is a thing, not a person.
        "chitin","bonemold","netch",
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
        // Item and creature vocabulary from the Morrowind mods here. Without
        // these, "Boiling Pot", "Ashlander Tent" and "Domesticated Guar" all
        // read as somebody's name and are never translated at all.
        "pot","tent","pipe","whip","herder","herders","shortsword","longsword",
        "damaged","domesticated","tame","wild","bowl","cup","plate","basket",
        "lantern","torch","rug","mat","pillow","blanket","hide","pelt",
        "board","boat","set","sets","orders","order","work","cutting","masked",
        "broadsword","halberd","ringmail","mail","chainmail","cuirass",
        "greaves","pauldron","pauldrons","bracer","bracers","sleeve","sleeves",
        "glove","gloves","cap","caps","skirt","shirt","pants","belt","mask",
        "flower","flowers","leaf","leaves","root","roots","seed","seeds",
        "paste","powder","bread","meat","stew","soup","fried","boiled","dried",
        "skeleton","skull","corpse","remains","tail","claw","scale","scales",
    };
    return kWords;
}

bool isOrdinary(const QString &word, const QSet<QString> &extra)
{
    const QString w = word.toLower();
    return ordinaryWords().contains(w) || extra.contains(w);
}

} // namespace

QStringList knownNames()
{
    // Ashlander given names. Every one of these is a person in Morrowind or in
    // a mod built on it, none is a word in English, and each appears once in
    // the string list it comes from - which is exactly the case repetition
    // cannot catch. Verified against The Ashlanders' own plugin, where the
    // camp cells name them: "Urshilaku Camp, Shalapli's Yurt", "Zainab Camp,
    // Kuda's Yurt", "Erabenimsun Camp, Addut-Lamanu's Yurt".
    //
    // The two-part forms come first only for readability; findNames sorts by
    // length, so "Addut-Lamanu" is masked before the bare "Lamanu" either way.
    static const QStringList kNames = {
        QStringLiteral("Sinnammu Mirpal"),
        QStringLiteral("Addut-Lamanu"),
        QStringLiteral("Ashu-Ahhe"),
        QStringLiteral("Kausamsi"),
        QStringLiteral("Massanud"),
        QStringLiteral("Ninibaal"),
        QStringLiteral("Shanbaal"),
        QStringLiteral("Shalapli"),
        QStringLiteral("Ilasour"),
        QStringLiteral("Shalibi"),
        QStringLiteral("Shulhaz"),
        QStringLiteral("Hanlay"),
        QStringLiteral("Kumlay"),
        QStringLiteral("Lamanu"),
        QStringLiteral("Mashah"),
        QStringLiteral("Miishi"),
        QStringLiteral("Shinat"),
        QStringLiteral("Ashlander"),
        QStringLiteral("Ahan"),
        QStringLiteral("Ahhe"),
        QStringLiteral("Idan"),
        QStringLiteral("Kuda"),
        QStringLiteral("Yalit"),
        // Creatures, not descriptions: "Domesticated Guar" has to come back
        // as a domesticated GUAR, and a translator asked cold invents a
        // different animal every time.
        QStringLiteral("Shalk"),
        QStringLiteral("Guar"),
    };
    return kNames;
}

namespace {

// `name` as a WHOLE word. A term is a word, and matching it as a bare
// substring is how "Boar" gets pulled out of the middle of "Cupboard".
QRegularExpression wordRe(const QString &name, bool caseSensitive)
{
    QRegularExpression::PatternOptions opts =
        QRegularExpression::UseUnicodePropertiesOption;
    if (!caseSensitive) opts |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(
        QStringLiteral("(?<![\\p{L}\\p{N}])") + QRegularExpression::escape(name)
            + QStringLiteral("(?![\\p{L}\\p{N}])"),
        opts);
}

// Case-sensitive on purpose: these are proper nouns and mask() matches them
// the same way, so a lowercase "guar" in prose is left as prose.
bool mentionedIn(const QString &name, const QStringList &sources)
{
    const QRegularExpression re = wordRe(name, true);
    for (const QString &src : sources)
        if (re.match(src).hasMatch()) return true;
    return false;
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

QStringList mentionedFrom(const QStringList &vocabulary,
                          const QStringList &sources)
{
    QStringList out;
    for (const QString &term : vocabulary) {
        const QString t = term.trimmed();
        if (t.isEmpty() || out.contains(t)) continue;
        if (mentionedIn(t, sources)) out << t;
    }
    return out;
}

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

    // Then the built-in names - but only the ones this mod actually says.
    // Carrying all of them would mask nothing extra and cost a pass over every
    // row for each unused name, on mods that can run to thousands of rows.
    //
    // extraOrdinary still wins: [ordinary] is documented as the escape hatch
    // when protection is too eager, and a built-in the user has declared a
    // word must obey that like any other.
    for (const QString &n : knownNames()) {
        if (extraOrdinary.contains(n.toLower())) continue;
        if (kept.contains(n)) continue;
        if (mentionedIn(n, sources)) kept << n;
    }

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

namespace {

// wordRe, kept. mask() runs once per row per term and a plugin can hold eight
// thousand rows, so building the same expression a quarter of a million times
// a run is the difference between instant and noticeable. Terms are bounded by
// the names one mod uses, so the map cannot grow without limit, and this is
// only ever reached from the editor's own thread.
const QRegularExpression &cachedWordRe(const QString &term)
{
    static QHash<QString, QRegularExpression> cache;
    auto it = cache.constFind(term);
    if (it == cache.constEnd()) it = cache.insert(term, wordRe(term, true));
    return it.value();
}

} // namespace

QString mask(const QString &text, const QStringList &terms)
{
    QString out = text;
    // terms arrive longest first from findNames; honour that so a longer name
    // is substituted before any shorter one nested inside it.
    for (int i = 0; i < terms.size(); ++i)
        out.replace(cachedWordRe(terms[i]), tokenFor(i));
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
    // Whole words only. A bare substring removal lets a short name eat the
    // middle of an unrelated word - "Boar" turns "Cupboard" into "Cupd",
    // which then reads as a name because "Cupd" is in no dictionary.
    QString rest = subject;
    for (const QString &t : terms) {
        const QString term = t.trimmed();
        if (term.isEmpty()) continue;
        rest.remove(wordRe(term, false));
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
