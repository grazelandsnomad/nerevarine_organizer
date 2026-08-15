#include "plugin_strings.h"

#include "tes3_encoding.h"

#include <QByteArray>
#include <QFile>
#include <QSet>

#include <algorithm>

namespace plugin_strings {

namespace {

// Same 24-byte record/GRUP framing as plugin_records.cpp - see that file for
// the layout notes. The difference here is that record bodies get read and
// decompressed instead of skipped.
constexpr int kHeader = 24;

// TES4 header record flags.
constexpr quint32 kFlagLocalized  = 0x00000080;
// Per-record flag: body is a 4-byte little-endian uncompressed size followed
// by a raw zlib stream.
constexpr quint32 kFlagCompressed = 0x00040000;

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

// CORE: record types whose FULL/DESC/SHRT the player reads AND whose identical
// rate is low enough to compute a ratio from. See the header for the
// measurements behind every inclusion and every omission.
bool isTranslatableType(const char *type)
{
    static const QSet<QByteArray> kTypes = {
        "BOOK", "WEAP", "ARMO", "SPEL", "PERK", "MISC", "KEYM", "LSCR",
        "INGR", "CONT", "MESG", "ENCH", "SCRL", "AMMO", "FURN", "FLOR",
        "PROJ", "SHOU", "DOOR", "SLGM", "AVIF", "DIAL",
    };
    return kTypes.contains(QByteArray(type, 4));
}

// SECONDARY: names shown in dialogue, on the map and in menus. Real text - a
// mod carrying only these has something to translate - but proper-noun heavy,
// so it never feeds a ratio. WRLD and TREE are deliberately absent: worldspace
// and tree names are geography that stays put across languages, and including
// them would flag mods (Traverse the Ulvenwald) that have nothing to translate.
bool isSecondaryType(const char *type)
{
    static const QSet<QByteArray> kTypes = {
        "NPC_", "LCTN", "CELL", "QUST", "REFR", "ACTI", "RACE", "FACT",
    };
    return kTypes.contains(QByteArray(type, 4));
}

bool isTextSubrecord(const char *sub)
{
    return qstrncmp(sub, "FULL", 4) == 0
        || qstrncmp(sub, "DESC", 4) == 0
        || qstrncmp(sub, "SHRT", 4) == 0;
}

// qUncompress wants the uncompressed size as four BIG-endian bytes ahead of the
// zlib stream; Bethesda writes the same number little-endian. Re-stamping the
// prefix is the whole conversion, so no zlib dependency of our own.
//
// Returns empty on anything malformed. The caller skips that record rather than
// discarding the plugin: one bad record should not blind the whole scan.
QByteArray inflateRecord(const QByteArray &body)
{
    if (body.size() < 4) return {};
    const quint32 rawSize = le32(body.constData());
    // Guard against a corrupt size claiming gigabytes.
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

// -- TES3 (Morrowind) ------------------------------------------------
//
// Which subrecord of which TES3 record type is player-visible text, and in
// which tier. Assembled from Nerevarine Scribe's record table and verified
// against real plugins from the live list (Cyr_Main.esm, Sky_Main.esm):
// INFO's dialogue/journal text is its NAME subrecord (identity is INAM), and
// NPC_/CREA display names are FNAM - while their CNAM/RNAM/ANAM/BNAM are
// class/race/cell/head CROSS-REFERENCES that must never be offered as text.
//
// Deliberately absent, both tiers:
//   DIAL NAME - the topic identity; dialogue cross-references break if it
//               changes (Scribe marks it textIsIdentity for the same reason).
//   CELL NAME - cell identity; scripts and teleports reference cell names.
//   SCTX/SCVR and INFO's BNAM result script - script text; rewriting it
//               desyncs from the compiled SCDT.
//
// Tier assignment mirrors the TES4 reasoning above: long-form text and item
// names are core; NPC_/CREA FNAM are proper-name heavy, so they establish
// presence and support pairing but never feed a ratio.
enum class Tes3Tier { No, Core, Secondary };

Tes3Tier tes3TierFor(const char *rec, const char *sub)
{
    const auto is = [](const char *p, const char *tag) {
        return qstrncmp(p, tag, 4) == 0;
    };

    if (is(sub, "FNAM")) {
        if (is(rec, "NPC_") || is(rec, "CREA")) return Tes3Tier::Secondary;
        static const char *const kFnam[] = {
            "WEAP", "ARMO", "CLOT", "MISC", "ALCH", "INGR", "APPA", "LIGH",
            "LOCK", "PROB", "REPA", "CONT", "DOOR", "ACTI", "SPEL", "ENCH",
            "BOOK", "BSGN", "CLAS", "FACT", "RACE", "REGN",
        };
        for (const char *t : kFnam)
            if (is(rec, t)) return Tes3Tier::Core;
        return Tes3Tier::No;
    }
    if (is(sub, "DESC")) {
        static const char *const kDesc[] = {
            "BSGN", "CLAS", "FACT", "RACE", "REGN", "SKIL", "MGEF",
        };
        for (const char *t : kDesc)
            if (is(rec, t)) return Tes3Tier::Core;
        return Tes3Tier::No;
    }
    if (is(sub, "TEXT") && is(rec, "BOOK")) return Tes3Tier::Core;
    if (is(sub, "STRV") && is(rec, "GMST")) return Tes3Tier::Core;
    if (is(sub, "NAME") && is(rec, "INFO")) return Tes3Tier::Core;
    if (is(sub, "RNAM") && is(rec, "FACT")) return Tes3Tier::Core;  // rank names
    return Tes3Tier::No;
}

// TES3 record header: type[4] dataSize[4] header1[4] flags[4] - 16 bytes.
// Subrecords: type[4] size[4] data. Flat stream, no groups, no compression.
constexpr int kTes3Header = 16;

void extractTes3(const char *data, qint64 fileEnd, StringSet &out)
{
    out.valid = true;   // magic already checked by the caller
    out.tes3  = true;

    qint64 off = 0;
    while (off + kTes3Header <= fileEnd) {
        const char *hdr = data + off;
        const quint32 size = le32(hdr + 4);
        if (off + kTes3Header + qint64(size) > fileEnd) break;   // malformed tail
        const char *body = hdr + kTes3Header;

        const QString identity = tes3Identity(hdr, body, size);
        if (identity.isEmpty()) { off += kTes3Header + qint64(size); continue; }

        // Second pass: collect the text subrecords the table admits.
        const QString prefix = QString::fromLatin1(hdr, 4) + QLatin1Char(':')
                             + identity + QLatin1Char(':');
        QHash<QString, int> seen;
        int so = 0;
        while (so + 8 <= int(size)) {
            const char *p = body + so;
            const quint32 ss = le32(p + 4);
            if (so + 8 + qint64(ss) > qint64(size)) break;

            const Tes3Tier tier = tes3TierFor(hdr, p);
            if (tier != Tes3Tier::No && ss > 0) {
                const QString text = tes3_encoding::fromCp1252(p + 8, int(ss))
                                         .remove(QChar(u'\0')).trimmed();
                if (!text.isEmpty()) {
                    const QString kind = QString::fromLatin1(p, 4);
                    const QString key = prefix + kind + QLatin1Char(':')
                                      + QString::number(seen[kind]++);
                    (tier == Tes3Tier::Core ? out.byKey : out.auxByKey)
                        .insert(key, text);
                }
            }
            so += 8 + int(ss);
        }

        off += kTes3Header + qint64(size);
    }
}

// Pull the text subrecords out of one record body. Subrecord framing is
// type[4] size[2] data[size], flat, no nesting.
void collectFrom(const char *body, int bodySize, const char *type, quint32 formId,
                 QHash<QString, QString> &out)
{
    const QString prefix = QString::fromLatin1(type, 4) + QLatin1Char(':')
                         + QString::number(formId, 16) + QLatin1Char(':');
    QHash<QString, int> seen;   // per subrecord kind, for the index suffix
    int off = 0;
    while (off + 6 <= bodySize) {
        const char *p = body + off;
        const int size = int(le16(p + 4));
        if (off + 6 + size > bodySize) break;
        if (isTextSubrecord(p) && size > 0) {
            // Zero-terminated in practice; take everything up to the first NUL
            // so a trailing terminator does not make two equal strings differ.
            QByteArray raw(p + 6, size);
            const int nul = raw.indexOf('\0');
            if (nul >= 0) raw.truncate(nul);
            const QString text = QString::fromUtf8(raw).trimmed();
            if (!text.isEmpty()) {
                const QString kind = QString::fromLatin1(p, 4);
                out.insert(prefix + kind + QLatin1Char(':')
                               + QString::number(seen[kind]++),
                           text);
            }
        }
        off += 6 + size;
    }
}

} // namespace

bool tes3TextSubrecord(const char *rec, const char *sub)
{
    return tes3TierFor(rec, sub) != Tes3Tier::No;
}

QString tes3Identity(const char *rec, const char *body, quint32 bodySize)
{
    // NAME is the editor id for most types - but INFO's NAME is its TEXT, so
    // INFO takes INAM, and SKIL/MGEF have no NAME at all, only an INDX int.
    const bool isInfo = qstrncmp(rec, "INFO", 4) == 0;
    const bool byIndx = qstrncmp(rec, "SKIL", 4) == 0
                     || qstrncmp(rec, "MGEF", 4) == 0;
    int so = 0;
    while (so + 8 <= int(bodySize)) {
        const char *p = body + so;
        const quint32 ss = le32(p + 4);
        if (so + 8 + qint64(ss) > qint64(bodySize)) break;
        if (byIndx && qstrncmp(p, "INDX", 4) == 0 && ss >= 4)
            return QString::number(le32(p + 8));
        if (!byIndx
            && qstrncmp(p, isInfo ? "INAM" : "NAME", 4) == 0 && ss > 0)
            return tes3_encoding::fromCp1252(p + 8, int(ss))
                       .remove(QChar(u'\0')).trimmed();
        so += 8 + int(ss);
    }
    return {};
}

StringSet extract(const QString &path)
{
    StringSet out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    const qint64 fileEnd = f.size();
    if (fileEnd < kHeader) return out;

    // Map the file rather than walking it with seek()+read().
    //
    // This is not a micro-optimisation. Every record needs its 24-byte header,
    // and a QFile::seek() throws away the read buffer, so a seek-per-record
    // walk turns one plugin into as many buffered re-reads as it has records.
    // Measured on the 21 MB Unofficial Skyrim Special Edition Patch (58 964
    // records): 170 SECONDS cold, against 55 ms once the page cache was warm.
    // Mapping makes the kernel read it sequentially, and costs no heap.
    //
    // map() can fail (some filesystems, or a 0-byte file), so fall back to
    // reading it in one go - still one syscall, just with a copy. The mapping
    // is released by ~QFile on every exit path, so there is no unmap to forget
    // on the early returns below.
    const uchar *mapped = f.map(0, fileEnd);
    QByteArray  copy;
    const char *data = nullptr;
    if (mapped) {
        data = reinterpret_cast<const char *>(mapped);
    } else {
        copy = f.readAll();
        if (copy.size() != fileEnd) return out;
        data = copy.constData();
    }

    // TES3 (Morrowind) is a different family - flat records, 4-byte subrecord
    // sizes, CP1252, editor-id identity instead of FormIDs - so it gets its
    // own walk rather than a parameterised one.
    if (qstrncmp(data, "TES3", 4) == 0) {
        extractTes3(data, fileEnd, out);
        return out;
    }
    if (qstrncmp(data, "TES4", 4) != 0) return out;

    const quint32 headerDataSize = le32(data + 4);
    out.localized = (le32(data + 8) & kFlagLocalized) != 0;
    out.valid     = true;
    // Nothing to read from the bodies: they hold string IDs, and the text is in
    // Strings/<base>_<language>.*. The caller answers coverage from those.
    if (out.localized) return out;

    // Iterative walk with an explicit stack of group extents, so a deeply
    // nested worldspace cannot recurse as deep as its nesting.
    QList<qint64> ends;
    qint64 off  = qint64(kHeader) + qint64(headerDataSize);
    qint64 stop = fileEnd;

    while (off + kHeader <= stop) {
        const char *hdr    = data + off;
        const quint32 size = le32(hdr + 4);

        if (qstrncmp(hdr, "GRUP", 4) == 0) {
            if (size < quint32(kHeader)) break;          // malformed
            ends.append(stop);
            stop = qMin(off + qint64(size), stop);
            off += kHeader;
        } else {
            // Touch the body only for the types we score. Skipping the rest is
            // what keeps this affordable: a texture mod's plugin never gets
            // decompressed at all.
            const bool core = isTranslatableType(hdr);
            if (size > 0 && off + kHeader + qint64(size) <= fileEnd
                && (core || isSecondaryType(hdr))) {
                const quint32 flags  = le32(hdr + 8);
                const quint32 formId = le32(hdr + 12);
                const char *bodyPtr  = hdr + kHeader;
                QHash<QString, QString> &into = core ? out.byKey : out.auxByKey;
                if (flags & kFlagCompressed) {
                    const QByteArray body =
                        inflateRecord(QByteArray::fromRawData(bodyPtr, int(size)));
                    if (!body.isEmpty())
                        collectFrom(body.constData(), body.size(), hdr, formId,
                                    into);
                } else {
                    collectFrom(bodyPtr, int(size), hdr, formId, into);
                }
            }
            off += qint64(kHeader) + qint64(size);
        }

        // Leaving a group, possibly several at once when they end together.
        while (off >= stop && !ends.isEmpty())
            stop = ends.takeLast();
    }

    return out;
}

Comparison compare(const StringSet &a, const StringSet &b, int maxSamples,
                   Tier tier)
{
    Comparison out;
    if (!a.valid || !b.valid) return out;

    const QHash<QString, QString> &ka = (tier == Tier::Core) ? a.byKey : a.auxByKey;
    const QHash<QString, QString> &kb = (tier == Tier::Core) ? b.byKey : b.auxByKey;

    // Walk the smaller side; the intersection is the same either way.
    const QHash<QString, QString> &small = (ka.size() <= kb.size()) ? ka : kb;
    const QHash<QString, QString> &large = (ka.size() <= kb.size()) ? kb : ka;

    // A handful of DIAL/topic FULLs hold an editor id rather than a prompt
    // ("DBSancMalloryRefitChoice1", "MS10Hellos"), which is text no player ever
    // sees. They stay in the counts - the thresholds have room for them, and
    // guessing at "this looks like an identifier" risks discarding a real
    // one-word item name ("Stalhrim") - but they make terrible examples, so the
    // samples prefer anything with a space in it.
    // Book DESCs open with markup ("<img src='img://Textures/...'>") that is
    // identical in every language by definition, so those go last too.
    QStringList phrases, singles, rest;
    for (auto it = small.cbegin(); it != small.cend(); ++it) {
        const auto other = large.constFind(it.key());
        if (other == large.cend()) continue;
        ++out.common;
        if (it.value() != other.value()) continue;
        ++out.identical;

        const QString &s = it.value();
        if (s.startsWith(QLatin1Char('<')) || s.size() > 80) rest << s;
        else if (s.contains(QLatin1Char(' ')))               phrases << s;
        else                                                 singles << s;
    }

    // QHash iteration order is unspecified, so sort or the tooltip reshuffles
    // between two scans of files that never changed.
    for (QStringList *l : {&phrases, &singles, &rest}) std::sort(l->begin(), l->end());
    out.samples = phrases + singles + rest;
    if (out.samples.size() > maxSamples) out.samples = out.samples.mid(0, maxSamples);
    return out;
}

} // namespace plugin_strings
