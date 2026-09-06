#ifndef LIBRE_TRANSLATE_H
#define LIBRE_TRANSLATE_H

// Talk to a LibreTranslate-compatible server, usually one running on this
// very machine.
//
// Google's free endpoint blocks by IP, the block is sticky - one was measured
// still refusing the first request of a fresh run twelve hours on - and
// nothing client-side can shorten it. Four blocks in three days of ordinary
// use made that plain. Every mitigation in google_translate.h manages the
// scarcity; a local server removes it: no rate limit, no block, no network,
// and it translates a Cyrodiil-sized mod as fast as the machine can go.
//
// The API is LibreTranslate's, chosen over shelling out to a specific engine
// because it is a lingua franca: the reference server (docker one-liner or
// pip), self-hosted instances, and other engines behind the same shape all
// answer it. The app stays a client; which engine runs is the user's business.
//
//     POST <endpoint>/translate
//     {"q": ["Iron Sword", ...], "source": "en", "target": "es",
//      "format": "text", "api_key": "..."}          (api_key only if set)
//  -> {"translatedText": ["Espada de hierro", ...]}
//
// `q` is ALWAYS sent as an array, even for one string, so one parser handles
// every reply - the server answers an array for an array. Errors come back as
// {"error": "..."} with a 4xx/5xx status.
//
// Same rules as google_translate: pure functions, no QNetworkAccessManager
// here, so every shape is testable without a server.

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace libre_translate {

// Between requests. Google gets 350 ms of politeness; a server on this
// machine is happier saturated than trickled, and 25 ms is only there so the
// UI thread breathes between replies.
constexpr int kLocalSpacingMs = 25;

// The /translate endpoint under `base` ("http://localhost:5000"). Tolerant of
// a trailing slash and of the user pasting the full /translate URL already.
QUrl endpointUrl(const QString &base);

// The POST body. `apiKey` empty means the field is omitted entirely - the
// reference server rejects requests carrying an empty api_key when it has
// none configured.
QByteArray requestBody(const QStringList &texts, const QString &targetIso,
                       const QString &apiKey = {});

// The answers, in the order asked, or EMPTY when the reply is not exactly
// `expected` readable answers. The same refuse-rather-than-guess contract as
// google_translate::parseResponses: mapping answers onto the wrong rows is
// the one failure nobody would ever catch.
QStringList parseResponses(const QByteArray &json, int expected);

// The server's own words for what went wrong ({"error": "..."}), or empty.
// Worth surfacing: "Invalid API key" and "Slowdown" name the fix, where a
// bare HTTP status does not.
QString parseError(const QByteArray &json);

} // namespace libre_translate

#endif // LIBRE_TRANSLATE_H
