#include "plugin_writer.h"

#include "plugin_strings.h"
#include "tes3_encoding.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace plugin_writer {
namespace {

constexpr int    kHeader         = 24;            // record and GRUP alike
constexpr quint32 kFlagCompressed = 0x00040000;
constexpr quint32 kFlagLocalized  = 0x00000080;   // TES4 header only

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

void putLe32(char *p, quint32 v)
{
    p[0] = char( v        & 0xff);
    p[1] = char((v >>  8) & 0xff);
    p[2] = char((v >> 16) & 0xff);
    p[3] = char((v >> 24) & 0xff);
}

void putLe16(char *p, quint16 v)
{
    p[0] = char( v       & 0xff);
    p[1] = char((v >> 8) & 0xff);
}

bool isTextSubrecord(const char *sub)
{
    return qstrncmp(sub, "FULL", 4) == 0
        || qstrncmp(sub, "DESC", 4) == 0
        || qstrncmp(sub, "SHRT", 4) == 0;
}

// Mirrors plugin_strings::inflateRecord - qUncompress wants the uncompressed
// size big-endian ahead of the stream, Bethesda writes it little-endian.
QByteArray inflateRecord(const QByteArray &body)
{
    if (body.size() < 4) return {};
    const quint32 rawSize = le32(body.constData());
    if (rawSize == 0 || rawSize > 64u * 1024u * 1024u) return {};

    QByteArray framed;
    framed.reserve(body.size());
    framed.append(char((rawSize >> 24) & 0xff));
    framed.append(char((rawSize >> 16) & 0xff));
    framed.append(char((rawSize >>  8) & 0xff));
    framed.append(char( rawSize        & 0xff));
    framed.append(body.constData() + 4, body.size() - 4);
    return qUncompress(framed);
}

// Inverse: qCompress emits the same big-endian size prefix, so the conversion
// is again just re-stamping those four bytes little-endian.
QByteArray deflateRecord(const QByteArray &raw)
{
    const QByteArray framed = qCompress(raw);
    if (framed.size() < 4) return {};
    QByteArray out;
    out.resize(4);
    putLe32(out.data(), quint32(raw.size()));
    out.append(framed.constData() + 4, framed.size() - 4);
    return out;
}

// Rewrite the text subrecords of one record body. Returns false when nothing
// changed, so an untouched record can be copied through verbatim instead of
// being reassembled - that is what makes the empty-replacement round trip
// byte-identical even for records we do understand.
bool patchBody(const QByteArray &body, const char *type, quint32 formId,
               const Replacements &repl, QByteArray &out,
               QStringList &usedKeys)
{
    const QString prefix = QString::fromLatin1(type, 4) + QLatin1Char(':')
                         + QString::number(formId, 16) + QLatin1Char(':');
    QHash<QString, int> seen;

    bool changed = false;
    QByteArray built;
    built.reserve(body.size());

    const char *p0 = body.constData();
    int off = 0;
    while (off + 6 <= body.size()) {
        const char *p    = p0 + off;
        const int   size = int(le16(p + 4));
        if (off + 6 + size > body.size()) break;   // malformed tail

        bool wrote = false;
        if (isTextSubrecord(p) && size > 0) {
            const QString kind = QString::fromLatin1(p, 4);
            const QString key  = prefix + kind + QLatin1Char(':')
                               + QString::number(seen[kind]++);
            const auto it = repl.constFind(key);
            if (it != repl.constEnd()) {
                // Mirror the original's terminator convention rather than
                // imposing one: a plugin that stored the string without a NUL
                // keeps that shape, so the diff stays as small as the edit.
                const bool hadNul = (size > 0 && p[6 + size - 1] == '\0');
                QByteArray text = it.value().toUtf8();
                if (hadNul) text.append('\0');

                built.append(p, 4);
                char sz[2];
                putLe16(sz, quint16(text.size()));
                built.append(sz, 2);
                built.append(text);

                usedKeys << key;
                changed = true;
                wrote   = true;
            }
        }
        if (!wrote) built.append(p, 6 + size);      // verbatim, unknown or not
        off += 6 + size;
    }
    // Anything after a malformed subrecord is copied through untouched.
    if (off < body.size()) built.append(p0 + off, body.size() - off);

    if (!changed) return false;
    out = built;
    return true;
}

// Rebuild one span of records and groups. Sizes are computed from the bytes
// each block actually produced, so they cannot drift out of step with content.
QByteArray rebuild(const QByteArray &src, int begin, int end,
                   const Replacements &repl, QStringList &usedKeys, bool &bad)
{
    QByteArray out;
    const char *base = src.constData();
    int off = begin;

    while (off + kHeader <= end) {
        const char *hdr = base + off;
        const quint32 size = le32(hdr + 4);

        if (qstrncmp(hdr, "GRUP", 4) == 0) {
            if (size < quint32(kHeader) || off + qint64(size) > end) { bad = true; return out; }
            const int inner = off + kHeader;
            const int stop  = off + int(size);
            const QByteArray body = rebuild(src, inner, stop, repl, usedKeys, bad);
            if (bad) return out;

            QByteArray head(hdr, kHeader);
            // A GRUP's size counts its own header, unlike a record's.
            putLe32(head.data() + 4, quint32(kHeader + body.size()));
            out.append(head);
            out.append(body);
            off = stop;
            continue;
        }

        if (off + kHeader + qint64(size) > end) { bad = true; return out; }
        const quint32 flags   = le32(hdr + 8);
        const quint32 formId  = le32(hdr + 12);
        const char   *bodyPtr = hdr + kHeader;

        QByteArray newBody;
        bool changed = false;

        if (flags & kFlagCompressed) {
            const QByteArray raw =
                inflateRecord(QByteArray::fromRawData(bodyPtr, int(size)));
            QByteArray patched;
            if (!raw.isEmpty()
                && patchBody(raw, hdr, formId, repl, patched, usedKeys)) {
                newBody = deflateRecord(patched);
                // A compression failure must not silently drop the record.
                if (newBody.isEmpty()) { bad = true; return out; }
                changed = true;
            }
        } else {
            QByteArray patched;
            if (patchBody(QByteArray::fromRawData(bodyPtr, int(size)),
                          hdr, formId, repl, patched, usedKeys)) {
                newBody = patched;
                changed = true;
            }
        }

        if (!changed) {
            out.append(hdr, kHeader + int(size));    // verbatim
        } else {
            QByteArray head(hdr, kHeader);
            putLe32(head.data() + 4, quint32(newBody.size()));
            out.append(head);
            out.append(newBody);
        }
        off += kHeader + int(size);
    }

    // A trailing fragment too short to be a header is not ours to interpret.
    if (off < end) out.append(base + off, end - off);
    return out;
}

// -- TES3 (Morrowind) ------------------------------------------------
//
// Flat record stream: hdr = type[4] dataSize[4] header1[4] flags[4], then
// subrecords type[4] size[4] data. No groups, no compression - the rebuild is
// a single pass, and only the record's own dataSize has to follow a change.
// Text is CP1252 (tes3_encoding), and key generation is shared with the
// extractor via plugin_strings::tes3TextSubrecord / tes3Identity, so identity
// subrecords (DIAL topics, CELL names, editor ids) can never be rewritten:
// they are never text, so no key names them.
constexpr int kTes3Header = 16;

QByteArray rebuildTes3(const QByteArray &src, const Replacements &repl,
                       QStringList &usedKeys, bool &bad)
{
    QByteArray out;
    out.reserve(src.size());
    const char *base = src.constData();
    const qint64 total = src.size();
    qint64 off = 0;

    while (off + kTes3Header <= total) {
        const char *hdr = base + off;
        const quint32 size = le32(hdr + 4);
        if (off + kTes3Header + qint64(size) > total) { bad = true; return out; }
        const char *body = hdr + kTes3Header;

        const QString identity =
            plugin_strings::tes3Identity(hdr, body, size);
        if (identity.isEmpty()) {
            out.append(hdr, kTes3Header + int(size));   // no keys, verbatim
            off += kTes3Header + qint64(size);
            continue;
        }

        const QString prefix = QString::fromLatin1(hdr, 4) + QLatin1Char(':')
                             + identity + QLatin1Char(':');
        QHash<QString, int> seen;

        bool changed = false;
        QByteArray built;
        built.reserve(int(size));
        int so = 0;
        while (so + 8 <= int(size)) {
            const char *p = body + so;
            const quint32 ss = le32(p + 4);
            if (so + 8 + qint64(ss) > qint64(size)) break;   // malformed tail

            bool wrote = false;
            if (plugin_strings::tes3TextSubrecord(hdr, p) && ss > 0) {
                const QString kind = QString::fromLatin1(p, 4);
                const QString key  = prefix + kind + QLatin1Char(':')
                                   + QString::number(seen[kind]++);
                const auto it = repl.constFind(key);
                if (it != repl.constEnd()) {
                    // Mirror the original's terminator convention - TES3
                    // strings usually carry a trailing NUL, but not always
                    // (INFO NAME frequently does not).
                    const bool hadNul = (ss > 0 && p[8 + ss - 1] == '\0');
                    QByteArray text = tes3_encoding::toCp1252(it.value());
                    if (hadNul) text.append('\0');

                    built.append(p, 4);
                    char sz[4];
                    putLe32(sz, quint32(text.size()));
                    built.append(sz, 4);
                    built.append(text);

                    usedKeys << key;
                    changed = true;
                    wrote   = true;
                }
            }
            if (!wrote) built.append(p, 8 + int(ss));
            so += 8 + int(ss);
        }
        if (so < int(size)) built.append(body + so, int(size) - so);

        if (!changed) {
            out.append(hdr, kTes3Header + int(size));
        } else {
            QByteArray head(hdr, kTes3Header);
            putLe32(head.data() + 4, quint32(built.size()));
            out.append(head);
            out.append(built);
        }
        off += kTes3Header + qint64(size);
    }

    if (off < total) out.append(base + off, int(total - off));
    return out;
}

} // namespace

