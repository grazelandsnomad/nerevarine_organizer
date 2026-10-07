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

// Folder names that are mod data themselves, not a BAIN package: an asset dir
// (install_layout's kDataRootNames plus the Bethesda script-extender dirs), or
// the data root wrapper itself ("Data Files" for Morrowind, "Data" for the
// Bethesda engines). One of these at top level means a plain mod, not a
// package set; one inside a candidate folder means that folder holds data.
bool isDataName(const QString &nameLower)
{
    static const QSet<QString> kDataDirs {
        "textures", "meshes", "splash", "fonts", "sound", "music",
        "icons", "bookart", "mwscript", "video", "shaders", "scripts",
        "grass", "lod", "distantland", "fomod",
        "data files", "data",
        "skse", "f4se", "obse", "nvse", "fose", "sfse",
        "interface", "strings", "materials", "facegen", "menus",
    };
    return kDataDirs.contains(nameLower);
}

// Folders that document a package set rather than belong to it. They ride
// along in the archive and install nothing, as in Wrye Bash.
bool isDocsFolder(const QString &nameLower)
{
    static const QSet<QString> kDocs {
        "docs", "doc", "documentation", "readme", "readmes",
        "screenshots", "screenshot", "screens", "images", "pictures",
    };
    return kDocs.contains(nameLower);
}

// A plugin or asset archive: a file that is mod content on its own.
bool isContentFile(const QString &name)
{
    const QString lower = name.toLower();
    for (const QString &ext : plugins::contentExtensions())
        if (lower.endsWith(ext)) return true;
    return lower.endsWith(QLatin1String(".bsa"))
        || lower.endsWith(QLatin1String(".ba2"));
}

// Does `dir` hold mod data at its top: an asset dir, a Data Files/ wrapper,
// or a plugin / archive file? That is what makes an unnumbered folder a
// package.
bool looksLikeData(const QString &dir)
{
    const QDir d(dir);
    for (const QString &s : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot
                                        | QDir::NoSymLinks))
        if (isDataName(s.toLower())) return true;
    for (const QString &f : d.entryList(QDir::Files))
        if (isContentFile(f)) return true;
    return false;
}

// Numeric value of a package's leading digits, for ordering ("10" > "02" > "1").
int leadingNumber(const QString &name)
{
    int i = 0;
    while (i < name.size() && name[i].isDigit()) ++i;
    return i ? name.left(i).toInt() : 0;
}

// The package folders of `modPath` - numbered ones first in numeric order,
// unnumbered ones after, by name - or an empty list when the layout is not a
// package set.
//
// Wrye Bash's rule, roughly: a "complex" package is an archive whose top level
// holds no data itself, only folders that do. The "00 Core" numbering is a
// convention, not a requirement. So a numbered folder is a package on its
// name alone, an unnumbered folder is one when it holds data, Docs/ and
// Screenshots/ ride along unoffered, and anything else (Tools/, Source/) is
// neither. Two packages make a set; one is nothing to choose from. A plain
// mod - an asset dir, Data Files/, or a plugin at top level - is never a
// package set, whatever sits beside it. Requiring every top-level folder to
// be numbered, as this once did, rejected real archives over a Docs/ folder
// (GitHub issue #2).
QList<Package> scanPackages(const QString &modPath)
{
    QDir d(modPath);
    if (!d.exists()) return {};

    // FOMOD precedence: never offer BAIN when an installer is present.
    if (QFileInfo::exists(d.filePath(QStringLiteral("fomod/ModuleConfig.xml"))))
        return {};

    for (const QString &f : d.entryList(QDir::Files))
        if (isContentFile(f)) return {};            // plain mod

    QList<Package> numbered, plain;
    for (const QString &s : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot
                                        | QDir::NoSymLinks)) {
        const QString lower = s.toLower();
        if (isDataName(lower)) return {};           // plain mod
        if (isDocsFolder(lower)) continue;
        const Package p{s, d.filePath(s)};
        if (numberedRe().match(s).hasMatch())
            numbered.append(p);
        else if (looksLikeData(p.path))
            plain.append(p);
    }
    if (numbered.size() + plain.size() < 2) return {};

    std::stable_sort(numbered.begin(), numbered.end(),
        [](const Package &a, const Package &b) {
            const int na = leadingNumber(a.name), nb = leadingNumber(b.name);
            if (na != nb) return na < nb;
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
    std::stable_sort(plain.begin(), plain.end(),
        [](const Package &a, const Package &b) {
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
    return numbered + plain;
}

} // namespace

bool looksLikeBain(const QString &modPath)
{
    return !scanPackages(modPath).isEmpty();
}

QList<Package> packages(const QString &modPath)
{
    return scanPackages(modPath);
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
