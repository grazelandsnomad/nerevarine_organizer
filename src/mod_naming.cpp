#include "mod_naming.h"

#include <QHash>
#include <QRegularExpression>

namespace mod_naming {

QStringList findStaleSiblings(const QString     &currentFolderName,
                              const QStringList &siblings)
{
    if (currentFolderName.isEmpty() || siblings.isEmpty()) return {};

    // Strip an optional trailing "_<digits>" timestamp suffix so the
    // shape check matches both "<name>-<id>" and "<name>-<id>_<ts>".
    static const QRegularExpression trailSuffix(
        QStringLiteral("_\\d+$"));
    QString prefix = currentFolderName;
    prefix.remove(trailSuffix);

    // Gate: only fire when the stripped prefix looks like a Nexus
    // archive slug.  User-named folders whose name happens to end in
    // "_1234" must NOT trip the cleanup.
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

    const QString folderLc = folderName.toLower();
    return kGenericFolderNames.contains(folderLc)
        || kNexusArchiveShape.match(folderLc).hasMatch()
        || kNexusVersionedArchive.match(folderLc).hasMatch()
        || kGenericPrefixFolder.match(folderLc).hasMatch();
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
