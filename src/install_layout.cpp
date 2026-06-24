#include "install_layout.h"

#include <QRegularExpression>
#include <QSet>

namespace install_layout {

QString diveTarget(const QStringList &topSubdirs,
                   const QStringList &topFiles)
{
    if (topSubdirs.size() != 1) return {};
    if (!topFiles.isEmpty())    return {};

    // Folder names that ARE the data root, not a wrapper around one.
    // Lowercase; caller normalises before lookup.
    static const QSet<QString> kDataRootNames {
        // OpenMW asset dirs - live directly under data=.
        "textures", "meshes", "splash", "fonts", "sound", "music",
        "icons", "bookart", "mwscript", "video", "shaders", "scripts",
        "grass", "lod", "distantland",
        // Diving past fomod/ pushes it to <modPath>/fomod/fomod/… and
        // FomodWizard::hasFomod() then can't find ModuleConfig.xml.
        "fomod",
    };

    if (kDataRootNames.contains(topSubdirs.first().toLower()))
        return {};

    return topSubdirs.first();
}

OaabVariant classifyOaabVariant(const QString &fileName)
{
    // "No OAAB" / "Non-OAAB" / "without OAAB", any separator. Checked first
    // so it wins over the bare-OAAB pattern, which it also matches.
    static const QRegularExpression noOaabRe(
        QStringLiteral("\\b(?:no|non|without)[ _-]*oaab\\b"),
        QRegularExpression::CaseInsensitiveOption);
    // OAAB as a whole word; boundaries keep "Oaaberration" out.
    static const QRegularExpression oaabRe(
        QStringLiteral("\\boaab\\b"),
        QRegularExpression::CaseInsensitiveOption);

    if (noOaabRe.match(fileName).hasMatch()) return OaabVariant::NoOaab;
    if (oaabRe.match(fileName).hasMatch())   return OaabVariant::NeedsOaab;
    return OaabVariant::None;
}

} // namespace install_layout
