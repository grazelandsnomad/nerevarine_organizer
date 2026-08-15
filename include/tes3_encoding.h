#ifndef TES3_ENCODING_H
#define TES3_ENCODING_H

// TES3 (Morrowind) plugins store text in Windows-1252, not UTF-8. The two
// encodings agree below 0x80 and across 0xA0-0xFF, but CP1252's 0x80-0x9F
// range holds printable characters (smart quotes, dashes, the Euro sign)
// where Latin-1 has control codes - and a Spanish "á" is the single byte
// 0xE1, which read as UTF-8 is an invalid sequence. Writing UTF-8 into a
// TES3 plugin mojibakes every accent in game.
//
// Ported from Nerevarine Scribe (src/morrowind/encoding.cpp), where this
// distinction was learned the hard way. Shared by plugin_strings (reading)
// and plugin_writer (writing) so the two can never disagree about it.

#include <QByteArray>
#include <QString>

namespace tes3_encoding {

QString fromCp1252(const char *data, qsizetype size);

// Best-effort encode: characters with no CP1252 representation become '?'.
QByteArray toCp1252(const QString &str);

} // namespace tes3_encoding

#endif // TES3_ENCODING_H
