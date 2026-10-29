#include "creations.h"

#include "fs_utils.h"
#include "modroles.h"
#include "placeholder_state.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

namespace creations {

namespace {

const QRegularExpression &uuidRe()
{
    static const QRegularExpression re(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return re;
}

const QRegularExpression &slugRe()
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9]+$"));
    return re;
}

bool isPluginName(const QString &lower)
{
    return lower.endsWith(QLatin1String(".esp")) || lower.endsWith(QLatin1String(".esm"))
        || lower.endsWith(QLatin1String(".esl"));
}

// The site's game segment for each profile id. Both Skyrim profiles share the
// one "skyrim" site.
struct SlugMap { const char *slug; const char *profile; };
constexpr SlugMap kSlugs[] = {
    {"fallout4",  "fallout4"},
    {"skyrim",    "skyrimspecialedition"},
    {"skyrim",    "skyrimanniversaryedition"},
    {"starfield", "starfield"},
};

} // namespace

std::optional<CreationRef> parseCreationUrl(const QString &url)
{
    QString text = url.trimmed();
    if (text.isEmpty()) return std::nullopt;
    if (!text.contains(QLatin1String("://"))) text.prepend(QLatin1String("https://"));
    const QUrl u(text);
    if (!u.isValid()) return std::nullopt;
    const QString scheme = u.scheme().toLower();
    if (scheme != QLatin1String("https") && scheme != QLatin1String("http")) return std::nullopt;
    QString host = u.host().toLower();
    if (host.startsWith(QLatin1String("www."))) host.remove(0, 4);
    if (host != QLatin1String("creations.bethesda.net")) return std::nullopt;

    const QStringList parts = u.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const int details = parts.indexOf(QLatin1String("details"));
    if (details != 1 && details != 2) return std::nullopt;   // [lang]/<game>/details
    if (parts.size() < details + 2) return std::nullopt;
    CreationRef ref;
    ref.gameSlug = parts.at(details - 1).toLower();
    if (!slugRe().match(ref.gameSlug).hasMatch()) return std::nullopt;
    const QString id = parts.at(details + 1);
    if (!uuidRe().match(id).hasMatch()) return std::nullopt;
    ref.id   = id.toLower();
    ref.slug = parts.value(details + 2);
    return ref;
}

QString canonicalUrl(const CreationRef &ref)
{
    QString out = QStringLiteral("https://creations.bethesda.net/en/%1/details/%2")
                      .arg(ref.gameSlug, ref.id);
    if (!ref.slug.isEmpty()) out += QLatin1Char('/') + ref.slug;
    return out;
}

QString provisionalTitle(const CreationRef &ref)
{
    QString title = QUrl::fromPercentEncoding(ref.slug.toUtf8());
    title.replace(QLatin1Char('_'), QLatin1Char(' '));
    title = title.simplified();
    return title.isEmpty() ? ref.id : title;
}

QStringList profileIdsForSlug(const QString &gameSlug)
{
    const QString slug = gameSlug.toLower();
    QStringList out;
    for (const SlugMap &m : kSlugs)
        if (slug == QLatin1String(m.slug)) out << QString::fromLatin1(m.profile);
    return out;
}

QString slugForProfile(const QString &gameId)
{
    for (const SlugMap &m : kSlugs)
        if (gameId == QLatin1String(m.profile)) return QString::fromLatin1(m.slug);
    return {};
}

QList<CatalogEntry> parseContentCatalog(const QByteArray &json)
{
    QList<CatalogEntry> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return out;
    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (!it.value().isObject()) continue;
        const QJsonObject o = it.value().toObject();
        const QJsonArray files = o.value(QLatin1String("Files")).toArray();
        CatalogEntry e;
        e.key = it.key();
        e.id  = e.key.section(QLatin1Char('_'), -1).toLower();
        for (const QJsonValue &f : files) {
            const QString name = f.toString().trimmed();
            if (!name.isEmpty()) e.files << name;
        }
        if (e.files.isEmpty()) continue;
        e.title     = o.value(QLatin1String("Title")).toString();
        e.version   = o.value(QLatin1String("Version")).toString();
        e.size      = o.value(QLatin1String("FilesSize")).toInteger();
        e.timestamp = o.value(QLatin1String("Timestamp")).toInteger();
        out << e;
    }
    return out;
}

