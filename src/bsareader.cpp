#include "bsareader.h"

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QList>

namespace bsa {

// TES3 BSA layout (all little-endian):
//
//   [ 0  ..  4)  version     = 0x00000100
//   [ 4  ..  8)  hashOffset  (from the end of the 12-byte header, i.e. the
//                             hash table starts at absolute offset 12+hashOffset)
//   [ 8  .. 12)  fileCount   (N)
//   [12  ..12+8N )  file size/offset records (we don't need their contents)
//   [12+8N  ..12+12N )  name offsets (uint32 into the name block)
//   [12+12N ..12+hashOffset ) packed null-terminated filenames
//   [12+hashOffset ..12+hashOffset+8N ) 8-byte hashes (ignored)
//   [ then file data … ]
//
// We only need filenames, so anything past the name block is skipped.

QStringList listTes3BsaFiles(const QString &bsaPath)
{
    QFile f(bsaPath);
    if (!f.open(QIODevice::ReadOnly)) return {};
    if (f.size() < 12) return {};

    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);

    quint32 version = 0, hashOffset = 0, fileCount = 0;
    ds >> version >> hashOffset >> fileCount;
    if (ds.status() != QDataStream::Ok) return {};

    if (version != 0x100) return {};           // Not a TES3 BSA.
    if (fileCount == 0)   return {};
    if (fileCount > 1'000'000) return {};      // Defensive: absurd file count.

    // The name block is bounded by nameOffsets-end and the hash table.
    //   nameBlockStart = 12 + 12*N
    //   nameBlockEnd   = 12 + hashOffset
    const quint64 nameBlockStart = 12ULL + quint64(fileCount) * 12ULL;
    const quint64 nameBlockEnd   = 12ULL + quint64(hashOffset);
    if (nameBlockEnd <= nameBlockStart) return {};
    const quint64 nameBlockSize = nameBlockEnd - nameBlockStart;
    if (nameBlockSize > (50ULL << 20)) return {}; // 50 MB sanity cap.

    // Skip the file-record array (8 × N bytes), read name offsets.
    if (!f.seek(12ULL + quint64(fileCount) * 8ULL)) return {};

    QList<quint32> nameOffsets;
    nameOffsets.resize(int(fileCount));
    for (quint32 i = 0; i < fileCount; ++i) {
        ds >> nameOffsets[int(i)];
        if (ds.status() != QDataStream::Ok) return {};
    }

    if (!f.seek(nameBlockStart)) return {};
    const QByteArray names = f.read(qint64(nameBlockSize));
    if (quint64(names.size()) != nameBlockSize) return {};

    QStringList out;
    out.reserve(int(fileCount));
    const char *base   = names.constData();
    const char *maxEnd = base + names.size();
    for (quint32 i = 0; i < fileCount; ++i) {
        const quint32 off = nameOffsets[int(i)];
        if (off >= quint64(names.size())) return {};

        // Names are null-terminated; find the terminator without reading past
        // the end of the block.
        const char *p = base + off;
        const char *e = p;
        while (e < maxEnd && *e) ++e;
        if (e == maxEnd) return {};           // unterminated → malformed

        // Normalise to forward-slash + lowercase for conflict-map keys.
        QString name = QString::fromLatin1(p, int(e - p));
        name.replace('\\', '/');
        out << name.toLower();
    }
    return out;
}

} // namespace bsa
