#ifndef NEXUSCLIENT_H
#define NEXUSCLIENT_H

#include "file_status.h"
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QDateTime>
#include <QNetworkRequest>
#include <QHash>
#include <QString>

#include <expected>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

// Thin wrapper around api.nexusmods.com. Owns its QNetworkAccessManager so
// request construction (base URL, apikey/Accept headers, redirect policy)
// lives in one place and can be mocked for tests.
//
// Response handling stays with the caller: each request returns a
// QNetworkReply* to connect to. The static parsers (parseModInfo /
// parseFilesList / parseDownloadUri) are tested against fixture JSON, no
// network needed.
// What Nexus last said about the allowance, read off the X-RL-* headers it
// puts on every v1 reply. Nexus publishes 20,000 requests per 24 hours,
// dropping to 500 an hour once that is gone, and the app spent years with no
// idea how close it was to either.
//
// Every field is -1 until a reply carries it. That discipline is the point:
// if Nexus renames a header, this reads as "do not know", which is what every
// build before it did, and never as "nothing left" - which would refuse to
// send requests the user is perfectly entitled to.
struct NexusRateLimit {
    int       hourlyLimit = -1, hourlyRemaining = -1;
    int       dailyLimit  = -1, dailyRemaining  = -1;
    QDateTime hourlyReset, dailyReset;   // UTC; invalid == not told
    QDateTime readAt;                    // invalid == never read one
    int       retryAfterSecs = 0;        // from a 429's Retry-After

    [[nodiscard]] bool known() const { return readAt.isValid(); }
    // The one that binds - the smaller of the two that are known - or -1.
    [[nodiscard]] int remaining() const;
    // When the binding one frees up, or an invalid date when not told.
    [[nodiscard]] QDateTime resetsAt() const;
};

class NexusClient : public QObject
{
    // No Q_OBJECT: no signals/slots, QObject only for parent-child cleanup.
    // moc-free so the unit tests link it without AUTOMOC.
public:
    explicit NexusClient(QObject *parent = nullptr);

    void    setApiKey(const QString &key) { m_apiKey = key; }
    QString apiKey() const                { return m_apiKey; }
    bool    hasApiKey() const             { return !m_apiKey.isEmpty(); }

    // Swap the QNetworkAccessManager (tests inject a mock). Takes ownership.
    void setNetworkAccessManager(QNetworkAccessManager *nam);
    QNetworkAccessManager *networkAccessManager() const { return m_nam; }

    // GET /v1/games/{game}/mods/{modId}.json - mod-page metadata
    QNetworkReply *requestModInfo(const QString &game, int modId);

    // GET /v1/games/{game}/mods/updated.json?period=... - every mod of a game
    // that changed in the last 1d, 1w or 1m, in ONE request. What makes a
    // check cost one request instead of one per installed mod; see
    // update_sweep.h for how the answer is used.
    QNetworkReply *requestUpdatedMods(const QString &game, const QString &period);

    // GET /v1/games/{game}/mods/{modId}/files.json - file listing
    QNetworkReply *requestModFiles(const QString &game, int modId);

    // GET /v1/games/{game}/mods/{modId}/changelogs.json - version history
    QNetworkReply *requestChangelog(const QString &game, int modId);
    // The mod page's authored requirements table, from the v2 GraphQL
    // endpoint (api.nexusmods.com/v2/graphql) - public, unauthenticated, and
    // what Nexus's own app runs on. The v1 API does not carry this table at
    // all, which is why the deps dialog used to miss a mod the author lists
    // as "Hard Requirement" but never links in the description.
    // `gameIdNumeric` comes from ModInfo::gameIdNumeric.
    // `offset` pages through tables longer than one page - see kV2PageCap.
    QNetworkReply *requestModRequirements(int gameIdNumeric, int modId,
                                          int offset = 0);

    // GET /v1/games/{game}/mods/{modId}/files/{fileId}/download_link.json
    // key/expires are the signed nxms:// params; empty for premium "Mod
    // Manager Download".
    QNetworkReply *requestDownloadLink(const QString &game, int modId, int fileId,
                                        const QString &key     = QString(),
                                        const QString &expires = QString());

    // GET /v1/users/validate.json - confirms the apikey is accepted and returns
    // the account's name + premium status. Sends only the apikey header; a bad
    // key comes back as HTTP 401 (the reply error), not a parse failure.
    QNetworkReply *requestValidateUser();

    // --- Pure parsers (testable without network) ---
    //
    // Parsers return std::expected<DTO, NexusError>. Distinct failure modes
    // let callers log/surface them instead of collapsing to an empty
    // string/list sentinel - that sentinel pattern hid the nxms:// regression
    // twice.
    //
    // Kind contract:
    //   InvalidJson   - body wasn't JSON at all
    //   WrongShape    - parsed, but top-level wasn't the expected object/array
    //                   (Nexus returns an explanatory object instead of the CDN
    //                   array when mod-manager downloads are disabled - worth
    //                   telling apart from real errors)
    //   MissingField  - key absent or wrong type; `detail` names it ("files", "URI")
    //   EmptyPayload  - shape fine but a required array was empty
    //
    // Empty content inside a valid shape (e.g. a mod with zero files) is a
    // successful parse, not an error.
    struct NexusError {
        enum class Kind {
            InvalidJson,
            WrongShape,
            MissingField,
            EmptyPayload,
        };
        Kind    kind   = Kind::InvalidJson;
        QString detail;   // field name for MissingField, otherwise empty

