#include "scan_cache.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace scan_cache {

namespace {
constexpr int kVersion = 1;
}

Cache parse(const QByteArray &json)
{
    Cache out;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return out;
    const QJsonObject root = doc.object();
    // A future format is not a format this build understands; an empty cache
    // just means the first scan of the session does the work.
    if (root.value(QStringLiteral("version")).toInt() != kVersion) return out;

    const QJsonObject mods = root.value(QStringLiteral("mods")).toObject();
    for (auto it = mods.constBegin(); it != mods.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        Entry e;
        for (const QJsonValue &dv : o.value(QStringLiteral("dataFolders")).toArray()) {
            const QJsonObject d = dv.toObject();
            QStringList files;
            for (const QJsonValue &f : d.value(QStringLiteral("files")).toArray())
                files << f.toString();
            const QString dir = d.value(QStringLiteral("dir")).toString();
            if (!dir.isEmpty()) e.dataFolders.append({dir, files});
        }
        for (const QJsonValue &dv : o.value(QStringLiteral("dirs")).toArray()) {
            const QJsonObject d = dv.toObject();
            const QString dir = d.value(QStringLiteral("dir")).toString();
            if (!dir.isEmpty())
                e.dirs.append({dir, qint64(d.value(QStringLiteral("mtime")).toDouble())});
        }
        e.hasContent = o.value(QStringLiteral("hasContent")).toBool();
        // An entry with nothing to check cannot be trusted later, so it is not
        // worth keeping.
        if (!e.dirs.isEmpty()) out.mods.insert(it.key(), e);
    }

    for (const QJsonValue &mv : root.value(QStringLiteral("masters")).toArray()) {
        const QJsonObject m = mv.toObject();
        const QString file = m.value(QStringLiteral("file")).toString();
        if (file.isEmpty()) continue;
        QStringList parents;
        for (const QJsonValue &p : m.value(QStringLiteral("parents")).toArray())
            parents << p.toString();
        out.masters.insert(file,
            {qint64(m.value(QStringLiteral("mtime")).toDouble()), parents});
    }
    return out;
}

QByteArray serialize(const Cache &cache)
{
    QJsonObject mods;
    for (auto it = cache.mods.constBegin(); it != cache.mods.constEnd(); ++it) {
        const Entry &e = it.value();
        if (e.dirs.isEmpty()) continue;
        QJsonArray dataFolders;
        for (const auto &p : e.dataFolders) {
            QJsonArray files;
            for (const QString &f : p.second) files.append(f);
            dataFolders.append(QJsonObject{{QStringLiteral("dir"), p.first},
                                           {QStringLiteral("files"), files}});
        }
        QJsonArray dirs;
        for (const auto &p : e.dirs)
            dirs.append(QJsonObject{{QStringLiteral("dir"), p.first},
                                    {QStringLiteral("mtime"), double(p.second)}});
        mods.insert(it.key(), QJsonObject{{QStringLiteral("dataFolders"), dataFolders},
                                          {QStringLiteral("dirs"), dirs},
                                          {QStringLiteral("hasContent"), e.hasContent}});
    }
    QJsonArray masters;
    for (auto it = cache.masters.constBegin(); it != cache.masters.constEnd(); ++it) {
        QJsonArray parents;
        for (const QString &m : it.value().second) parents.append(m);
        masters.append(QJsonObject{{QStringLiteral("file"), it.key()},
                                   {QStringLiteral("mtime"), double(it.value().first)},
                                   {QStringLiteral("parents"), parents}});
    }
    const QJsonObject root{{QStringLiteral("version"), kVersion},
                           {QStringLiteral("mods"), mods},
                           {QStringLiteral("masters"), masters}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool stillValid(const Entry &entry)
{
    if (entry.dirs.isEmpty()) return false;   // nothing to check against
    for (const auto &d : entry.dirs) {
        const QFileInfo fi(d.first);
        if (!fi.isDir()) return false;
        if (fi.lastModified().toMSecsSinceEpoch() != d.second) return false;
    }
    return true;
}

} // namespace scan_cache
