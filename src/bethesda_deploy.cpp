#include "bethesda_deploy.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

#include <unistd.h>   // ::link; Qt has no hardlink primitive

namespace bethesda_deploy {

namespace {

QString methodToStr(LinkMethod m)
{
    switch (m) {
    case LinkMethod::Hardlink: return QStringLiteral("hardlink");
    case LinkMethod::Symlink:  return QStringLiteral("symlink");
    case LinkMethod::Copy:     return QStringLiteral("copy");
    }
    return QStringLiteral("copy");
}

LinkMethod methodFromStr(const QString &s)
{
    if (s == QLatin1String("hardlink")) return LinkMethod::Hardlink;
    if (s == QLatin1String("symlink"))  return LinkMethod::Symlink;
    return LinkMethod::Copy;
}

// Place src -> dst with one method. dst's parent must exist and dst must not
// (link/copy/symlink all fail on an existing target).
bool tryMethod(const QString &src, const QString &dst, LinkMethod m)
{
    switch (m) {
    case LinkMethod::Hardlink:
        return ::link(QFile::encodeName(src).constData(),
                      QFile::encodeName(dst).constData()) == 0;
    case LinkMethod::Symlink:
        return QFile::link(src, dst);     // symlink on Unix
    case LinkMethod::Copy:
        return QFile::copy(src, dst);
    }
    return false;
}

// Try `preferred`, falling back Hardlink -> Symlink -> Copy. Copy ends the
// ladder.
bool placeFile(const QString &src, const QString &dst,
               LinkMethod preferred, LinkMethod &used)
{
    QList<LinkMethod> ladder;
    switch (preferred) {
    case LinkMethod::Hardlink: ladder = {LinkMethod::Hardlink, LinkMethod::Symlink, LinkMethod::Copy}; break;
    case LinkMethod::Symlink:  ladder = {LinkMethod::Symlink, LinkMethod::Copy}; break;
    case LinkMethod::Copy:     ladder = {LinkMethod::Copy}; break;
    }
    for (LinkMethod m : ladder) {
        if (tryMethod(src, dst, m)) { used = m; return true; }
    }
    return false;
}

// One spelling per path under a deploy root.
//
// The game reads its folder the way Windows does, without case, and Proton
// finds a path one component at a time: on a Linux disk, which keeps case,
// one mod's "textures/" and another's "Textures/" are two folders, and
// whatever sits in the one Proton does not walk into is never seen. Fallout
// 4's Data/ had F4SE and f4se, Meshes and meshes, and three more pairs; a
// translation that spelled its plugin's name differently landed beside the
// original instead of replacing it.
//
// canonical() spells each component as the root already does - an entry
// that is there in another case keeps its spelling, the first in name order
// if two are - and remembers each new one, so the next mod that spells it
// otherwise lands in the same place. Each folder is listed once, lazily.
class CaseFold
{
public:
    explicit CaseFold(const QString &root) : m_root(root) {}

    QString canonical(const QString &rel)
    {
        const QStringList parts = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString actualDir, foldedDir;
        QStringList out;
        out.reserve(parts.size());
        for (const QString &part : parts) {
            QHash<QString, QString> &kids = childrenOf(foldedDir, actualDir);
            const QString folded = part.toCaseFolded();
            auto it = kids.constFind(folded);
            if (it == kids.cend()) it = kids.insert(folded, part);
            out << it.value();
            actualDir = actualDir.isEmpty() ? it.value()
                                            : actualDir + QLatin1Char('/') + it.value();
            foldedDir = foldedDir.isEmpty() ? folded : foldedDir + QLatin1Char('/') + folded;
        }
        return out.join(QLatin1Char('/'));
    }

private:
    QHash<QString, QString> &childrenOf(const QString &foldedDir, const QString &actualDir)
    {
        auto it = m_children.find(foldedDir);
        if (it != m_children.end()) return it.value();
        QHash<QString, QString> kids;
        const QDir dir(actualDir.isEmpty() ? m_root : QDir(m_root).filePath(actualDir));
        // Name order, so of two twins already there the same one always wins.
        const QStringList names = dir.entryList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
            QDir::Name);
        for (const QString &n : names) {
            const QString folded = n.toCaseFolded();
            if (!kids.contains(folded)) kids.insert(folded, n);
        }
        return m_children.insert(foldedDir, kids).value();
    }