        // Stable log id: "InvalidJson", "WrongShape", "MissingField(URI)",
        // "EmptyPayload". ASCII-only so logs read the same in any locale.
        QString toString() const;

        friend bool operator==(const NexusError &, const NexusError &) = default;
    };

    struct ModInfo {
        QString name;
        QString description;
        qint64  updatedTimestamp = 0; // seconds since epoch, 0 if missing
        // Nexus's NUMERIC game id ("game_id": 1151 for fallout4). The v2
        // requirements query below is addressed by number, not domain, and
        // this reply already carries it - no lookup table needed.
        int     gameIdNumeric   = 0;
    };
    // Top level must be a JSON object. Missing individual fields aren't an
    // error - they keep their defaults (Nexus omits optional fields).
    static std::expected<ModInfo, NexusError> parseModInfo(const QByteArray &json);

    // The updated.json array, as mod id -> newest activity (epoch seconds).
    // Measured shape, 2026-11-22:
    //   [{"mod_id":42138,"latest_file_update":1795180708,
    //     "latest_mod_activity":1795180761}, ...]
    //
    // nullopt when the body is not that array, which the caller must treat as
    // "learned nothing" and fall back to asking about every mod. An EMPTY
    // array is a real answer - no mod of that game changed - and comes back
    // as an empty map, which is why the two cannot share a return value.
    static std::optional<QHash<int, qint64>> parseUpdatedMods(const QByteArray &json);

    // The v2 endpoint PAGES its lists, and quietly: ask without `count` and
    // you get 20; ask for more and you get at most 80. Measured on the live
    // endpoint, 2026-09-29 - and discovered the hard way, because every
    // caller degrades gracefully when v2 comes up short, so truncation looked
    // exactly like success. Vehicle Overhaul Continued's requirements table
    // has 23 rows and three of them had never been shown; a 158-link deps
    // dialog was naming 20 rows in its "single" request and fetching the
    // other 138 one at a time.
    static constexpr int kV2PageCap = 80;

    // What a paged v2 reply says about the page it came from. `received` is
    // the number of NODES the server sent - not the number of rows a parser
    // kept after dropping unusable ones - because that is what the next
    // page's offset has to advance by. Advancing by kept rows re-requests
    // nodes already seen whenever a row is dropped.
    struct PageInfo {
        int total    = 0;   // totalCount: how many exist in all
        int received = 0;   // nodes on this page
    };

    // Split ids into groups a single v2 request can answer whole. Client-side
    // on purpose: each group then comes back complete in one page, and
    // nothing depends on how the server orders results across offsets.
    static QList<QList<int>> chunkForV2(const QList<int> &ids);

    // One row of the mod page's own "Requirements" table - the authored
    // list, with the author's note ("Hard Requirement. Necessary for Base
    // Object Swapper"). Served by the v2 GraphQL endpoint, which the v1 API
    // never exposed; see requestModRequirements.
    struct Requirement {
        int     modId = 0;
        QString name;
        QString notes;
        bool    external = false;   // off-Nexus URL; kept but not resolvable
    };
    // Display names for up to kV2PageCap mods in one request, from the same
    // v2 endpoint. Callers with more ids split them with chunkForV2 first;
    // anything past the cap here is not sent rather than silently dropped by
    // the server.
    //
    // The dialog that needs this had been asking v1 for one mod at a time -
    // 158 requests to open a single dialog on a mod with a link-heavy
    // description, against a non-premium allowance of about a hundred an
    // hour. `legacyMods(ids: [{gameId, modId}, ...], count: n)` answers them
    // in two.
    QNetworkReply *requestModNames(int gameIdNumeric, const QList<int> &modIds);

    // Every file on one mod page with its category (v2 `modFiles`,
    // unauthenticated, no API quota). Asked by Check Updates only for rows
    // that recorded which file they were installed from.
    QNetworkReply *requestModFilesV2(int gameIdNumeric, int modId);
    // The page's files. A GraphQL error, a null list or garbage is an error,
    // never an empty list: an empty list would read as "the page offers
    // nothing", and file_status must not judge a file on a reply that said
    // nothing.
    // modId -> name. Empty on a GraphQL error, a null payload or garbage, so
    // the caller falls back to naming rows one at a time.
    static QHash<int, QString> parseModNames(const QByteArray &json);
    static std::expected<QList<file_status::PageFile>, NexusError>
    parseModFilesV2(const QByteArray &json);

    // The nexusRequirements rows out of a v2 reply. GraphQL errors, missing
    // fields or a null mod all come back as an error/empty rather than a
    // crash - the caller degrades to the description-only dialog.
    //
    // `page`, when given, receives totalCount and the node count, which is
    // what tells a caller whether another page exists.
    static std::expected<QList<Requirement>, NexusError>
    parseModRequirements(const QByteArray &json, PageInfo *page = nullptr);

