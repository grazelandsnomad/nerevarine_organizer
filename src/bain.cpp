#include "bain.h"
#include "fomod_copy.h"
#include "pluginparser.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace bain {
namespace {

// BAIN package folder: leading number, optional letter, separator, label
// ("00 Core", "10a Alt Meshes", "02_Patch"). Anchored so "100 Mods Merged"
// qualifies but a plain "Core" doesn't.
const QRegularExpression &numberedRe()
{
    static const QRegularExpression re(QStringLiteral("^\\d+[A-Za-z]?[ ._-]"));
    return re;
}

// Folder names that are an OpenMW data root, not a BAIN package. One of these
// at top level means plain mod data, not a package set. Same as
// install_layout's kDataRootNames.
bool isAssetRoot(const QString &nameLower)
{
    static const QSet<QString> kAssetDirs {
        "textures", "meshes", "splash", "fonts", "sound", "music",
        "icons", "bookart", "mwscript", "video", "shaders", "scripts",
        "grass", "lod", "distantland", "fomod",
    };
    return kAssetDirs.contains(nameLower);
}

// Numeric value of a package's leading digits, for ordering ("10" > "02" > "1").
int leadingNumber(const QString &name)
{
    int i = 0;
    while (i < name.size() && name[i].isDigit()) ++i;
    return i ? name.left(i).toInt() : 0;
}

} // namespace

bool looksLikeBain(const QString &modPath)
{
    QDir d(modPath);
    if (!d.exists()) return false;

    // FOMOD precedence: never offer BAIN when an installer is present.
    if (QFileInfo::exists(d.filePath(QStringLiteral("fomod/ModuleConfig.xml"))))
        return false;

    const QStringList subdirs =
        d.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    if (subdirs.size() < 2) return false;     // need a real choice to be BAIN

    int numbered = 0;
    for (const QString &s : subdirs) {
        if (isAssetRoot(s.toLower())) return false;   // plain data root, not BAIN
        if (numberedRe().match(s).hasMatch()) ++numbered;
    }
    // Require EVERY top-level folder numbered; a mix is some other layout.
    return numbered >= 2 && numbered == subdirs.size();
}

QList<Package> packages(const QString &modPath)
{
    QList<Package> out;
    if (!looksLikeBain(modPath)) return out;

    QDir d(modPath);
    for (const QString &s : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot
                                        | QDir::NoSymLinks)) {
        if (numberedRe().match(s).hasMatch())
            out.append({s, d.filePath(s)});
    }
    std::stable_sort(out.begin(), out.end(),
        [](const Package &a, const Package &b) {
            const int na = leadingNumber(a.name), nb = leadingNumber(b.name);
            if (na != nb) return na < nb;
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
    return out;
}

QString stage(const QString &modPath, const QStringList &chosenNames)
{
    if (chosenNames.isEmpty()) return {};

    const QString stageDir =
        QFileInfo(modPath).absolutePath() + QStringLiteral("/bain_install");
    if (QDir(stageDir).exists())
        QDir(stageDir).removeRecursively();
    QDir().mkpath(stageDir);

    const QSet<QString> chosen(chosenNames.begin(), chosenNames.end());
    // packages() is numeric order, so a higher-numbered package overwrites a
    // lower one (last writer wins, in fomod_copy::copyContents).
    for (const Package &p : packages(modPath)) {
        if (chosen.contains(p.name))
            fomod_copy::copyContents(p.path, stageDir);
    }

    // Every chosen package was empty -> nothing staged.
    QDir sd(stageDir);
    if (sd.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
        sd.removeRecursively();
        return {};
    }
    return stageDir;
}


QStringList foreignMasters(const QList<Package> &packages, int index)
{
    if (index < 0 || index >= packages.size()) return {};

    // Every plugin filename this archive supplies, from ANY package: "00
    // Core"'s .esm is what "03 Patch" is meant to load after, and a plugin is
    // never a master of itself. Counting only the chosen packages would make a
    // patch look foreign to the core it ships beside.
    QSet<QString> own;
    for (const Package &p : packages)
        for (const auto &dir : plugins::collectDataFolders(
                 p.path, plugins::contentExtensions()))
            for (const QString &f : dir.second) own.insert(f.toLower());

    static const QSet<QString> kBaseGame = {
        QStringLiteral("morrowind.esm"), QStringLiteral("tribunal.esm"),
        QStringLiteral("bloodmoon.esm")};

    QStringList out;
    for (const auto &dir : plugins::collectDataFolders(
             packages[index].path, plugins::contentExtensions())) {
        for (const QString &f : dir.second) {
            const QString full = dir.first + QLatin1Char('/') + f;
            for (const QString &m : plugins::readTes3Masters(full)) {
                const QString low = m.toLower();
                if (kBaseGame.contains(low) || own.contains(low)) continue;
                if (!out.contains(m, Qt::CaseInsensitive)) out << m;
            }
        }
    }
    return out;
}

} // namespace bain
