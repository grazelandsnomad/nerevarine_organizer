#include "file_pick.h"

#include <QRegularExpression>
#include <QStringList>

namespace file_pick {
namespace {

// A name reduced to the words that identify the file, for asking whether one
// file's name quotes another's. Version fragments go first: the patch on
// Rafael's page is "Patch For Enhanced PBR Lighting For OpenMW 0.52" and the
// file it patches is "Enhanced PBR Lighting for OpenMW 0.49-0.52", so the
// names only line up once the numbers are out of the way.
QString identityWords(const QString &name)
{
    static const QRegularExpression rxNonWord(QStringLiteral("[^a-z0-9]+"));
    static const QRegularExpression rxVersionish(QStringLiteral("^v?\\d+[a-z]?$"));

    QStringList words;
    for (const QString &tok :
         name.toLower().split(rxNonWord, Qt::SkipEmptyParts)) {
        if (rxVersionish.match(tok).hasMatch()) continue;
        words << tok;
    }
    return words.join(QLatin1Char(' '));
}

bool isCategory(const QString &category, const char *name)
{
    return category.compare(QLatin1String(name), Qt::CaseInsensitive) == 0;
}

bool isPatchCategory(const QString &c)
{
    return isCategory(c, "UPDATE") || isCategory(c, "PATCH");
}

bool isOldCategory(const QString &c)
{
    return isCategory(c, "OLD_VERSION") || isCategory(c, "OLD VERSION")
        || isCategory(c, "ARCHIVED");
}

} // namespace

QString plainDescription(const QString &raw, int maxChars)
{
    if (raw.isEmpty()) return {};

    QString t = raw;
    // A <br> is a line break the author meant; every other tag is markup
    // around words and becomes a space so the words do not run together.
    static const QRegularExpression rxBreak(
        QStringLiteral("<\\s*br\\s*/?\\s*>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rxTag(QStringLiteral("<[^>]*>"));
    t.replace(rxBreak, QStringLiteral(" "));
    t.replace(rxTag,   QStringLiteral(" "));

    // &amp; last, or "&amp;lt;" would decode twice into a tag we just removed.
    t.replace(QLatin1String("&nbsp;"), QStringLiteral(" "));
    t.replace(QLatin1String("&quot;"), QStringLiteral("\""));
    t.replace(QLatin1String("&#39;"),  QStringLiteral("'"));
    t.replace(QLatin1String("&apos;"), QStringLiteral("'"));
    t.replace(QLatin1String("&lt;"),   QStringLiteral("<"));
    t.replace(QLatin1String("&gt;"),   QStringLiteral(">"));
    t.replace(QLatin1String("&amp;"),  QStringLiteral("&"));

    t = t.simplified();
    if (maxChars > 0 && t.size() > maxChars) {
        // Cut at the last space before the limit so a word is never sliced;
        // a description with no spaces at all is cut where the limit falls.
        int cut = t.lastIndexOf(QLatin1Char(' '), maxChars);
        if (cut < maxChars / 2) cut = maxChars;
        t = t.left(cut).trimmed() + QStringLiteral("...");
    }
    return t;
}

QList<Note> describe(const QList<FileInfo> &files)
{
    QList<Note> out;
    out.reserve(files.size());

    // The page's own answer to "which one do I want?". Only meaningful when
    // exactly one file claims it; two would mean the flag says nothing.
    int primaryIdx = -1;
    int primaryCount = 0;
    for (int i = 0; i < files.size(); ++i) {
        if (!files[i].isPrimary) continue;
        ++primaryCount;
        if (primaryIdx < 0) primaryIdx = i;
    }
    if (primaryCount != 1) primaryIdx = -1;

    for (int i = 0; i < files.size(); ++i) {
        const FileInfo &f = files[i];
        Note n;

        if (isPatchCategory(f.category)) {
            n.kind = Kind::Patch;

            // Which file it patches. A patch names what it patches - "Patch
            // For Enhanced PBR Lighting For OpenMW" - so the longest other
            // name quoted inside this one is the target. Longest wins because
            // a page may hold both "Enhanced PBR Lighting" and "Enhanced PBR
            // Lighting for OpenMW", and the more specific is the right answer.
            const QString mine = identityWords(f.name);
            int bestLen = 0;
            for (int j = 0; j < files.size(); ++j) {
                if (j == i || isPatchCategory(files[j].category)) continue;
                const QString theirs = identityWords(files[j].name);
                // Two words and eight characters before a name counts as
                // quoted: a file called "Core" or "1K" appears inside half the
                // names on a page and identifies nothing.
                if (theirs.size() < 8) continue;
                if (theirs.count(QLatin1Char(' ')) < 1) continue;
                if (!mine.contains(theirs)) continue;
                if (theirs.size() <= bestLen) continue;
                bestLen  = theirs.size();
                n.goesOn = j;
            }

            if (n.goesOn >= 0) n.detailArg = files[n.goesOn].name;
        } else if (isOldCategory(f.category)) {
            n.kind = Kind::Old;
        } else if (isCategory(f.category, "OPTIONAL")) {
            n.kind = Kind::Optional;
        } else if (isCategory(f.category, "MAIN")) {
            if (i == primaryIdx) {
                n.kind = Kind::Base;
            } else if (primaryIdx >= 0) {
                // Only an "add-on" because the page named something else its
                // main download. With no primary flag there is nothing for
                // this to be an add-on TO, and two MAIN files are just two
                // MAIN files - said plainly rather than ranked by guess.
                n.kind      = Kind::AddOn;
                n.detailArg = files[primaryIdx].name;
            } else {
                n.kind = Kind::Main;
            }
        } else {
            n.kind = Kind::Other;
        }

        out.append(n);
    }
    return out;
}

int defaultIndex(const QList<FileInfo> &files, const QList<Note> &notes,
                 const QList<int> &engineScores)
{
    if (files.isEmpty()) return 0;

    auto score = [&engineScores](int i) {
        return i < engineScores.size() ? engineScores[i] : 0;
    };
    auto wholeMod = [&notes](int i) {
        if (i >= notes.size()) return true;
        return notes[i].kind != Kind::Patch && notes[i].kind != Kind::Old;
    };

    // A page can hold nothing but patches (an author who only ever uploads
    // updates). Filtering then leaves no candidate at all, so the filter is
    // dropped rather than the list.
    bool anyWhole = false;
    for (int i = 0; i < files.size(); ++i) anyWhole |= wholeMod(i);

    // The page's main download is the author's own answer, and it outranks
    // anything read out of a file name - which is the bug this fixes: a 2.3 MB
    // add-on won the default over a 22.4 MB base file because its name said
    // "OpenMW" and the base file's did not. A negative score still overrules
    // it, since that means the build is for the wrong engine outright.
    for (int i = 0; i < files.size(); ++i) {
        if (i < notes.size() && notes[i].kind == Kind::Base && score(i) >= 0)
            return i;
    }

    int best = -1;
    for (int i = 0; i < files.size(); ++i) {
        if (anyWhole && !wholeMod(i)) continue;
        if (best < 0 || score(i) > score(best)) best = i;
    }
    return best < 0 ? 0 : best;
}

} // namespace file_pick
