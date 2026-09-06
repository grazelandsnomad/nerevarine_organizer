#include "libre_translate.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace libre_translate {

QUrl endpointUrl(const QString &base)
{
    QString b = base.trimmed();
    if (b.isEmpty()) return {};
    while (b.endsWith(QLatin1Char('/'))) b.chop(1);
    // A user who pasted the full endpoint meant the full endpoint.
    if (!b.endsWith(QLatin1String("/translate")))
        b += QLatin1String("/translate");
    return QUrl(b);
}

QByteArray requestBody(const QStringList &texts, const QString &targetIso,
                       const QString &apiKey)
{
    QJsonArray q;
    for (const QString &t : texts) q.append(t);

    QJsonObject o;
    o.insert(QStringLiteral("q"),      q);
    o.insert(QStringLiteral("source"), QStringLiteral("en"));
    o.insert(QStringLiteral("target"), targetIso);
    // "text", never "html": plugin strings carry @# markup and angle brackets
    // of their own, and the html mode would try to preserve them as tags.
    o.insert(QStringLiteral("format"), QStringLiteral("text"));
    if (!apiKey.isEmpty())
        o.insert(QStringLiteral("api_key"), apiKey);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QStringList parseResponses(const QByteArray &json, int expected)
{
    if (expected <= 0) return {};

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return {};
    const QJsonValue v = doc.object().value(QStringLiteral("translatedText"));

    QStringList out;
    if (v.isArray()) {
        for (const QJsonValue &e : v.toArray()) {
            if (!e.isString()) return {};
            out << e.toString();
        }
    } else if (v.isString()) {
        // The reply to a scalar q. This client always sends an array, but a
        // proxy that unwraps single-element arrays exists in the wild and one
        // answer to one question is not a shape to refuse.
        out << v.toString();
    } else {
        return {};
    }

    // Exactly as many answers as questions, or nothing: fewer means a row
    // would go unanswered silently, more means the mapping is a guess.
    if (out.size() != expected) return {};
    return out;
}

QString parseError(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return {};
    return doc.object().value(QStringLiteral("error")).toString();
}

} // namespace libre_translate