    QString m_root;
    QHash<QString, QHash<QString, QString>> m_children;  // folded dir -> folded name -> name
};

// The folders that held the files an undeploy took out, emptied by it: gone
// too, deepest first, up to (never including) the root. Left standing, an old
// deployment's "meshes" beside the game's "Meshes" kept the spellings apart
// for the next deploy. QDir::rmdir only ever removes an empty folder, so one
// that still holds anything - a restored original, a file of the user's - is
// left as it is.
void pruneEmptyDirs(const QString &root, const QSet<QString> &dirs)
{
    const QString cleanRoot = QDir::cleanPath(root);
    QStringList todo(dirs.cbegin(), dirs.cend());
    std::sort(todo.begin(), todo.end(), [](const QString &a, const QString &b) {
        return a.size() > b.size();
    });
    for (QString d : std::as_const(todo)) {
        d = QDir::cleanPath(d);
        while (d.startsWith(cleanRoot + QLatin1Char('/'))) {
            if (!QDir().rmdir(d)) break;
            d = QFileInfo(d).path();
        }
    }
}

// Move `from` to `to`, crossing filesystems via copy+remove if rename fails.
bool moveFile(const QString &from, const QString &to)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (QFile::exists(to)) QFile::remove(to);
    if (QFile::rename(from, to)) return true;
    if (QFile::copy(from, to)) { QFile::remove(from); return true; }
    return false;
}

} // namespace

DeployResult deploy(const QString &dataDir,
                    const QString &backupDir,
                    const QList<DeploySource> &sources,
                    LinkMethod preferred,
                    const ProgressFn &progress)
{
    DeployResult res;
    const QDir data(dataDir);
    QHash<QString, int> relToIdx;   // rel -> index in res.manifest.files
    CaseFold fold(dataDir);         // one spelling per path, so rel is one key

    // Count first so the percentage is real. Walking the sources is pure
    // directory traversal - milliseconds against the thousands of mkpath +
    // hardlink calls below - and a bar that cannot say how far along it is
    // barely beats no bar at all.
    int total = 0;
    if (progress) {
        for (const DeploySource &src : sources) {
            if (!src.onlyFiles.isEmpty()) { total += int(src.onlyFiles.size()); continue; }
            QDirIterator cit(src.dir,
                             QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                             QDirIterator::Subdirectories);
            while (cit.hasNext()) { cit.next(); ++total; }
        }
        progress(0, total);
    }
    // Report every 64 files, not every file: each callback hops a value onto
    // the UI thread, and a 7000-file list would post 7000 events to move a bar
    // by fractions of a pixel.
    constexpr int kReportEvery = 64;
    int seen = 0;

    for (const DeploySource &src : sources) {
        const QDir base(src.dir);

        // Either the named files or everything under `dir`. Materialised
        // rather than iterated lazily so both cases share one loop body; the
        // cost is a QString per file, which is nothing beside linking them.
        QStringList absFiles;
        if (!src.onlyFiles.isEmpty()) {
            for (const QString &name : src.onlyFiles) {
                const QString abs = base.filePath(name);
                if (QFileInfo::exists(abs)) absFiles << abs;
            }
        } else {
            QDirIterator it(src.dir,
                            QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) absFiles << it.next();
        }

        for (const QString &abs : absFiles) {
            if (progress && (++seen % kReportEvery) == 0) progress(seen, total);
            QString rel = base.relativeFilePath(abs);
            if (rel.startsWith(QLatin1String(".."))) continue;   // don't escape Data/
            if (!src.destSubdir.isEmpty()) rel = src.destSubdir + QLatin1Char('/') + rel;
            // As Data/ already spells it: a mod's "textures/" joins the
            // game's "Textures/", and a file another mod placed in another
            // case is the same file, which this one now wins.
            rel = fold.canonical(rel);
            const QString dst = data.filePath(rel);
            QDir().mkpath(QFileInfo(dst).absolutePath());

            const auto existing = relToIdx.constFind(rel);
            if (existing != relToIdx.constEnd()) {
                // Already deployed this rel earlier in load order; later mod
                // wins. Relink, keeping the original displacedVanilla flag.
                if (QFileInfo::exists(dst) || QFileInfo(dst).isSymLink())
                    QFile::remove(dst);
                LinkMethod used;
                if (!placeFile(abs, dst, preferred, used)) {
                    res.errors << QStringLiteral("deploy failed: %1").arg(rel);
                    continue;
                }
                DeployedFile &f = res.manifest.files[existing.value()];
                f.sourceMod = src.label;
                f.method    = used;
                continue;
            }

            // First touch of this rel: anything already there is vanilla/
            // pre-existing, back it up before overwriting.
            bool displaced = false;
            if (QFileInfo::exists(dst) || QFileInfo(dst).isSymLink()) {
                const QString bak = QDir(backupDir).filePath(rel);
                if (!moveFile(dst, bak)) {
                    res.errors << QStringLiteral("backup failed: %1").arg(rel);
                    continue;
                }
                displaced = true;
                ++res.vanillaBackedUp;
            }

            LinkMethod used;
            if (!placeFile(abs, dst, preferred, used)) {
                res.errors << QStringLiteral("deploy failed: %1").arg(rel);
                // Displaced a vanilla file then failed to place ours: restore
                // the original so Data/ isn't left with a hole.
                if (displaced) {
                    moveFile(QDir(backupDir).filePath(rel), dst);
                    --res.vanillaBackedUp;
                }
                continue;
            }

            res.manifest.files.append({rel, src.label, used, displaced});
            relToIdx.insert(rel, res.manifest.files.size() - 1);
        }
    }

    res.filesDeployed = res.manifest.files.size();
    return res;
}

