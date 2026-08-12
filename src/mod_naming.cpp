#include "mod_naming.h"

#include <QHash>
#include <QRegularExpression>

namespace mod_naming {

QStringList findStaleSiblings(const QString     &currentFolderName,
                              const QStringList &siblings)
{
    if (currentFolderName.isEmpty() || siblings.isEmpty()) return {};

    // Drop a trailing "_<digits>" timestamp so the shape check matches both
    // "<name>-<id>" and "<name>-<id>_<ts>".
    static const QRegularExpression trailSuffix(
        QStringLiteral("_\\d+$"));
    QString prefix = currentFolderName;
    prefix.remove(trailSuffix);

    // Only fire when the prefix looks like a Nexus slug. A user folder that
    // happens to end in "_1234" must not trip the cleanup.
    static const QRegularExpression nexusShape(
        QStringLiteral("^.+-\\d+$"));
    if (!nexusShape.match(prefix).hasMatch()) return {};

    const QRegularExpression siblingPat(
        QLatin1String("^") + QRegularExpression::escape(prefix) +
        QLatin1String("(_\\d+)?$"));

    QStringList stale;
    for (const QString &sib : siblings) {
        if (sib == currentFolderName) continue;
        if (siblingPat.match(sib).hasMatch())
            stale << sib;
    }
    return stale;
}

QStringList findOlderVersionSiblings(const QString     &currentFolderName,
                                     const QStringList &siblings,
                                     int                modId)
{
    if (modId <= 0 || currentFolderName.isEmpty() || siblings.isEmpty())
        return {};

    // "-<id>" must be followed by a separator or end of name, so mod 5865 never
    // matches "Foo-58652-1-87-...". The leading "-" keeps a version component
    // from posing as the id.
    const QRegularExpression idPat(
        QLatin1String("-") + QString::number(modId) + QLatin1String("([-_]|$)"));

    QStringList stale;
    for (const QString &sib : siblings) {
        if (sib == currentFolderName) continue;
        if (idPat.match(sib).hasMatch())
            stale << sib;
    }
    return stale;
}

bool folderNameLooksGeneric(const QString &folderName)
{
    static const QStringList kGenericFolderNames = {
        "scripts", "data", "data files", "meshes", "textures", "sounds", "music",
        "bookart", "icons", "splash", "video", "fonts", "main",
        "mygui",
        "00 core",
        "complete pack",
        "open_mw",
        "sm_cv_mask",
        "sm_m_blade",
        "hq",
        "mq",
        "animations",
        "disenchanting",
        "disenchant",
    };
    static const QRegularExpression kNexusArchiveShape(
        QStringLiteral(R"(^(main|complete pack)[-_]\d+([-_]\d+)*$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kNexusVersionedArchive(
        QStringLiteral(R"(^.+[-_]\d+([-_]\d+){3,}$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kGenericPrefixFolder(
        QStringLiteral(R"(^(sound|audio|mesh|fix).*$)"),
        QRegularExpression::CaseInsensitiveOption);
    // Bare CDN UUID (8-4-4-4-12 hex), optionally with the "_<ts>" the
    // extractor appends to dodge the name/dir collision. No readable content,
    // so treat as generic - else the row shows up literally named
    // "4c9017a6-9af8-40b9-acb9-d95d6cff091f" instead of the mod title.
    static const QRegularExpression kBareUuid(
        QStringLiteral(R"(^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}(_\d+)?$)"),
        QRegularExpression::CaseInsensitiveOption);

    const QString folderLc = folderName.toLower();
    return kGenericFolderNames.contains(folderLc)
        || kNexusArchiveShape.match(folderLc).hasMatch()
        || kNexusVersionedArchive.match(folderLc).hasMatch()
        || kGenericPrefixFolder.match(folderLc).hasMatch()
        || kBareUuid.match(folderLc).hasMatch();
}

QString stripTrailingVersionChain(const QString &folderName)
{
    static const QRegularExpression kTrailingVersionChain(
        QStringLiteral(R"([-_]\d+(?:[-_]v?\d+){2,}$)"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = kTrailingVersionChain.match(folderName);
    if (!m.hasMatch()) return {};
    QString cleaned = folderName.left(m.capturedStart()).trimmed();
    return cleaned;
}

QString hardcodedRename(const QString &folderName)
{
    static const QHash<QString, QString> kFolderRenames = {
        { "restock", "(OpenMW 0.49) Restocking" },
    };
    auto it = kFolderRenames.find(folderName.toLower());
    return it == kFolderRenames.end() ? QString() : it.value();
}

} // namespace mod_naming
