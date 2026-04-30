#ifndef NEXUSCLIENT_H
#define NEXUSCLIENT_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include <expected>

class QNetworkAccessManager;
class QNetworkReply;

// Thin wrapper around api.nexusmods.com. Owns its own QNetworkAccessManager
// so the request construction (base URL, apikey header, Accept header,
// redirect policy) is centralised in one place and can be mocked for tests.
//
// Response handling is still the caller's responsibility: each request
// returns a QNetworkReply* that the caller connects to. The pure parsing
// helpers (parseModInfo / parseFilesList / parseDownloadUri) are static
// and unit-tested directly against fixture JSON - no network required.
class NexusClient : public QObject
{
    // Intentionally no Q_OBJECT: this class exposes no signals/slots and
    // inherits QObject only for parent-child memory management.  Keeping it
    // moc-free means the unit tests can link it without pulling AUTOMOC into
    // their build graph.
public:
    explicit NexusClient(QObject *parent = nullptr);

    void    setApiKey(const QString &key) { m_apiKey = key; }
    QString apiKey() const                { return m_apiKey; }
    bool    hasApiKey() const             { return !m_apiKey.isEmpty(); }

    // Swap the QNetworkAccessManager (useful for tests that inject a mock).
    // Takes ownership of the passed manager.
    void setNetworkAccessManager(QNetworkAccessManager *nam);
    QNetworkAccessManager *networkAccessManager() const { return m_nam; }

    // GET /v1/games/{game}/mods/{modId}.json - mod-page metadata
    QNetworkReply *requestModInfo(const QString &game, int modId);

    // GET /v1/games/{game}/mods/{modId}/files.json - file listing
    QNetworkReply *requestModFiles(const QString &game, int modId);

    // GET /v1/games/{game}/mods/{modId}/changelogs.json - version history
    QNetworkReply *requestChangelog(const QString &game, int modId);

    // GET /v1/games/{game}/mods/{modId}/files/{fileId}/download_link.json
    // key/expires are the signed nxms:// params; empty when the user clicks
    // "Mod Manager Download" from a premium account.
    QNetworkReply *requestDownloadLink(const QString &game, int modId, int fileId,
                                        const QString &key     = QString(),
                                        const QString &expires = QString());

    // --- Pure parsers (testable without network) ---
    //
    // Parse outcomes flow through std::expected<DTO, NexusError>.  The error
    // struct distinguishes a handful of structural failure modes so callers
    // can log or surface them meaningfully instead of collapsing every
    // problem into an empty string / empty list sentinel - that's the exact
    // pattern that let the nxms:// regression go undetected twice.
    //
    // Kind contract:
    //   InvalidJson   - body didn't parse as JSON at all
    //   WrongShape    - parsed, but the top-level wasn't the expected
    //                   object/array (Nexus replies with an explanatory
    //                   object instead of the CDN array when mod-manager
    //                   downloads are disabled for that mod - this is the
    //                   one most worth telling apart from real errors)
    //   MissingField  - expected key absent or wrong type; `detail` names
    //                   the key ("files", "URI", …)
    //   EmptyPayload  - structure was fine but a required array was empty
    //
    // Empty CONTENT inside a valid shape (e.g. a mod with zero files) is a
    // successful parse - NOT an error - since that's a real real-world state.
    struct NexusError {
        enum class Kind {
            InvalidJson,
            WrongShape,
            MissingField,
            EmptyPayload,
        };
        Kind    kind   = Kind::InvalidJson;
        QString detail;   // field name for MissingField, otherwise empty

        // Short stable identifier for logging: "InvalidJson", "WrongShape",
        // "MissingField(URI)", "EmptyPayload".  Intentionally ASCII-only so
        // it reads the same in log files regardless of locale.
        QString toString() const;

        friend bool operator==(const NexusError &, const NexusError &) = default;
    };

    struct ModInfo {
        QString name;
        QString description;
        qint64  updatedTimestamp = 0; // seconds since epoch, 0 if missing
    };
    // Requires the top level to be a JSON object.  Missing individual
    // fields within a valid object are NOT an error - they stay at their
    // defaults.  This mirrors Nexus's habit of omitting optional fields.
    static std::expected<ModInfo, NexusError> parseModInfo(const QByteArray &json);

    struct FileEntry {
        int     fileId   = 0;
        QString name;
        QString version;
        QString category;       // "MAIN", "UPDATE", "PATCH", etc.
        QString md5;            // lowercase
        qint64  sizeBytes = 0;  // from size_in_bytes
        double  sizeKb    = 0;  // from size_kb (for UI display)
    };
    // Every file in the "files" array. Order preserved.  A mod with zero
    // files parses to an empty list, NOT an error - that's a real state.
    // MissingField("files") fires only when the key is absent or isn't an
    // array at all (Nexus error envelopes, HTML interstitials, …).
    static std::expected<QList<FileEntry>, NexusError>
    parseFilesList(const QByteArray &json);

    struct ChangelogEntry {
        QString     version;
        QStringList changes;
    };
    // Returns entries sorted newest-first.  An unparseable or empty response
    // returns an empty list (caller shows "no changelog available").
    static QList<ChangelogEntry> parseChangelog(const QByteArray &json);

    // Extracts the download URI from a download_link.json response.
    // Success: a non-empty URI string.
    static std::expected<QString, NexusError>
    parseDownloadUri(const QByteArray &json);

private:
    QNetworkReply         *buildGet(const QString &path);
    QNetworkAccessManager *m_nam = nullptr;
    QString                m_apiKey;
};

#endif // NEXUSCLIENT_H
