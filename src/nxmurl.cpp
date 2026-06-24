#include "nxmurl.h"

#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

std::expected<NxmTarget, QString> parseNxmUrl(const QString &rawUrl)
{
    QUrl qurl(rawUrl);

    // nxms:// is the SSL/CDN variant; treat it as nxm://. Don't drop this
    // branch or CDN downloads fail with "Unknown protocol: nxms".
    if (qurl.scheme() == QStringLiteral("nxms"))
        qurl.setScheme(QStringLiteral("nxm"));

    if (qurl.scheme() != QStringLiteral("nxm"))
        return std::unexpected(QStringLiteral("invalid-scheme"));

    const QStringList parts = qurl.path().split('/', Qt::SkipEmptyParts);
    if (parts.size() < 4 || parts[0] != QStringLiteral("mods")
                         || parts[2] != QStringLiteral("files"))
        return std::unexpected(QStringLiteral("invalid-path"));

    bool ok1 = false, ok2 = false;
    const int modId  = parts[1].toInt(&ok1);
    const int fileId = parts[3].toInt(&ok2);
    if (!ok1 || !ok2)
        return std::unexpected(QStringLiteral("invalid-ids"));

    const QUrlQuery query(qurl);
    return NxmTarget{
        qurl.host().toLower(),
        modId,
        fileId,
        query.queryItemValue(QStringLiteral("key")),
        query.queryItemValue(QStringLiteral("expires")),
    };
}

std::optional<NexusModRef> parseNexusModUrl(const QString &rawUrl)
{
    // Stored form is /{game}/mods/{modId}: size >= 3, second segment "mods",
    // numeric id.
    const QStringList parts = QUrl(rawUrl).path().split('/', Qt::SkipEmptyParts);
    if (parts.size() < 3 || parts[1] != QStringLiteral("mods"))
        return std::nullopt;
    bool ok = false;
    const int modId = parts[2].toInt(&ok);
    if (!ok)
        return std::nullopt;
    return NexusModRef{parts[0].toLower(), modId};
}

QString nexusModUrl(const QString &game, int modId)
{
    return QStringLiteral("https://www.nexusmods.com/%1/mods/%2")
        .arg(game).arg(modId);
}
