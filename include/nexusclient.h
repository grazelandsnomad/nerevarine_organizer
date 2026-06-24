#ifndef NEXUSCLIENT_H
#define NEXUSCLIENT_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include <expected>

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

    // GET /v1/games/{game}/mods/{modId}/files.json - file listing
    QNetworkReply *requestModFiles(const QString &game, int modId);

    // GET /v1/games/{game}/mods/{modId}/changelogs.json - version history
    QNetworkReply *requestChangelog(const QString &game, int modId);

    // GET /v1/games/{game}/mods/{modId}/files/{fileId}/download_link.json
    // key/expires are the signed nxms:// params; empty for premium "Mod
    // Manager Download".
    QNetworkReply *requestDownloadLink(const QString &game, int modId, int fileId,
                                        const QString &key     = QString(),
                                        const QString &expires = QString());

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
    };
    // Top level must be a JSON object. Missing individual fields aren't an
    // error - they keep their defaults (Nexus omits optional fields).
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
    // Every file in "files", minus any the "file_updates" succession chain
    // marks superseded by a still-present newer file (an author re-uploading
    // without archiving leaves two same-version entries the picker can't tell
    // apart). Order preserved. Zero files -> empty list, not an error.
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

private:
    QNetworkReply         *buildGet(const QString &path);
    QNetworkAccessManager *m_nam = nullptr;
    QString                m_apiKey;
};

#endif // NEXUSCLIENT_H
