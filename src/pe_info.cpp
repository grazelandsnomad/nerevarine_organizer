#include "pe_info.h"

#include <QRegularExpression>

namespace pe_info {
namespace {

// Every read goes through here. The images are other people's files, some of
// them truncated downloads, so an out-of-range offset has to be an answer
// rather than a fault.
struct Reader {
    const QByteArray &b;

    bool u16(qsizetype off, quint16 &out) const
    {
        if (off < 0 || off + 2 > b.size()) return false;
        const auto *p = reinterpret_cast<const quint8 *>(b.constData()) + off;
        out = quint16(p[0] | (p[1] << 8));
        return true;
    }

    bool u32(qsizetype off, quint32 &out) const
    {
        if (off < 0 || off + 4 > b.size()) return false;
        const auto *p = reinterpret_cast<const quint8 *>(b.constData()) + off;
        out = quint32(p[0]) | (quint32(p[1]) << 8)
            | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
        return true;
    }

    // A fixed-width field holding a NUL-terminated string, as the SKSE record
    // uses. Anything past the terminator is padding and is not ours to read.
    QString fixedString(qsizetype off, int cap) const
    {
        if (off < 0 || off + cap > b.size()) return {};
        const QByteArray raw = b.mid(off, cap);
        const int nul = raw.indexOf('\0');
        return QString::fromLatin1(nul < 0 ? raw : raw.left(nul)).trimmed();
    }
};

struct Section {
    quint32 virtualAddress = 0, virtualSize = 0, rawPointer = 0, rawSize = 0;
};

// Where an address the image uses internally actually sits in the file. A
// section whose virtual size exceeds what was written to disk is normal
// (zero-filled tails), so the span is the larger of the two.
qsizetype rvaToOffset(const QList<Section> &secs, quint32 rva)
{
    for (const Section &s : secs) {
        const quint32 span = qMax(s.virtualSize, s.rawSize);
        if (rva < s.virtualAddress || rva >= s.virtualAddress + span) continue;
        const quint32 delta = rva - s.virtualAddress;
        if (delta >= s.rawSize) return -1;   // in the zero-filled tail
        return qsizetype(s.rawPointer) + qsizetype(delta);
    }
    return -1;
}

// The resource tree is three levels deep - type, name, language - and every
// offset in it is relative to the start of the tree rather than to the image.
struct ResEntry { quint32 id = 0; quint32 offset = 0; bool isDir = false; };

QList<ResEntry> resourceEntries(const Reader &r, qsizetype treeBase,
                                quint32 dirOffset)
{
    QList<ResEntry> out;
    quint16 named = 0, ids = 0;
    if (!r.u16(treeBase + dirOffset + 12, named)) return out;
    if (!r.u16(treeBase + dirOffset + 14, ids))   return out;
    const int total = int(named) + int(ids);
    // A directory claiming thousands of entries is a corrupt image, not a
    // resource section; the cap keeps a bad header from walking the file.
    if (total <= 0 || total > 4096) return out;
    for (int i = 0; i < total; ++i) {
        const qsizetype e = treeBase + dirOffset + 16 + qsizetype(8) * i;
        quint32 id = 0, off = 0;
        if (!r.u32(e, id) || !r.u32(e + 4, off)) break;
        out.append({ id & 0x7fffffffu, off & 0x7fffffffu, (off & 0x80000000u) != 0 });
    }
    return out;
}

// VS_FIXEDFILEINFO is preceded by a variable-length UTF-16 key, so it is
// found by its own signature rather than by a fixed offset.
Version fixedFileInfo(const Reader &r, qsizetype at, quint32 size)
{
    Version v;
    const qsizetype end = qMin<qsizetype>(at + qsizetype(size), r.b.size());
    for (qsizetype o = at; o + 16 <= end; o += 4) {
        quint32 sig = 0;
        if (!r.u32(o, sig) || sig != 0xFEEF04BDu) continue;
        quint32 ms = 0, ls = 0;
        if (!r.u32(o + 8, ms) || !r.u32(o + 12, ls)) return v;
        v.major = int(ms >> 16);
        v.minor = int(ms & 0xffffu);
        v.build = int(ls >> 16);
        v.sub   = int(ls & 0xffffu);
        v.valid = true;
        return v;
    }
    return v;
}

SksePlugin skseVersionData(const Reader &r, const QList<Section> &secs,
                           quint32 exportRva)
{
    SksePlugin p;
    const qsizetype dir = rvaToOffset(secs, exportRva);
    if (dir < 0) return p;

    quint32 nameCount = 0, funcsRva = 0, namesRva = 0, ordsRva = 0;
    if (!r.u32(dir + 24, nameCount)) return p;
    if (!r.u32(dir + 28, funcsRva)) return p;
    if (!r.u32(dir + 32, namesRva)) return p;
    if (!r.u32(dir + 36, ordsRva))  return p;
    if (nameCount == 0 || nameCount > 65535) return p;

    const qsizetype funcs = rvaToOffset(secs, funcsRva);
    const qsizetype names = rvaToOffset(secs, namesRva);
    const qsizetype ords  = rvaToOffset(secs, ordsRva);
    if (funcs < 0 || names < 0 || ords < 0) return p;

    for (quint32 i = 0; i < nameCount; ++i) {
        quint32 nameRva = 0;
        if (!r.u32(names + qsizetype(4) * i, nameRva)) break;
        const qsizetype nameOff = rvaToOffset(secs, nameRva);
        if (nameOff < 0) continue;
        if (r.fixedString(nameOff, 32) != QLatin1String("SKSEPlugin_Version"))
            continue;

        quint16 ordinal = 0;
        if (!r.u16(ords + qsizetype(2) * i, ordinal)) return p;
        quint32 dataRva = 0;
        if (!r.u32(funcs + qsizetype(4) * ordinal, dataRva)) return p;
        const qsizetype d = rvaToOffset(secs, dataRva);
        if (d < 0) return p;

        // SKSEPluginVersionData: dataVersion, pluginVersion, three 256-byte
        // strings, the independence flags, sixteen compatible versions.
        if (!r.u32(d + 4, p.pluginVersion)) return p;
        p.name   = r.fixedString(d + 8,   256);
        p.author = r.fixedString(d + 264, 256);
        if (!r.u32(d + 776, p.independence)) return p;
        for (int k = 0; k < 16; ++k) {
            quint32 packed = 0;
            if (!r.u32(d + 780 + qsizetype(4) * k, packed)) break;
            if (packed == 0) continue;
            const Version v = decodeSkseVersion(packed);
            if (v.valid && !p.compatibleVersions.contains(v))
                p.compatibleVersions.append(v);
        }
        p.valid = true;
        return p;
    }
    return p;
}

} // namespace

QString Version::toString() const
{
    if (!valid) return {};
    return QStringLiteral("%1.%2.%3.%4").arg(major).arg(minor).arg(build).arg(sub);
}

QString Version::shortString() const
{
    if (!valid) return {};
    return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(build);
}

Version decodeSkseVersion(quint32 packed)
{
    Version v;
    if (packed == 0) return v;
    v.major = int((packed >> 24) & 0xffu);
    v.minor = int((packed >> 16) & 0xffu);
    v.build = int((packed >> 4)  & 0xfffu);
    v.sub   = 0;
    v.valid = true;
    return v;
}

Version runtimeFromLoaderName(const QString &fileName)
{
    // skse64_1_7_99.dll, skse_1_9_32.dll, obse_1_2_416.dll: the extender names
    // the runtime it hooks, and that is the whole reason to read it.
    static const QRegularExpression rx(
        QStringLiteral("_(\\d+)_(\\d+)_(\\d+)\\.dll$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = rx.match(fileName);
    Version v;
    if (!m.hasMatch()) return v;
    v.major = m.captured(1).toInt();
    v.minor = m.captured(2).toInt();
    v.build = m.captured(3).toInt();
    v.sub   = 0;
    v.valid = true;
    return v;
}

Info read(const QByteArray &image)
{
    Info info;
    const Reader r{ image };

    quint32 peAt = 0;
    if (!r.u32(0x3c, peAt)) return info;
    if (image.mid(qsizetype(peAt), 4) != QByteArray("PE\0\0", 4)) return info;
    const qsizetype pe = qsizetype(peAt);

    quint16 sectionCount = 0, optSize = 0;
    quint32 stamp = 0;
    if (!r.u16(pe + 6, sectionCount)) return info;
    if (!r.u32(pe + 8, stamp))        return info;
    if (!r.u16(pe + 20, optSize))     return info;
    if (sectionCount == 0 || sectionCount > 96) return info;

    // A linker stamp is seconds since the epoch. Reproducible builds sometimes
    // put a hash there instead, so a date outside the plausible range is
    // reported as no date at all rather than as the year 5000.
    // Bounds checked as plain epoch seconds, and converted with the one
    // spelling that means the same thing on the Qt 6.4 CI runs and on the
    // newer Qt here: the timezone-argument overloads moved between them.
    constexpr qint64 kYear2000 = 946684800;      // 2000-01-01T00:00:00Z
    const qint64 stampSecs = qint64(stamp);
    const qint64 soon = QDateTime::currentSecsSinceEpoch() + 400LL * 24 * 3600;
    if (stampSecs > kYear2000 && stampSecs < soon)
        info.built = QDateTime::fromSecsSinceEpoch(stampSecs).toUTC();

    const qsizetype opt = pe + 24;
    quint16 magic = 0;
    if (!r.u16(opt, magic)) return info;
    const qsizetype dirs = opt + (magic == 0x20b ? 112 : 96);

    quint32 exportRva = 0, resourceRva = 0;
    r.u32(dirs, exportRva);           // directory 0
    r.u32(dirs + 16, resourceRva);    // directory 2

    QList<Section> secs;
    const qsizetype secTable = opt + qsizetype(optSize);
    for (int i = 0; i < int(sectionCount); ++i) {
        const qsizetype s = secTable + qsizetype(40) * i;
        Section sec;
        if (!r.u32(s + 8,  sec.virtualSize))    break;
        if (!r.u32(s + 12, sec.virtualAddress)) break;
        if (!r.u32(s + 16, sec.rawSize))        break;
        if (!r.u32(s + 20, sec.rawPointer))     break;
        secs.append(sec);
    }
    if (secs.isEmpty()) return info;

    if (resourceRva != 0) {
        const qsizetype tree = rvaToOffset(secs, resourceRva);
        if (tree >= 0) {
            for (const ResEntry &type : resourceEntries(r, tree, 0)) {
                if (type.id != 16 || !type.isDir) continue;   // RT_VERSION
                for (const ResEntry &name : resourceEntries(r, tree, type.offset)) {
                    if (!name.isDir) continue;
                    for (const ResEntry &lang : resourceEntries(r, tree, name.offset)) {
                        if (lang.isDir) continue;
                        quint32 dataRva = 0, size = 0;
                        if (!r.u32(tree + lang.offset, dataRva))     continue;
                        if (!r.u32(tree + lang.offset + 4, size))    continue;
                        const qsizetype at = rvaToOffset(secs, dataRva);
                        if (at < 0) continue;
                        info.fileVersion = fixedFileInfo(r, at, size);
                        if (info.fileVersion.valid) break;
                    }
                    if (info.fileVersion.valid) break;
                }
                break;
            }
        }
    }

    if (exportRva != 0) info.skse = skseVersionData(r, secs, exportRva);

    return info;
}

} // namespace pe_info
