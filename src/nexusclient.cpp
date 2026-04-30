#include "nexusclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr const char *kBaseUrl = "https://api.nexusmods.com";
}

NexusClient::NexusClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

void NexusClient::setNetworkAccessManager(QNetworkAccessManager *nam)
{
    if (nam == m_nam) return;
    if (m_nam && m_nam->parent() == this)
        m_nam->deleteLater();
    m_nam = nam;
    if (m_nam) {
        m_nam->setParent(this);
        m_nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    }
}

QNetworkReply *NexusClient::buildGet(const QString &path)
{
    QNetworkRequest req{QUrl(QString::fromLatin1(kBaseUrl) + path)};
    req.setRawHeader("apikey", m_apiKey.toUtf8());
    req.setRawHeader("Accept", "application/json");
    return m_nam->get(req);
}

QNetworkReply *NexusClient::requestModInfo(const QString &game, int modId)
{
    return buildGet(QString("/v1/games/%1/mods/%2.json").arg(game).arg(modId));
}

QNetworkReply *NexusClient::requestChangelog(const QString &game, int modId)
{
    return buildGet(
        QString("/v1/games/%1/mods/%2/changelogs.json").arg(game).arg(modId));
}

QNetworkReply *NexusClient::requestModFiles(const QString &game, int modId)
{
    return buildGet(QString("/v1/games/%1/mods/%2/files.json").arg(game).arg(modId));
}

QNetworkReply *NexusClient::requestDownloadLink(const QString &game, int modId, int fileId,
                                                 const QString &key, const QString &expires)
{
    QUrl url(QString::fromLatin1(kBaseUrl)
             + QString("/v1/games/%1/mods/%2/files/%3/download_link.json")
                   .arg(game).arg(modId).arg(fileId));
    if (!key.isEmpty() && !expires.isEmpty()) {
        QUrlQuery q;
        q.addQueryItem("key", key);
        q.addQueryItem("expires", expires);
        url.setQuery(q);
    }
    QNetworkRequest req(url);
    req.setRawHeader("apikey", m_apiKey.toUtf8());
    req.setRawHeader("Accept", "application/json");
    return m_nam->get(req);
}

QList<NexusClient::ChangelogEntry> NexusClient::parseChangelog(const QByteArray &json)
{
    const QJsonObject obj = QJsonDocument::fromJson(json).object();
    if (obj.isEmpty()) return {};

    QStringList versions = obj.keys();
    // Sort descending so the newest version appears first in the viewer.
    std::sort(versions.begin(), versions.end(), [](const QString &a, const QString &b) {
        const auto ap = a.split('.', Qt::SkipEmptyParts);
        const auto bp = b.split('.', Qt::SkipEmptyParts);
        for (int i = 0; i < qMax(ap.size(), bp.size()); ++i) {
            const int av = (i < ap.size()) ? ap[i].toInt() : 0;
            const int bv = (i < bp.size()) ? bp[i].toInt() : 0;
            if (av != bv) return av > bv;
        }
        return a > b;
    });

    QList<ChangelogEntry> result;
    result.reserve(versions.size());
    for (const QString &ver : versions) {
        ChangelogEntry entry;
        entry.version = ver;
        for (const QJsonValue &v : obj[ver].toArray())
            entry.changes << v.toString();
        result.append(std::move(entry));
    }
    return result;
}

QString NexusClient::NexusError::toString() const
{
    switch (kind) {
    case Kind::InvalidJson:  return QStringLiteral("InvalidJson");
    case Kind::WrongShape:   return QStringLiteral("WrongShape");
    case Kind::MissingField: return QStringLiteral("MissingField(%1)").arg(detail);
    case Kind::EmptyPayload: return QStringLiteral("EmptyPayload");
    }
    return QStringLiteral("Unknown");
}

std::expected<NexusClient::ModInfo, NexusClient::NexusError>
NexusClient::parseModInfo(const QByteArray &json)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || doc.isNull())
        return std::unexpected(NexusError{NexusError::Kind::InvalidJson, {}});
    if (!doc.isObject())
        return std::unexpected(NexusError{NexusError::Kind::WrongShape, {}});

    const QJsonObject obj = doc.object();
    ModInfo info;
    info.name             = obj["name"].toString().trimmed();
    info.description      = obj["description"].toString();
    info.updatedTimestamp = obj["updated_timestamp"].toInteger();
    return info;
}

std::expected<QList<NexusClient::FileEntry>, NexusClient::NexusError>
NexusClient::parseFilesList(const QByteArray &json)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || doc.isNull())
        return std::unexpected(NexusError{NexusError::Kind::InvalidJson, {}});
    if (!doc.isObject())
        return std::unexpected(NexusError{NexusError::Kind::WrongShape, {}});

    const QJsonValue filesVal = doc.object().value("files");
    if (!filesVal.isArray())
        return std::unexpected(NexusError{NexusError::Kind::MissingField,
                                          QStringLiteral("files")});

    const QJsonArray arr = filesVal.toArray();
    QList<FileEntry> out;
    out.reserve(arr.size());
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        FileEntry f;
        f.fileId    = o["file_id"].toInt();
        f.name      = o["name"].toString();
        f.version   = o["version"].toString();
        f.category  = o["category_name"].toString();
        f.md5       = o["md5"].toString().trimmed().toLower();
        f.sizeBytes = o["size_in_bytes"].toVariant().toLongLong();
        f.sizeKb    = o["size_kb"].toDouble();
        out.append(f);
    }
    return out;
}

std::expected<QString, NexusClient::NexusError>
NexusClient::parseDownloadUri(const QByteArray &json)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || doc.isNull())
        return std::unexpected(NexusError{NexusError::Kind::InvalidJson, {}});
    if (!doc.isArray())
        return std::unexpected(NexusError{NexusError::Kind::WrongShape, {}});

    const QJsonArray arr = doc.array();
    if (arr.isEmpty())
        return std::unexpected(NexusError{NexusError::Kind::EmptyPayload, {}});

    const QString uri = arr.first().toObject()["URI"].toString();
    if (uri.isEmpty())
        return std::unexpected(NexusError{NexusError::Kind::MissingField,
                                          QStringLiteral("URI")});

    return uri;
}
