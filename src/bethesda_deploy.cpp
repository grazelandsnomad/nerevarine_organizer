#include "bethesda_deploy.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
                    LinkMethod preferred)
{
    DeployResult res;
    const QDir data(dataDir);
    QHash<QString, int> relToIdx;   // rel -> index in res.manifest.files

    for (const DeploySource &src : sources) {
        const QDir base(src.dir);
        QDirIterator it(src.dir,
                        QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString abs = it.next();
            const QString rel = base.relativeFilePath(abs);
            if (rel.startsWith(QLatin1String(".."))) continue;   // don't escape Data/
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
                        const Manifest &manifest)
{
    UndeployResult res;
    const QDir data(dataDir);

    for (const DeployedFile &f : manifest.files) {
        const QString dst = data.filePath(f.rel);
        if (QFileInfo::exists(dst) || QFileInfo(dst).isSymLink()) {
            if (QFile::remove(dst)) ++res.removed;
            else res.errors << QStringLiteral("remove failed: %1").arg(f.rel);
        }
        if (f.displacedVanilla) {
            const QString bak = QDir(backupDir).filePath(f.rel);
            if (QFileInfo::exists(bak) || QFileInfo(bak).isSymLink()) {
                if (moveFile(bak, dst)) ++res.restored;
                else res.errors << QStringLiteral("restore failed: %1").arg(f.rel);
            }
        }
    }
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
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
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
    return m;
}

} // namespace bethesda_deploy
