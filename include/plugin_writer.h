#ifndef PLUGIN_WRITER_H
#define PLUGIN_WRITER_H

// Rewrites the player-visible text of a TES4-family plugin.
//
// The scan can say "Bandit Chief is still English"; this is what turns it into
// "Lider Bandido". Same file format as plugin_strings, same keys, opposite
// direction - that one reads FULL/DESC/SHRT out, this one puts new text back.
//
// -- Why rebuilding, not patching in place ----------------------------
//
// A translated string is almost never the same length as the original, so the
// subrecord's 2-byte size changes, which changes the record's 4-byte dataSize,
// which changes the size of every GRUP enclosing it - and GRUP sizes nest
// arbitrarily deep. Editing bytes in place means backpatching a stack of
// offsets and getting every one right; one wrong size and the game reads
// garbage from that point on, or refuses the file.
//
// So the file is rebuilt instead: a recursive walk returns each block's new
// bytes, and every size is computed from what the block actually contains.
// Sizes cannot drift out of step with content because they are derived from it.
//
// The safety property this is held to is a byte-identical round trip: applying
// no replacements must reproduce the input exactly, byte for byte. Anything the
// walk does not understand is copied through untouched rather than
// reconstructed, so an unknown subrecord or a record type we never look at
// survives verbatim.
//
// -- What it refuses to do --------------------------------------------
//
// A localized plugin (TES4 header flag 0x80) stores 4-byte string IDs where the
// text would be; the strings live in Strings/<base>_<language>.*. Patching a
// FULL there would overwrite an ID with text and corrupt the lookup, so those
// are refused outright rather than half-handled.
//
// Text is written as UTF-8. Measured on the user's installed Spanish plugins
// (JK's Whiterun's Outskirts: "La facci\xc3\xb3n del caballo danzante"), which
// is what Skyrim SE/AE and Starfield read - not the Windows-1252 that classic
// Skyrim LE used.

#include <QHash>
#include <QString>
#include <QStringList>

namespace plugin_writer {

// New text keyed exactly as plugin_strings::StringSet, "TYPE:formid:SUB:index",
// so a value read out of a plugin can be written straight back to where it came
// from with no separate addressing scheme.
using Replacements = QHash<QString, QString>;

struct Result {
    bool        ok      = false;
    int         applied = 0;   // replacements that found their subrecord
    QStringList missed;        // keys that matched nothing in this plugin
    QString     error;         // set only when ok is false
};

// Reads srcPath, writes the patched plugin to dstPath (overwritten if it
// exists, and written via a temporary so a failure cannot leave a half-file).
// srcPath and dstPath may name the same file.
//
// An empty `repl` is the round-trip case and must reproduce the source exactly.
Result apply(const QString &srcPath, const QString &dstPath,
             const Replacements &repl);

} // namespace plugin_writer

#endif // PLUGIN_WRITER_H