UndeployResult undeploy(const QString &dataDir,
                        const QString &backupDir,
                        const Manifest &manifest,
                        const ProgressFn &progress)
{
    UndeployResult res;
    const QDir data(dataDir);
    // The manifest already knows the total, so no counting pass here.
    constexpr int kReportEvery = 64;
    int seen = 0;
    QSet<QString> emptied;   // folders a removal may have left empty

    for (const DeployedFile &f : manifest.files) {
        if (progress && (++seen % kReportEvery) == 0)
            progress(seen, int(manifest.files.size()));
        const QString dst = data.filePath(f.rel);
        if (QFileInfo::exists(dst) || QFileInfo(dst).isSymLink()) {
            if (QFile::remove(dst)) {
                ++res.removed;
                emptied.insert(QFileInfo(dst).absolutePath());
            } else {
                res.errors << QStringLiteral("remove failed: %1").arg(f.rel);
            }
        }
        if (f.displacedVanilla) {
            const QString bak = QDir(backupDir).filePath(f.rel);
            if (QFileInfo::exists(bak) || QFileInfo(bak).isSymLink()) {
                if (moveFile(bak, dst)) ++res.restored;
                else res.errors << QStringLiteral("restore failed: %1").arg(f.rel);
            }
        }
    }
    pruneEmptyDirs(dataDir, emptied);
    return res;
}

QString manifestToJson(const Manifest &m)
{
    QJsonArray arr;
    for (const DeployedFile &f : m.files) {
        QJsonObject o;
        o.insert(QStringLiteral("rel"),    f.rel);
        o.insert(QStringLiteral("mod"),    f.sourceMod);
        o.insert(QStringLiteral("method"), methodToStr(f.method));
        if (f.displacedVanilla) o.insert(QStringLiteral("vanilla"), true);
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"),  QStringLiteral("nerevarine_deploy_manifest"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("files"),   arr);
    // Additive, so a manifest written before this existed still reads back
    // fine - it just reports no overrides to take away.
    if (!m.dllOverrides.isEmpty())
        root.insert(QStringLiteral("dll_overrides"),
                    QJsonArray::fromStringList(m.dllOverrides));
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString sourceFingerprint(const QStringList &mods, const QStringList &loadOrder)
{
    // Unit and record separators: neither can occur in a path or a plugin
    // name, so no two different inputs join to the same bytes.
    // The version leads: a deploy that now lays files out differently - one
    // spelling per path since 2 - is stale against every older one, and the
    // next Start or deploy redoes it.
    const QString joined = QStringLiteral("deploy-v2") + QChar(0x1f)
                         + mods.join(QChar(0x1e)) + QChar(0x1f)
                         + loadOrder.join(QChar(0x1e));
    return QString::fromLatin1(
        QCryptographicHash::hash(joined.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString missingGameExe(const QString &gameRoot, const QStringList &exeNames)
{
    const QDir root(gameRoot);
    QString first;
    for (const QString &rel : exeNames) {
        if (rel.isEmpty()) continue;
        if (first.isEmpty()) first = rel;
        const QFileInfo fi(root.filePath(rel));
        if (fi.isFile()) return {};
        // The same name in another case: a Linux disk keeps case, and the
        // game asks for its files as Windows does, without it.
        const QDir dir = fi.dir();
        if (!dir.exists()) continue;
        const QStringList names = dir.entryList(QDir::Files | QDir::Hidden);
        for (const QString &n : names)
            if (n.compare(fi.fileName(), Qt::CaseInsensitive) == 0) return {};
    }
    return first;
}

GameLocation followMovedGame(const QString &savedData, const QString &foundData,
                             const QStringList &exeNames, const QString &rootFromData)
{
    const auto root = [&](const QString &data) {
        return QDir::cleanPath(data + QLatin1Char('/') + rootFromData);
    };
    if (savedData.isEmpty()) return {foundData, {}};
    if (missingGameExe(root(savedData), exeNames).isEmpty()) return {savedData, {}};
    if (foundData.isEmpty()
        || QDir::cleanPath(foundData) == QDir::cleanPath(savedData)
        || !missingGameExe(root(foundData), exeNames).isEmpty())
        return {savedData, {}};
    return {foundData, savedData};
}

Manifest manifestFromJson(const QString &json)
{
    Manifest m;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return m;
    const QJsonArray arr = doc.object().value(QStringLiteral("files")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        DeployedFile f;
        f.rel              = o.value(QStringLiteral("rel")).toString();
        f.sourceMod        = o.value(QStringLiteral("mod")).toString();
        f.method           = methodFromStr(o.value(QStringLiteral("method")).toString());
        f.displacedVanilla = o.value(QStringLiteral("vanilla")).toBool(false);
        if (!f.rel.isEmpty()) m.files.append(f);
    }
    for (const QJsonValue &v : doc.object().value(QStringLiteral("dll_overrides")).toArray())
        if (!v.toString().isEmpty()) m.dllOverrides << v.toString();
    return m;
}

} // namespace bethesda_deploy
