#ifndef TRANSLATION_MOD_H
#define TRANSLATION_MOD_H

// Builds a translation the same shape a Nexus translation has: a separate mod
// holding a patched copy of the plugin, under the same filename, loaded after
// the original so it wins the file conflict.
//
// That shape is chosen because it is the one everything else in this app
// already understands. The overwrite arrows explain why it wins, unticking it
// puts the game back in English, deleting it undoes the whole thing, and the
// untranslated scan pairs the two and stops reporting the mod - the loop closes
// on itself with no special case anywhere. Patching the plugin inside the
// user's own mod folder would have been fewer moving parts and would have been
// destroyed by the next re-download.
//
// Only plugins that actually gained a replacement are copied. A mod with ten
// plugins and one translated string produces a translation mod holding one
// plugin, so the file conflict stays as narrow as the edit.

#include <QHash>
#include <QString>
#include <QStringList>

#include "plugin_text.h"
#include "plugin_writer.h"

namespace translation_mod {

// Replacements for one mod, keyed by the plugin's path RELATIVE to the mod
// folder ("Varuun DLC items in base game.esm", "Data/Foo.esp"), so the output
// can mirror the source layout and the plugin lands where the game expects it.
using ByPlugin = QHash<QString, plugin_writer::Replacements>;

// Per-plugin text encoding, keyed the same way. A plugin absent from the map
// is written as UTF-8. Carried from plugin_strings::extract rather than
// re-detected: writing the wrong one mojibakes every accent in game.
using EncodingByPlugin = QHash<QString, plugin_text::Encoding>;

struct Result {
    bool        ok = false;
    QString     modPath;    // the folder created, empty on failure
    QString     modName;    // its display name
    int         plugins = 0;
    int         strings = 0;
    QStringList warnings;   // per-plugin problems that did not stop the build
    QString     error;      // set only when ok is false
};

// The folder/display name for a translation of `sourceModName` into
// `language`: "<source> - <Language> (Nerevarine)". Exposed so callers can
// detect an existing translation mod without rebuilding one.
QString nameFor(const QString &sourceModName, const QString &language);

// Creates the translation mod under `modsDir` and writes every patched plugin
// into it. An existing folder of the same name is reused and its plugins
// overwritten - re-running the editor updates the translation in place rather
// than piling up "(2)" folders.
//
// `sourceModPath` is the folder of the mod being translated; `language` is the
// lowercase token the rest of the app uses ("spanish").
Result build(const QString &sourceModPath,
             const QString &sourceModName,
             const QString &modsDir,
             const QString &language,
             const ByPlugin &replacements,
             const EncodingByPlugin &encodings = {});

} // namespace translation_mod

#endif // TRANSLATION_MOD_H
