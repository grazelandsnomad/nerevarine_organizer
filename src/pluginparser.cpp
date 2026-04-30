#include "pluginparser.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstring>

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

    // Recognized OpenMW asset subdirectory names (case-insensitive).
    static const QStringList kAssetDirs{
        "textures", "meshes", "splash", "fonts", "sound", "music",
        "icons", "bookart", "mwscript", "video", "shaders", "scripts",
        "grass", "lod", "distantland"
    };

    // If the root itself is a recognised asset directory (e.g. the mod was
    // installed with its path pointing directly at a "fonts/" or "textures/"
    // folder), treat it as the data= root so it is never mistaken for an
    // empty install.
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

        // Does this folder directly contain a recognized asset subdir or a .bsa?
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
            // Also recognise folders that contain loose asset files
            // (retextures, mesh replacers, etc.) even without a
            // textures/ or meshes/ subfolder.
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
            // Don't recurse into a folder we already accepted - its asset
            // subdirs would each be rediscovered as "contains textures/" etc.
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

QStringList readTes3Masters(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};

    // TES3 header layout (Morrowind, little-endian):
    //   [0..4)   tag   = "TES3"
    //   [4..8)   uint32 record-body size
    //   [8..12)  unused
    //   [12..16) unused
    //   … body of subrecords follows, each being:
    //     [0..4) tag  [4..8) uint32 size  [8..size+8) data
    QByteArray head = f.read(16);
    if (head.size() < 16 || head.left(4) != "TES3") return {};

    quint32 recSize;
    std::memcpy(&recSize, head.constData() + 4, 4);

    QByteArray body = f.read(recSize);
    if (static_cast<quint32>(body.size()) < recSize) return {};

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

} // namespace plugins
