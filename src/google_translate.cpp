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
    q.addQueryItem(QStringLiteral("sl"),     QStringLiteral("auto"));
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

} // namespace google_translate
