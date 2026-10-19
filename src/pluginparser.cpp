#include "pluginparser.h"

#include "plugin_bytes.h"
#include "plugin_records.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstring>
#include <optional>

namespace plugins {

QList<QPair<QString, QStringList>> collectDataFolders(
    const QString &root, const QStringList &exts, int maxDepth)
{
    QList<QPair<QString, QStringList>> out;
    if (root.isEmpty() || !QFileInfo(root).isDir()) return out;

    struct Entry { QString path; int depth; };
    QList<Entry> queue;
    queue.append({root, 0});

    while (!queue.isEmpty()) {
        Entry e = queue.takeFirst();
        QDir dir(e.path);

        QStringList files;
        for (const QString &cf : dir.entryList(QDir::Files, QDir::Name)) {
            QString lower = cf.toLower();
            for (const QString &ext : exts)
                if (lower.endsWith(ext)) { files << cf; break; }
        }
        if (!files.isEmpty()) out.append({e.path, files});

        if (e.depth < maxDepth) {
            for (const QString &sub :
                 dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            {
                QString lname = sub.toLower();
                if (lname == "fomod" || lname == ".git" || lname == ".svn"
                 || lname == "docs"  || lname == "documentation")
                    continue;
                queue.append({dir.filePath(sub), e.depth + 1});
            }
        }
    }
    return out;
}

QStringList collectResourceFolders(const QString &root, int maxDepth)
{
    QStringList out;
    if (root.isEmpty() || !QFileInfo(root).isDir()) return out;

    // OpenMW asset subdir names (case-insensitive).
    static const QStringList kAssetDirs{
        "textures", "meshes", "splash", "fonts", "sound", "music",
        "icons", "bookart", "mwscript", "video", "shaders", "scripts",
        "grass", "lod", "distantland"
    };

    // Root itself is an asset dir (path points straight at "fonts/" or
    // "textures/"): treat it as the data= root, else it reads as empty.
    if (kAssetDirs.contains(QFileInfo(root).fileName().toLower())) {
        out.append(root);
        return out;
    }

    struct Entry { QString path; int depth; };
    QList<Entry> queue;
    queue.append({root, 0});

    while (!queue.isEmpty()) {
        Entry e = queue.takeFirst();
        QDir dir(e.path);

        // Does this folder directly hold an asset subdir or a .bsa?
        bool looksLikeAssetRoot = false;
        const QStringList subs =
            dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &sub : subs) {
            if (kAssetDirs.contains(sub.toLower())) {
                looksLikeAssetRoot = true;
                break;
            }
        }
        if (!looksLikeAssetRoot) {
            // Also catch folders with loose asset files (retextures, mesh
            // replacers) that lack a textures/ or meshes/ subfolder.
            static const QStringList kAssetExts{
                ".bsa", ".dds", ".tga", ".nif", ".kf"
            };
            for (const QString &cf : dir.entryList(QDir::Files, QDir::Name)) {
                for (const QString &ext : kAssetExts) {
                    if (cf.endsWith(ext, Qt::CaseInsensitive)) {
                        looksLikeAssetRoot = true;
                        break;
                    }
                }
                if (looksLikeAssetRoot) break;
            }
        }

        if (looksLikeAssetRoot) {
            out.append(e.path);
            // Don't recurse into an accepted folder, or its asset subdirs
            // get rediscovered as "contains textures/" each.
            continue;
        }

        if (e.depth < maxDepth) {
            for (const QString &sub : subs) {
                QString lname = sub.toLower();
                if (lname == "fomod" || lname == ".git" || lname == ".svn"
                 || lname == "docs"  || lname == "documentation")
                    continue;
                queue.append({dir.filePath(sub), e.depth + 1});
            }
        }
    }
    return out;
}

namespace {

// The TES3 header's masters, or nullopt when the file is not TES3 or the
// header is cut short.
std::optional<QStringList> tes3Masters(QFile &f)
{
    // TES3 header (Morrowind, little-endian): [0..4) tag "TES3",
    // [4..8) uint32 body size, [8..16) unused. Body is subrecords, each
    // [0..4) tag [4..8) uint32 size [8..size+8) data.
    QByteArray head = f.read(16);
    if (head.size() < 16 || head.left(4) != "TES3") return std::nullopt;

    quint32 recSize;
    std::memcpy(&recSize, head.constData() + 4, 4);

    QByteArray body = f.read(recSize);
    if (static_cast<quint32>(body.size()) < recSize) return std::nullopt;

    QStringList masters;
    quint32 pos = 0;
    while (pos + 8 <= recSize) {
        QByteArray tag = body.mid(pos, 4);
        quint32 subSize;
        std::memcpy(&subSize, body.constData() + pos + 4, 4);
        pos += 8;
        if (pos + subSize > recSize) break;
        if (tag == "MAST") {
            QByteArray raw = body.mid(pos, subSize);
            int end = raw.indexOf('\0');
            if (end >= 0) raw.truncate(end);
            masters << QString::fromLatin1(raw);
        }
        pos += subSize;
    }
    return masters;
}

} // namespace

QStringList readTes3Masters(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return tes3Masters(f).value_or(QStringList{});
}

Masters readMasters(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray magic = f.peek(4);
    if (magic == "TES3") {
        const auto m = tes3Masters(f);
        return m ? Masters{*m, true} : Masters{};
    }
    if (magic != "TES4") return {};

    // The header record alone: 24 bytes, then its fields. A TES4 header is
    // never compressed, and the masters come before anything large.
    char hdr[plugin_bytes::kHeader];
    if (f.read(hdr, plugin_bytes::kHeader) != plugin_bytes::kHeader) return {};
    const quint32 size = plugin_bytes::le32(hdr + 4);
    const QByteArray fields = f.read(qint64(size));
    if (quint32(fields.size()) != size) return {};
    return {plugin_records::headerMasters(fields), true};
}

} // namespace plugins
