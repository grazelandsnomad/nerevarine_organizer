#include "nxmurl.h"

#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

std::expected<NxmTarget, QString> parseNxmUrl(const QString &rawUrl)
{
    QUrl qurl(rawUrl);

    // Normalise nxms:// (SSL/CDN variant) to nxm:// so the rest of the
    // parsing is uniform.  See header comment - do not drop this branch.
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
