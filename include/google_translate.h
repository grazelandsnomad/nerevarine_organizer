#ifndef GOOGLE_TRANSLATE_H
#define GOOGLE_TRANSLATE_H

// Google's free, unauthenticated translate endpoint - the one behind the
// editor's "machine translate empty rows" button.
//
// Ported from Nerevarine Scribe (src/web_translator_google.cpp), which shipped
// this as its "first pass" and learned two things the hard way that are not
// obvious from the endpoint alone:
//
//   1. The response is not one string. Long text comes back SPLIT into
//      segments, and every segment's first element has to be concatenated in
//      order. Measured against the live endpoint: a book-length description
//      returned FIVE segments, of which the first held 44 of its 541
//      characters - so reading only the first silently drops 92% of a book
//      and leaves something that still looks like a plausible translation.
//
//   2. Scribe found that Google's edge 403s the bare default Qt user agent
//      and sends a browser one instead. That did NOT reproduce here - the
//      same request with no UA header came back HTTP 200 - so treat it as
//      cheap insurance against an edge that behaves differently, not as a
//      verified requirement. It costs one header either way.
//
// Undocumented and unofficial, so it can change or start refusing traffic
// without notice. Everything here fails soft: a malformed or unexpected
// response yields an empty string, and the caller leaves that row for the
// user to fill in rather than writing a guess into a plugin.
//
// Parsing and URL building are kept free of the network so they can be tested
// without one.

#include <QByteArray>
#include <QString>
#include <QUrl>

namespace google_translate {

// The GET url for one string. `targetIso` is an ISO 639-1 code
// (target_language::isoCode). Source language is left to Google's detection:
// mods are not reliably in the language you assume, and "auto" costs nothing.
QUrl requestUrl(const QString &text, const QString &targetIso);

// The user agent the free endpoint requires - see note 1 above.
QByteArray userAgent();

// The translated text, with all segments concatenated in order. Empty when the
// payload is not the shape this endpoint returns, which the caller must treat
// as "no answer", never as "translates to nothing".
QString parseResponse(const QByteArray &json);

// How many requests to keep in flight at once. The free endpoint starts
// refusing when hit with a whole modlist at once, and a mod with 164 strings
// is an ordinary size here, so the batch is a queue rather than a fan-out.
// Matches the figure Scribe settled on.
constexpr int kMaxInFlight = 5;

} // namespace google_translate

#endif // GOOGLE_TRANSLATE_H
