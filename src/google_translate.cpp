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

int cooloffSecondsLeft(qint64 blockedAtEpochSec, qint64 nowEpochSec)
{
    if (blockedAtEpochSec <= 0) return 0;
    // A stamp in the future is a broken clock, not a longer wait. Fail open:
    // there is no UI anywhere to clear this, so a machine whose clock jumped
    // would otherwise lose machine translation for good.
    if (blockedAtEpochSec > nowEpochSec) return 0;

    const qint64 elapsed = nowEpochSec - blockedAtEpochSec;
    const qint64 total   = qint64(kBlockCooloffMinutes) * 60;
    if (elapsed >= total) return 0;
    return int(total - elapsed);
}

} // namespace google_translate
