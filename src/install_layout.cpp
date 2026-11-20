#include "install_layout.h"

#include "fs_utils.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace install_layout {

QString folderFor(const QString &modsDir, const QString &name, const QString &fallback)
{
    QString base = fsutils::sanitizeFolderName(name);
    if (base.isEmpty()) base = fsutils::sanitizeFolderName(fallback);
    if (base.isEmpty()) return {};
    const QDir mods(modsDir);
    QString    out = base;
    for (int n = 2; QFileInfo::exists(mods.filePath(out)); ++n)
        out = base + QLatin1Char('_') + QString::number(n);
    return mods.filePath(out);
}

bool isDataContainerName(const QString &folderName)
{
    const QString n = folderName.trimmed().toLower();
    return n == QLatin1String("data") || n == QLatin1String("data files");
}

bool isDataRootName(const QString &folderName)
{
    // Folder names that ARE the data root, not a wrapper around one.
    // Lowercase; caller normalises before lookup.
    static const QSet<QString> kDataRootNames{
        // OpenMW asset dirs - live directly under data=.
        "textures",
        "meshes",
        "splash",
        "fonts",
        "sound",
        "music",
        "icons",
        "bookart",
        "mwscript",
        "video",
        "shaders",
        "scripts",
        "grass",
        "lod",
        "distantland",
        // Diving past fomod/ pushes it to <modPath>/fomod/fomod/… and
        // FomodWizard::hasFomod() then can't find ModuleConfig.xml.
        "fomod",
        // Bethesda Data/ subfolders. Same rule, different engine: these sit
        // directly under Data/, so diving into one buries the payload a level
        // deep and the deploy writes Data/<contents> instead of
        // Data/<folder>/<contents>.
        //
        // The script-extender dirs are the ones this actually bit. Address
        // Library ships as nothing but SKSE/Plugins/versionlib-*.bin, so the
        // whole archive is a single top-level SKSE/ - it got dived into, and
        // the files landed in Data/Plugins/ where no SKSE plugin looks. The
        // game then fails with "Failed to locate an appropriate address
        // library", which reads like a missing download rather than a
        // misplaced one.
        "skse",
        "f4se",
        "obse",
        "nvse",
        "fose",
        "sfse",
        "interface",
        "strings",
        "seq",
        "materials",
        "shadersfx",
        "lodsettings",
        "dialogueviews",
        "source",
        "facegen",
        "menus",
        "trees",
        "distantlod",
        "calientetools",
        "planetdata",
        // Cyberpunk 2077 roots. The game is overlaid whole, so a mod that is
        // nothing but archive/ (nearly all of them) or bin/ (a CET mod) has
        // to keep that folder as its top level, or the overlay lands
        // pc/mod/foo.archive at the game root instead of archive/pc/mod/.
        // "bin" and "mods" are generic words, but a mod for any game that
        // wraps itself in a lone bin/ or mods/ is not a shape seen in the
        // wild, and a wrong dive is the worse outcome.
        "archive",
        "r6",
        "red4ext",
        "engine",
        "bin",
        "mods",
    };
    return kDataRootNames.contains(folderName.toLower());
}

QString deployRoot(const QString &modPath, const QString &modsDir)
{
    const QString parent = QFileInfo(modPath).path();
    if (isDataRootName(QDir(modPath).dirName())
        && !modsDir.isEmpty()
        && QFileInfo(parent) != QFileInfo(modsDir))
        return parent;
    return modPath;
}

QString diveTarget(const QStringList &topSubdirs,
                   const QStringList &topFiles)
{
    if (topSubdirs.size() != 1) return {};
    if (!topFiles.isEmpty())    return {};
    if (isDataRootName(topSubdirs.first())) return {};
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

bool folderHasContent(const QString &path)
{
    if (path.isEmpty()) return false;
    // QDir::isEmpty() reports true for a directory that does not exist, so
    // this one call covers missing, empty and present-with-content.
    //
    // Default filters on purpose: AllEntries | NoDotAndDotDot, which does NOT
    // include Hidden. That is what the two-call probe this replaces used, so a
    // folder holding nothing but a dotfile still reads as not installed.
    // Widening it here would quietly change which rows count as installed,
    // and installed feeds the deploy fingerprint.
    return !QDir(path).isEmpty();
}

} // namespace install_layout