    struct ValidatedUser {
        qint64  userId      = 0;
        QString name;
        QString email;
        bool    isPremium   = false;
        bool    isSupporter = false;
    };
    // Top level must be a JSON object carrying "user_id"; a 200 error-envelope
    // without it is MissingField("user_id"), so a rejected key never reads as a
    // valid account. is_premium / is_supporter default to false when absent.
    static std::expected<ValidatedUser, NexusError> parseValidateUser(const QByteArray &json);

    struct FileEntry {
        int     fileId   = 0;
        QString name;
        // The archive's real name on disk ("OAAB Tel Mora-56321-1-0-….7z"),
        // from "file_name". NOT `name`, which is the author's display label
        // ("Main File") and carries no extension. A Nexus CDN link can hand
        // back an opaque id with no name at all, and this is the only place
        // the real one can be recovered from - the mod folder is named after
        // the downloaded file, so without it a mod lands under a bare UUID.
        QString fileName;
        QString version;
        QString category;       // "MAIN", "UPDATE", "PATCH", etc.
        QString md5;            // lowercase
        qint64  sizeBytes = 0;  // from size_in_bytes
        double  sizeKb    = 0;  // from size_kb (for UI display)
        // The author's own words about THIS file, from "description". Often
        // the only thing that explains what a second MAIN file even is, so
        // the picker shows it verbatim. Frequently empty.
        QString description;
        // "is_primary": the file the mod page's own download button hands
        // you. The author's answer to "which one do I want?", and the only
        // field here that ranks the files at all.
        bool    isPrimary = false;
        // The earlier uploads this file replaces, by the page's own record:
        // every old_file_id whose "file_updates" chain ends here, including
        // files no longer listed. Ascending. The one answer to "which of the
        // page's files is the new version of the one I have?" that does not
        // depend on how the author named them.
        QList<int> replaces;
    };
    // Every file in "files", minus any the "file_updates" succession chain
    // marks superseded by a still-present newer file (an author re-uploading
    // without archiving leaves two same-version entries the picker can't tell
    // apart). Order preserved. Each file kept carries the old ids its chain
    // replaces (FileEntry::replaces). Zero files -> empty list, not an error.
    // MissingField("files") only fires when the key is absent or not an array
    // (Nexus error envelopes, HTML interstitials).
    static std::expected<QList<FileEntry>, NexusError>
    parseFilesList(const QByteArray &json);

    struct ChangelogEntry {
        QString     version;
        QStringList changes;
    };
    // Entries newest-first. Unparseable/empty -> empty list (caller shows
    // "no changelog available").
    static QList<ChangelogEntry> parseChangelog(const QByteArray &json);

    // Pulls the download URI out of a download_link.json response.
    static std::expected<QString, NexusError>
    parseDownloadUri(const QByteArray &json);

    // What the last v1 reply said about the allowance. Never read == known()
    // false, which every caller must treat as "carry on".
    // Polled, not signalled: this class has no Q_OBJECT on purpose. The pump
    // reads it between requests and the window reads it when a reply fails.
    [[nodiscard]] const NexusRateLimit &rateLimit() const { return m_rl; }

    // The X-RL-* headers of one reply, as a reading. Pure, so the header
    // names and their absence are testable without a network.
    //
    // The lookup is case-insensitive, and that is not politeness. HTTP/2
    // requires lowercase field names, Qt reports them as the wire carried
    // them, and api.nexusmods.com negotiates HTTP/2 with Qt - measured, with
    // a probe that printed all 16 names of a real reply in lowercase. The
    // first version of this looked them up byte-exact against the
    // documented capitalisation, so in production it matched nothing, every
    // field stayed -1, and the low-allowance banner, the quota floor and the
    // "try again at" in the 429 message were all dead on arrival. The unit
    // tests passed throughout, because they built their headers with the
    // same capitalisation the lookup used.
    //
    // The spellings themselves are still the documented ones; a 401 carries
    // no X-RL headers, so they cannot be read off an unauthenticated reply.
    // A header that is not there stays -1, so a rename degrades to the
    // behaviour of every earlier build.
    static NexusRateLimit parseRateLimit(const QHash<QByteArray, QByteArray> &headers,
                                         const QDateTime &now);

private:
    // Every v1 request goes through here. It is the only kind that spends the
    // allowance and the only kind Nexus answers with X-RL-* headers; the v2
    // GraphQL posts are unauthenticated and deliberately stay out.
    //
    // The reading is taken on a connection made before the caller can make
    // its own, and Qt delivers finished() in connection order, so a caller
    // handling its own reply already sees that reply's numbers.
    QNetworkReply         *sendV1(QNetworkRequest req);
    QNetworkReply         *buildGet(const QString &path);
    void                   noteRateLimit(QNetworkReply *reply);
    QNetworkAccessManager *m_nam = nullptr;
    QString                m_apiKey;
    NexusRateLimit         m_rl;
};

#endif // NEXUSCLIENT_H
