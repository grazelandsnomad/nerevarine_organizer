#ifndef PLUGIN_TEXT_H
#define PLUGIN_TEXT_H

// How the text inside a plugin is encoded, which is not one answer.
//
// TES3 (Morrowind) is always Windows-1252. TES4-family plugins are whatever
// the mod author's tools wrote, and BOTH occur in the wild. Measured across
// every plugin on the author's live Skyrim AE and Starfield lists:
//
//   UTF-8   7 plugins   (USSEP, JK's Whiterun's Outskirts, Alternate Start...)
//   CP1252  1 plugin    (Better Crowd Citizens ES - "T\xe9cnico")
//   mixed   0 plugins
//
// No plugin mixes the two, so the encoding is a property of the file and can
// be decided once by looking at whether every non-ASCII string is valid UTF-8.
// A CP1252 byte like 0xE9 is not valid UTF-8 on its own, so the test is exact
// in the direction that matters: valid UTF-8 that was meant as CP1252 would
// need the author to have typed a sequence like "Ã©", which is already broken.
//
// Getting this wrong is not cosmetic. Reading CP1252 as UTF-8 put a
// replacement glyph in the middle of "Técnico" in the editor; WRITING UTF-8
// back into that same plugin would have handed the game bytes it reads as
// CP1252, mojibaking every accent in the translation. So the writer encodes
// replacements in the encoding the source file already uses.
//
// Windows-1252 matters over plain Latin-1 for its 0x80-0x9F range, where it
// has printable characters (smart quotes, dashes, the Euro sign) rather than
// C1 control codes. Ported from Nerevarine Scribe, which learned that first.

#include <QByteArray>
#include <QString>

namespace plugin_text {

enum class Encoding { Utf8, Cp1252 };

// True when the buffer is valid UTF-8. Pure ASCII is valid UTF-8, so this
// answers "could be UTF-8", which is what detection needs.
bool isValidUtf8(const char *data, qsizetype size);

// Fold one string's evidence into a running verdict. Start from Utf8 (the
// no-evidence default, and the majority) and call this for every string read;
// one buffer that cannot be UTF-8 settles the file as Cp1252 for good.
void observe(Encoding &verdict, const char *data, qsizetype size);

QString decode(const char *data, qsizetype size, Encoding enc);

// Best-effort encode. Characters with no representation in the target
// encoding become '?'; UTF-8 can represent everything, so that only bites on
// Cp1252.
QByteArray encode(const QString &str, Encoding enc);

} // namespace plugin_text

#endif // PLUGIN_TEXT_H
