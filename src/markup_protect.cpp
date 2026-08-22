#include "markup_protect.h"

#include <QRegularExpression>

namespace markup_protect {
namespace {

// The three shapes, in one pass so they come back in document order.
//
//   <[^<>]*>        a tag and its attributes. Deliberately not nested-aware:
//                   these records are flat markup, and a stray "<" in prose
//                   ("a < b") fails to match rather than swallowing the rest
//                   of the line.
//   %[A-Za-z]+      the game's substitutions. Letters only, so "100%" and
//                   "50% chance" are prose and stay translatable.
//   \\[a-z]         an escape written into the record as backslash + letter.
const QRegularExpression &rxSpan()
{
    static const QRegularExpression re(
        QStringLiteral("<[^<>]*>|%[A-Za-z]+|\\\\[A-Za-z]"));
    return re;
}

} // namespace

QStringList findSpans(const QString &text)
{
    QStringList out;
    auto it = rxSpan().globalMatch(text);
    while (it.hasNext()) out << it.next().captured(0);
    return out;
}

QString tokenFor(int index)
{
    return QStringLiteral("{%1}").arg(index);
}

QString mask(const QString &text, const QStringList &spans)
{
    if (spans.isEmpty()) return text;

    // Walked left to right against the same expression that found them, so
    // the nth match becomes the nth token even when two spans are identical -
    // two <FONT> tags are two entries and each goes back in its own place.
    QString out;
    qsizetype last = 0;
    int idx = 0;
    auto it = rxSpan().globalMatch(text);
    while (it.hasNext() && idx < spans.size()) {
        const auto m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        out += tokenFor(idx++);
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

bool isOnlySpans(const QString &masked)
{
    static const QRegularExpression kToken(QStringLiteral("\\{\\d+\\}"));
    static const QRegularExpression kNoise(QStringLiteral("[^\\p{L}\\p{N}]+"),
                                           QRegularExpression::UseUnicodePropertiesOption);
    QString rest = masked;
    rest.remove(kToken);
    rest.remove(kNoise);
    return rest.isEmpty();
}

Restored restore(const QString &source, const QString &translated,
                 const QStringList &spans)
{
    Restored r;
    if (spans.isEmpty()) { r.text = translated; return r; }

    // Tolerant of what the endpoint does to a token: a space inside the
    // braces, or the braces spaced away from the words either side.
    QString out;
    {
        static const QRegularExpression kToken(QStringLiteral("\\{\\s*(\\d+)\\s*\\}"));
        qsizetype last = 0;
        auto it = kToken.globalMatch(translated);
        while (it.hasNext()) {
            const auto m = it.next();
            const int idx = m.captured(1).toInt();
            if (idx < 0 || idx >= spans.size()) continue;   // not one of ours
            out += translated.mid(last, m.capturedStart() - last);
            out += spans[idx];
            last = m.capturedEnd();
        }
        out += translated.mid(last);
    }

    // Did every span come back, as many times as the source had it? Counting
    // rather than merely looking catches the doubled token as well as the
    // dropped one.
    bool intact = true;
    for (const QString &sp : spans) {
        if (out.count(sp) != source.count(sp)) { intact = false; break; }
    }
    if (intact) { r.text = out; return r; }

    // It did not hold up. Rebuild: whatever opens the source opens the result
    // and whatever closes it closes the result, with the translated prose in
    // between and any token wreckage taken out of it.
    r.repaired = true;

    // How many spans sit back to back at the very start, and at the very end.
    QString prefix;
    int lead = 0;
    while (lead < spans.size()) {
        const QString grown = prefix + spans[lead];
        if (!source.startsWith(grown)) break;
        prefix = grown;
        ++lead;
    }

    QString suffix;
    int tail = 0;
    while (tail < spans.size() - lead) {
        const QString grown = spans[spans.size() - 1 - tail] + suffix;
        if (!source.endsWith(grown)) break;
        suffix = grown;
        ++tail;
    }

    // Whatever separated the opening run from the prose in the source goes
    // back with it, or the rebuild reads "%Nameoyo que hay alguien".
    {
        const QString after = source.mid(prefix.size());
        int i = 0;
        while (i < after.size() && after.at(i).isSpace()) ++i;
        prefix += after.left(i);

        const QString before = source.left(source.size() - suffix.size());
        int j = before.size();
        while (j > 0 && before.at(j - 1).isSpace()) --j;
        suffix = before.mid(j) + suffix;
    }

    QString middle = out;
    static const QRegularExpression kAnyToken(QStringLiteral("\\{\\s*\\d+\\s*\\}"));
    middle.remove(kAnyToken);
    for (int i = 0; i < lead; ++i)                       middle.remove(spans[i]);
    for (int i = spans.size() - tail; i < spans.size(); ++i) middle.remove(spans[i]);
    middle = middle.trimmed();

    r.text = prefix + middle + suffix;

    // Anything that was neither at the front nor at the back has nowhere to go
    // back to: the rebuild knows where a record opens and closes, not where a
    // tag belonged inside a sentence it has just reordered.
    r.lostInside = (lead + tail) < spans.size();
    return r;
}

} // namespace markup_protect
