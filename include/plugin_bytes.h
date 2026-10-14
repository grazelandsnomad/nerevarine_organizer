#pragma once

// plugin_bytes - the TES4-family on-disk framing shared by the plugin readers
// (plugin_records, plugin_strings) and the writer (plugin_writer): every record
// and every GRUP opens with a 24-byte header, integers are little-endian, and
// two header flags matter.
//   record: type[4] dataSize[4] flags[4] formId[4] ...8 more
//   GRUP:   "GRUP"  groupSize[4] (INCLUDING these 24) label[4] type[4] ...8

#include <QtGlobal>

namespace plugin_bytes {

constexpr int kHeader = 24;   // record and GRUP alike

// TES4 header record: the plugin's strings live in Strings/*.STRINGS files.
constexpr quint32 kFlagLocalized  = 0x00000080;
// Any record: the body is a 4-byte little-endian uncompressed size followed
// by a raw zlib stream.
constexpr quint32 kFlagCompressed = 0x00040000;

inline quint32 le32(const char *p)
{
    return quint32(quint8(p[0]))
         | (quint32(quint8(p[1])) <<  8)
         | (quint32(quint8(p[2])) << 16)
         | (quint32(quint8(p[3])) << 24);
}

inline quint16 le16(const char *p)
{
    return quint16(quint8(p[0])) | (quint16(quint8(p[1])) << 8);
}

inline void putLe32(char *p, quint32 v)
{
    p[0] = char( v        & 0xff);
    p[1] = char((v >>  8) & 0xff);
    p[2] = char((v >> 16) & 0xff);
    p[3] = char((v >> 24) & 0xff);
}

inline void putLe16(char *p, quint16 v)
{
    p[0] = char( v       & 0xff);
    p[1] = char((v >> 8) & 0xff);
}

} // namespace plugin_bytes