QStringList catalogCandidates(const QStringList &localAppDataDirs,
                              const QStringList &myGamesDirs)
{
    QStringList out;
    for (const QStringList *dirs : {&localAppDataDirs, &myGamesDirs})
        for (const QString &d : *dirs)
            if (!d.isEmpty()) out << d + QLatin1String("/ContentCatalog.txt");
    return out;
}

QSet<QString> cccPlugins(const QString &gameRoot)
{
    QSet<QString> out;
    if (gameRoot.isEmpty()) return out;
    const QDir root(gameRoot);
    const QStringList cccs = root.entryList({QStringLiteral("*.ccc")}, QDir::Files, QDir::Name);
    for (const QString &name : cccs) {
        QFile f(root.filePath(name));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
        for (const QString &raw : lines) {
            const QString line = raw.trimmed().toLower();
            if (isPluginName(line)) out.insert(line);
        }
    }
    return out;
}

QHash<QString, DataFile> dataRootFiles(const QString &dataDir)
{
    QHash<QString, DataFile> out;
    if (dataDir.isEmpty()) return out;
    const QFileInfoList infos = QDir(dataDir).entryInfoList(
        QDir::Files | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : infos)
        out.insert(fi.fileName().toLower(), DataFile{fi.fileName(), fi.isSymLink()});
    return out;
}

Plan planAdoption(const QList<CatalogEntry> &catalog,
                  const QHash<QString, RowInfo> &rowsById,
                  const QHash<QString, DataFile> &dataRoot,
                  const QSet<QString> &cccLower,
                  const QHash<QString, QString> &deployedLowerToLabel,
                  const QSet<QString> &ignoredIds)
{
    Plan plan;
    for (const CatalogEntry &entry : catalog) {
        bool creationClub = false;
        bool allPresent   = true;
        bool anySymlink   = false;
        for (const QString &file : entry.files) {
            const QString low = file.toLower();
            if (cccLower.contains(low)) creationClub = true;
            const auto it = dataRoot.constFind(low);
            if (it == dataRoot.cend()) allPresent = false;
            else if (it->symlink) anySymlink = true;
        }
        if (creationClub) continue;   // the game's own, always loaded

        const auto rowIt = rowsById.constFind(entry.id);
        const bool hasRow = rowIt != rowsById.cend();

        // A name one of our mods deploys: never half-move a Creation.
        bool otherMods = false;
        for (const QString &file : entry.files) {
            const QString low = file.toLower();
            const auto dep = deployedLowerToLabel.constFind(low);
            if (dep == deployedLowerToLabel.cend()) continue;
            if (hasRow && dep.value() == rowIt->label) continue;
            plan.skipped << Skipped{entry.title, file, dep.value()};
            otherMods = true;
            break;
        }
        if (otherMods) continue;

        if (!hasRow) {
            if (allPresent && !ignoredIds.contains(entry.id)) plan.unrequested << entry;
            continue;
        }
        const RowInfo &row = rowIt.value();
        if (!row.installed) {
            if (allPresent) plan.adopt << Adoption{row.rowId, entry, false};
            else            plan.notYet << Adoption{row.rowId, entry, false};
            continue;
        }
        // Installed: ours already. Only a fresh download (another version,
        // regular files where our links were) is worth taking over again.
        if (allPresent && !anySymlink && !entry.version.isEmpty()
            && entry.version != row.version)
            plan.adopt << Adoption{row.rowId, entry, true};
    }
    return plan;
}

QString destFolderFor(const QString &modsDir, const QString &title)
{
    QString base = fsutils::sanitizeFolderName(title);
    if (base.isEmpty()) base = QStringLiteral("Creation");
    const QDir mods(modsDir);
    QString name = base;
    for (int n = 2; QFileInfo::exists(mods.filePath(name)); ++n)
        name = base + QLatin1Char('_') + QString::number(n);
    return mods.filePath(name);
}

