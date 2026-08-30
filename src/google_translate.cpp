#include "google_translate.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>

namespace google_translate {

QUrl requestUrl(const QString &text, const QString &targetIso)
{
    QUrl url(QStringLiteral("https://translate.googleapis.com/translate_a/single"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client"), QStringLiteral("gtx"));
    // "en", not "auto". The editor only ever translates English mods, and
    // auto-detection handed a name-shaped string with no sentence around it
    // decides it is some other language and paraphrases: "Dagoth Andas" came
    // back "sin respirar", "Dagoth Faras" as "apenas lo hace".
    q.addQueryItem(QStringLiteral("sl"),     QStringLiteral("en"));
    q.addQueryItem(QStringLiteral("tl"),     targetIso);
    q.addQueryItem(QStringLiteral("dt"),     QStringLiteral("t"));
    q.addQueryItem(QStringLiteral("q"),      text);
    url.setQuery(q);
    return url;
}

QByteArray userAgent()
{
    return "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
           "(KHTML, like Gecko) Chrome/120.0 Safari/537.36";
}

QUrl requestUrl(const QStringList &texts, const QString &targetIso)
{
    QUrl url(QStringLiteral("https://translate.googleapis.com/translate_a/single"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("client"), QStringLiteral("gtx"));
    q.addQueryItem(QStringLiteral("sl"),     QStringLiteral("en"));
    q.addQueryItem(QStringLiteral("tl"),     targetIso);
    q.addQueryItem(QStringLiteral("dt"),     QStringLiteral("t"));
    for (const QString &t : texts)
        q.addQueryItem(QStringLiteral("q"), t);
    url.setQuery(q);
    return url;
}

bool fitsInOneRequest(const QStringList &texts, const QString &targetIso)
{
    if (texts.size() > kMaxBatch) return false;
    return requestUrl(texts, targetIso).toEncoded().size() <= kMaxUrlBytes;
}

int fitBatch(const QStringList &texts, int from, const QString &targetIso)
{
    if (from < 0 || from >= texts.size()) return 0;

    int n = 0;
    for (; n < kMaxBatch && from + n < texts.size(); ++n) {
        if (!fitsInOneRequest(texts.mid(from, n + 1), targetIso)) break;
    }
    // One string that is too long on its own still goes, alone. Returning 0
    // would leave the pump with nothing to send and nothing to drain.
    return n > 0 ? n : 1;
}

QString parseResponse(const QByteArray &json)
{
    // Shape: [[["translated","original",null,null,1], ...], ...]
    // The outer array's first element is the list of SEGMENTS; each segment's
    // first element is its translated text. Long strings arrive split across
    // several, so they are joined rather than sampled.
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray()) return {};

    const QJsonArray segments = doc.array().at(0).toArray();
    if (segments.isEmpty()) return {};

    QString out;
    for (const QJsonValue &seg : segments)
        out += seg.toArray().at(0).toString();
    return out;
}

namespace {

// Whitespace-insensitive comparison, for matching an echoed source against
// what was sent. Google normalises spacing around segment boundaries, so an
// exact compare would reject answers that are perfectly correct.
QString squashed(const QString &s)
{
    return s.simplified().replace(QLatin1Char('\u00a0'), QLatin1Char(' '));
}

} // namespace

QStringList parseResponses(const QByteArray &json, const QStringList &sent)
{
    if (sent.isEmpty()) return {};

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray()) return {};
    const QJsonArray segments = doc.array().at(0).toArray();
    if (segments.isEmpty()) return {};

    // Reassemble by ORIGINAL, not by position. Long strings come back split
    // across several segments and several q= run together in one list, so the
    // only reliable boundary is "the echoed source now equals what we asked".
    QStringList out;
    QString accTranslated, accOriginal;
    int want = 0;

    for (const QJsonValue &v : segments) {
        const QJsonArray seg = v.toArray();
        accTranslated += seg.at(0).toString();
        accOriginal   += seg.at(1).toString();

        if (want >= sent.size()) return {};        // more text than we asked for
        if (squashed(accOriginal) == squashed(sent[want])) {
            out << accTranslated;
            accTranslated.clear();
            accOriginal.clear();
            ++want;
        }
    }

    // Anything left over means a boundary was never found: the shape is not
    // what this reads, and a partial answer must not be passed off as a whole.
    if (!squashed(accOriginal).isEmpty()) return {};
    if (out.size() != sent.size())        return {};
    return out;
}

Failure classify(int netError, int httpStatus, bool gotText)
{
    // The status decides first. Qt reports a 429 as a non-zero error of its
    // own, so testing netError first would send every block down the generic
    // branch and lose the one distinction that matters.
    if (httpStatus == 429) return Failure::Blocked;
    if (httpStatus == 403 || httpStatus == 401) return Failure::Refused;

    // Errored with no status at all: nothing answered. DNS, no route, TLS,
    // timeout - all of them mean the machine could not reach the endpoint,
    // which is a different sentence from the endpoint turning it away.
    if (netError != 0 && httpStatus == 0) return Failure::Offline;

    if (httpStatus >= 400) return Failure::HttpError;
    if (netError != 0)     return Failure::HttpError;

    // HTTP was fine and the body still was not readable: the endpoint has
    // changed shape. Not a block, and telling somebody to wait would send
    // them away for fifteen minutes over something waiting cannot fix.
    if (!gotText) return Failure::BadResponse;
    return Failure::Ok;
}

void FailureTally::count(Failure f)
{
    switch (f) {
        case Failure::Ok:          ++ok;          break;
        case Failure::Blocked:     ++blocked;     break;
        case Failure::Refused:     ++refused;     break;
        case Failure::Offline:     ++offline;     break;
        case Failure::BadResponse: ++badResponse; break;
        case Failure::HttpError:   ++httpError;   break;
    }
}

Failure worstOf(const FailureTally &tally)
{
    if (tally.blocked)     return Failure::Blocked;
    if (tally.refused)     return Failure::Refused;
    if (tally.offline)     return Failure::Offline;
    if (tally.httpError)   return Failure::HttpError;
    if (tally.badResponse) return Failure::BadResponse;
    return Failure::Ok;
}

int cooloffMinutesFor(int strikes)
{
    // A ladder rather than a formula, so every step is a number somebody chose
    // and can argue with. Capped at a day: past that the user is better served
    // by being told to use a different network than by a longer timer.
    static const int kLadder[] = { 15, 15, 60, 360, 1440 };
    const int n = qBound(0, strikes, 4);
    return kLadder[n];
}

int cooloffSecondsLeft(qint64 blockedAtEpochSec, qint64 nowEpochSec,
                       int strikes)
{
    if (blockedAtEpochSec <= 0) return 0;
    // A stamp in the future is a broken clock, not a longer wait. Fail open:
    // there is no UI anywhere to clear this, so a machine whose clock jumped
    // would otherwise lose machine translation for good.
    if (blockedAtEpochSec > nowEpochSec) return 0;

    const qint64 elapsed = nowEpochSec - blockedAtEpochSec;
    const qint64 total   = qint64(cooloffMinutesFor(strikes)) * 60;
    if (elapsed >= total) return 0;
    return int(total - elapsed);
}

} // namespace google_translate
