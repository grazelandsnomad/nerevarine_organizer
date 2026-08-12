#include "plugin_records.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace plugin_records {

namespace {

// TES4-family layout: every record and every GRUP opens with a 24-byte header.
//   record: type[4] dataSize[4] flags[4] formId[4] ...8 more
//   GRUP:   "GRUP"  groupSize[4] (INCLUDING these 24) label[4] type[4] ...8
constexpr int kHeader = 24;

bool readExactly(QFile &f, char *buf, int n)
{
    return f.read(buf, n) == n;
}

quint32 le32(const char *p)
{
    return quint32(quint8(p[0]))
         | (quint32(quint8(p[1])) <<  8)
         | (quint32(quint8(p[2])) << 16)
         | (quint32(quint8(p[3])) << 24);
}

quint16 le16(const char *p)
{
    return quint16(quint8(p[0])) | (quint16(quint8(p[1])) << 8);
}

// Master list from the TES4 header record's MAST fields.
QStringList readMasters(QFile &f, quint32 headerDataSize)
{
    QStringList masters;
    QByteArray fields = f.read(qint64(headerDataSize));
    if (fields.size() != int(headerDataSize)) return masters;
    int off = 0;
    while (off + 6 <= fields.size()) {
        const char *p = fields.constData() + off;
        const quint16 size = le16(p + 4);
        if (qstrncmp(p, "MAST", 4) == 0) {
            const int start = off + 6;
            if (start + size > fields.size()) break;
            masters << QString::fromLatin1(
                QByteArray(fields.constData() + start, size)).remove(QChar('\0'));
        }
        off += 6 + size;
    }
    return masters;
}

} // namespace

Plugin scan(const QString &path)
{
    Plugin out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;

    char hdr[kHeader];
    if (!readExactly(f, hdr, kHeader)) return out;
    // "TES3" (Morrowind) has a different record layout and no FormIDs.
    if (qstrncmp(hdr, "TES4", 4) != 0) return out;

    const quint32 headerDataSize = le32(hdr + 4);
    out.masters = readMasters(f, headerDataSize);

    QStringList sources = out.masters;
    for (QString &s : sources) s = s.toLower();

    // Iterative walk with an explicit stack of group end offsets - a deeply
    // nested worldspace would otherwise recurse as deep as the nesting goes.
    const qint64 fileEnd = f.size();
    QList<qint64> ends;
    qint64 off = qint64(kHeader) + qint64(headerDataSize);
    qint64 stop = fileEnd;

    while (off + kHeader <= stop) {
        if (!f.seek(off) || !readExactly(f, hdr, kHeader)) break;
        const quint32 size = le32(hdr + 4);

        if (qstrncmp(hdr, "GRUP", 4) == 0) {
            if (size < quint32(kHeader)) break;                 // malformed
            const qint64 groupEnd = qMin(off + qint64(size), stop);
            ends.append(stop);
            stop = groupEnd;
            off += kHeader;
        } else {
            const quint32 formId = le32(hdr + 12);
            const int idx = int(formId >> 24);
            if (idx < sources.size())
                out.overrides.insert(sources[idx] + QLatin1Char(':')
                                     + QString::number(formId & 0x00ffffffu, 16));
            else
                ++out.ownRecords;   // this plugin's own, cannot collide
            off += qint64(kHeader) + qint64(size);
        }

        // Leaving a group: pop back to the enclosing extent, possibly several
        // at once when groups end together.
        while (off >= stop && !ends.isEmpty())
            stop = ends.takeLast();
    }

    out.valid = true;
    return out;
}

QHash<QString, QList<RecordClash>> findClashes(const QList<ModPlugins> &mods,
                                               int minShared, double minRatio)
{
    QHash<QString, QList<RecordClash>> out;
    for (int i = 0; i < mods.size(); ++i) {
        if (mods[i].overrides.isEmpty()) continue;
        for (int j = i + 1; j < mods.size(); ++j) {
            if (mods[j].overrides.isEmpty()) continue;

            // Intersect against the smaller set.
            const QSet<QString> &a = mods[i].overrides;
            const QSet<QString> &b = mods[j].overrides;
            const QSet<QString> &small = (a.size() <= b.size()) ? a : b;
            const QSet<QString> &large = (a.size() <= b.size()) ? b : a;
            int shared = 0;
            for (const QString &k : small)
                if (large.contains(k)) ++shared;
            if (shared < minShared) continue;

            const double ratio = double(shared) / double(small.size());
            if (ratio < minRatio) continue;

            out[mods[i].modPath].append({mods[j].modPath, mods[j].modName,
                                         mods[j].pluginNames, shared, ratio});
            out[mods[j].modPath].append({mods[i].modPath, mods[i].modName,
                                         mods[i].pluginNames, shared, ratio});
        }
    }
    auto byName = [](const RecordClash &x, const RecordClash &y) {
        return x.modName.compare(y.modName, Qt::CaseInsensitive) < 0;
    };
    for (QList<RecordClash> &l : out) std::sort(l.begin(), l.end(), byName);
    return out;
}

} // namespace plugin_records
