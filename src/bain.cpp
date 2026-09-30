#include "bain.h"
#include "fomod_copy.h"
#include "fomod_path.h"
#include "fs_progress.h"
#include "pluginparser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

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

namespace {

// Move everything inside `src` into `dst`, merging with what is there.
//
// A name `dst` does not have moves over whole: one rename, however much is
// inside it, so a package landing in an empty staging folder costs a handful
// of renames. A folder both have merges one level down. A file both have is
// replaced - the later package wins, as it did when this copied. Names match
// case-insensitively, as fomod::resolveDest matches them. Anything a rename
// cannot move (another filesystem, or a symlink, whose target a copy
// materializes) is copied instead. An entry whose kind clashes with what is
// there - a file where a folder is - is left out, as the copy left it out.
void moveContents(const QString &src, const QString &dst)
{
    const auto entries =
        QDir(src).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.isEmpty()) return;
    QDir().mkpath(dst);
    fomod::DestIndex index(dst);
    for (const QFileInfo &fi : entries) {
        const fomod::ResolvedPath to = index.child(fi.fileName());
        const QString from   = fi.absoluteFilePath();
        const QString toPath = to.str();
        const QFileInfo there(toPath);
        if (fi.isDir()) {
            if (there.isDir()) {
                moveContents(from, toPath);
                continue;
            }
            // An empty folder is skipped, as copying skipped it: an empty
            // scripts/ reads as a failed install (see fomod_copy.h).
            if (there.exists() || QDir(from).isEmpty()) {
                if (there.exists())
                    qWarning("bain: '%s' is a file; not putting a folder there",
                             qUtf8Printable(toPath));
                continue;
            }
            if (fi.isSymLink() || !QDir().rename(from, toPath))
                fomod_copy::copyDir(from, to);
        } else {
            if (there.isDir()) {
                qWarning("bain: '%s' is a folder; not putting a file there",
                         qUtf8Printable(toPath));
                continue;
            }
            if (there.exists()) QFile::remove(toPath);   // last writer wins
            if (fi.isSymLink() || !QFile::rename(from, toPath))
                fomod_copy::copyFile(from, to);
        }
        index.add(there.fileName());
    }
}

} // namespace

QString stage(const QString &modPath, const QStringList &chosenNames,
              FsProgress *progress)
{
    if (chosenNames.isEmpty()) return {};

    // Listed before the staging folder exists, which sits among them.
    const QList<Package> all = packages(modPath);

    // Inside the unpacked archive, so it is on the same filesystem as the
    // packages (a move is a rename) and two installs can never share it - the
    // old <mods>/bain_install was one fixed name for every install at once.
    // Hidden, and promote() carries it out before deleting the archive.
    const QString stageDir = QDir(modPath).filePath(QStringLiteral(".bain_stage"));
    if (QDir(stageDir).exists())
        QDir(stageDir).removeRecursively();
    QDir().mkpath(stageDir);

    const QSet<QString> chosen(chosenNames.begin(), chosenNames.end());
    int picked = 0;
    for (const Package &p : all) picked += chosen.contains(p.name) ? 1 : 0;
    if (progress) progress->begin(FsProgress::Phase::Staging, picked);

    // packages() is numeric order, so a higher-numbered package overwrites a
    // lower one (last writer wins).
    for (const Package &p : all) {
        if (!chosen.contains(p.name)) continue;
        moveContents(p.path, stageDir);
        if (progress) progress->add(1);
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
