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
#include <QtGlobal>
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

// How many replies may be outstanding at once.
//
// This is a STALL CAP, not the pacing mechanism - the comment here used to
// claim bounded width was the protection, and a live reproduction disproved
// it. What earns a block is the arrival RATE, and kRequestSpacingMs sets that;
// with one request per tick the rate is the same whatever this number is. The
// cap only matters when the endpoint is slow: it stops replies piling up
// unboundedly before we stop adding more.
//
// Kept at five rather than lowered to one: serialising would make a run's wall
// clock hostage to its slowest single reply, and the PendingDelegate above the
// table is drawn on the premise that several rows spin at once.
constexpr int kMaxInFlight = 5;

// Milliseconds between requests.
//
// The dispatcher was a five-wide burst in which every reply immediately fired
// the next, which on a fast link is tens of requests a second. That is what
// the endpoint answers with HTTP 429 and an "automated queries" page.
// Reproduced live: afterwards every request 429s, and the block is sticky - a
// single fresh request minutes later still 429s - while translate.google.com
// itself keeps answering. So it is this endpoint refusing this traffic
// pattern, not a ban on the address.
//
// 350 ms is a shade under three requests a second, roughly the rate of a
// person pasting into the web page. A 164-string mod - an ordinary size here -
// takes about a minute at this rate instead of about seven seconds. That is
// the trade: a minute of waiting, or fifteen minutes of block.
constexpr int kRequestSpacingMs = 350;

// -- What actually went wrong ------------------------------------------
//
// The caller used to collapse every outcome into "came back empty", which is
// the one thing that is never true. A 429 is a temporary block, a DNS failure
// is a machine with no network, and a clean 200 that parses to nothing is the
// endpoint having changed shape. They need different sentences, and exactly
// one of them should stop the run.
enum class Failure {
    Ok,           // a translation came back
    Blocked,      // HTTP 429: back off, and stay off
    Refused,      // HTTP 403/401: the endpoint is turning this client away
    Offline,      // no HTTP answer at all - DNS, no route, TLS, timeout
    BadResponse,  // HTTP was fine, the body was not the shape parseResponse reads
    HttpError,    // any other non-2xx
};

// Classify one finished request. `netError` is QNetworkReply::error() as an int
// (0 == NoError), `httpStatus` is HttpStatusCodeAttribute (0 when no HTTP
// answer arrived at all), `gotText` is whether parseResponse returned anything.
//
// Plain ints rather than the Qt enums on purpose: this file is compiled into a
// test target that does not link Qt6::Network. The HTTP status is the
// authoritative signal anyway - how Qt maps 429 onto its own error enum is not
// something worth depending on.
Failure classify(int netError, int httpStatus, bool gotText);

// How a whole run went, so it can say one true thing at the end.
struct FailureTally {
    int ok = 0, blocked = 0, refused = 0, offline = 0,
        badResponse = 0, httpError = 0;
    void count(Failure f);
    int  failed() const { return blocked + refused + offline
                               + badResponse + httpError; }
};

// The one outcome worth reporting, by severity:
//   Blocked > Refused > Offline > HttpError > BadResponse > Ok
// A single 429 among a hundred successes is still a blocked run - that is the
// fact which changes what the user should do next. Refused outranks Offline
// because a 403 is an ANSWER: the connection provably worked, so telling
// somebody to check their network would be a lie.
Failure worstOf(const FailureTally &tally);

// -- Cooling off after a block -----------------------------------------

// How long to stay off the endpoint once it has answered 429. Measured: the
// block is sticky, so asking again straight away only re-earns it. Long enough
// that it has plausibly lapsed, short enough that finishing the mod today is
// still on the table.
constexpr int kBlockCooloffMinutes = 15;

// Seconds still to wait. Epoch seconds rather than QDateTime so a test can
// drive "now" instead of sleeping a quarter of an hour.
//
// A stamp from the FUTURE returns 0, not the full wait: a clock that jumps
// forward and is then corrected would otherwise lock machine translation out
// permanently, with no UI anywhere to clear it. This fails open on purpose.
int cooloffSecondsLeft(qint64 blockedAtEpochSec, qint64 nowEpochSec);

} // namespace google_translate

#endif // GOOGLE_TRANSLATE_H