Result apply(const QString &srcPath, const QString &dstPath,
             const Replacements &repl)
{
    Result r;

    QFile in(srcPath);
    if (!in.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("cannot read %1").arg(srcPath);
        return r;
    }
    const QByteArray src = in.readAll();
    in.close();

    const bool tes3 = src.size() >= kTes3Header
                   && qstrncmp(src.constData(), "TES3", 4) == 0;
    if (!tes3
        && (src.size() < kHeader
            || qstrncmp(src.constData(), "TES4", 4) != 0)) {
        r.error = QStringLiteral("not a TES3/TES4-family plugin");
        return r;
    }
    // The strings are not in the record bodies; writing text over a string ID
    // would corrupt the Strings/ lookup. See the header. (TES4 only - TES3
    // has no localization concept.)
    if (!tes3 && (le32(src.constData() + 8) & kFlagLocalized)) {
        r.error = QStringLiteral("plugin is localized; its text lives in Strings/");
        return r;
    }

    QStringList used;
    bool bad = false;
    const QByteArray out = tes3
        ? rebuildTes3(src, repl, used, bad)
        : rebuild(src, 0, int(src.size()), repl, used, bad);
    if (bad) {
        r.error = QStringLiteral("malformed plugin structure; refusing to write");
        return r;
    }

    // Nothing to replace must reproduce the input exactly - the property the
    // whole design is held to. Checking it here means a regression corrupts a
    // build, not a user's mod.
    if (repl.isEmpty() && out != src) {
        r.error = QStringLiteral("internal: round trip changed the file");
        return r;
    }

    QSaveFile f(dstPath);
    if (!f.open(QIODevice::WriteOnly)) {
        r.error = QStringLiteral("cannot write %1").arg(dstPath);
        return r;
    }
    if (f.write(out) != out.size() || !f.commit()) {
        r.error = QStringLiteral("write failed for %1").arg(dstPath);
        return r;
    }

    r.applied = int(used.size());
    for (auto it = repl.cbegin(); it != repl.cend(); ++it)
        if (!used.contains(it.key())) r.missed << it.key();
    r.ok = true;
    return r;
}

} // namespace plugin_writer