namespace {

// A streamed copy, so a 700 MB archive reports progress while it goes.
bool copyStreamed(const QString &from, const QString &to, qint64 &done,
                  qint64 total, const MoveProgress &progress, QString &error)
{
    QFile in(from);
    if (!in.open(QIODevice::ReadOnly)) { error = in.errorString(); return false; }
    QFile out(to);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = out.errorString();
        return false;
    }
    QByteArray buf;
    buf.resize(1 << 20);
    while (!in.atEnd()) {
        const qint64 n = in.read(buf.data(), buf.size());
        if (n < 0) { error = in.errorString(); return false; }
        if (out.write(buf.constData(), n) != n) { error = out.errorString(); return false; }
        done += n;
        if (progress) progress(done, total);
    }
    out.close();
    if (QFileInfo(to).size() != QFileInfo(from).size()) {
        error = QStringLiteral("size mismatch after copy");
        return false;
    }
    return true;
}

} // namespace

MoveResult moveCreationFiles(const QString &dataDir, const QStringList &catalogFiles,
                             const QString &destDir, const MoveProgress &progress)
{
    MoveResult result;
    const QHash<QString, DataFile> root = dataRootFiles(dataDir);
    const QDir data(dataDir);

    // Resolve first: a missing or linked file fails before anything moves.
    struct Job { QString from, to, name; qint64 size = 0; };
    QList<Job> jobs;
    qint64 total = 0;
    for (const QString &file : catalogFiles) {
        const auto it = root.constFind(file.toLower());
        if (it == root.cend()) {
            result.failed = file;
            result.error  = QStringLiteral("not in Data");
            return result;
        }
        if (it->symlink) {
            result.failed = it->spelling;
            result.error  = QStringLiteral("a link, not a download");
            return result;
        }
        Job j;
        j.name = it->spelling;
        j.from = data.filePath(j.name);
        j.to   = QDir(destDir).filePath(j.name);
        j.size = QFileInfo(j.from).size();
        total += j.size;
        jobs << j;
    }
    if (!QDir().mkpath(destDir)) {
        result.failed = QFileInfo(destDir).fileName();
        result.error  = QStringLiteral("could not create the folder");
        return result;
    }

    // Phase 1: rename, or copy with the source kept. Phase 2 removes the
    // copied sources, so a failure in phase 1 can put everything back.
    QList<Job> renamed, copied;
    qint64 done = 0;
    auto rollBack = [&] {
        for (const Job &j : std::as_const(copied)) QFile::remove(j.to);
        for (const Job &j : std::as_const(renamed)) {
            QFile::rename(j.to, j.from);
        }
        QDir().rmdir(destDir);   // only when empty
    };
    for (const Job &j : std::as_const(jobs)) {
        if (QFile::exists(j.to)) QFile::remove(j.to);   // a re-adoption overwrites
        if (QFile::rename(j.from, j.to)) {
            renamed << j;
            done += j.size;
            if (progress) progress(done, total);
            continue;
        }
        QString error;
        if (!copyStreamed(j.from, j.to, done, total, progress, error)) {
            QFile::remove(j.to);
            rollBack();
            result.failed = j.name;
            result.error  = error;
            return result;
        }
        copied << j;
    }
    for (const Job &j : std::as_const(copied)) {
        if (!QFile::remove(j.from)) {
            // The copy is whole; leaving the source would make the deploy back
            // it up as the game's. Undo instead, so Data/ is as it was.
            rollBack();
            result.failed = j.name;
            result.error  = QStringLiteral("could not remove the original");
            return result;
        }
    }
    for (const Job &j : std::as_const(jobs)) result.moved << j.name;
    return result;
}

ModEntry waitingRow(const CreationRef &ref, const QString &tooltip)
{
    const QString title = provisionalTitle(ref);
    ModEntry e = ModEntry::named(title);
    e.customName    = title;
    e.nexusUrl      = canonicalUrl(ref);
    e.installStatus = int(InstallStatus::NotInstalled);
    e.checked       = true;
    e.toolTip       = tooltip;
    e.dateAdded     = QDateTime::currentDateTime();
    return e;
}

void applyAdoption(ModEntry &e, const CatalogEntry &entry, const QString &destDir,
                   const QString &url)
{
    if (!entry.title.isEmpty()) e.customName = entry.title;
    placeholder_state::markInstalled(e, destDir);
    if (e.nexusUrl.isEmpty()) e.nexusUrl = url;
    e.creationVersion = entry.version;
    e.toolTip         = destDir;
}

} // namespace creations
