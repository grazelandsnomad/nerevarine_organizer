#pragma once

// modlist_serializer - schema-versioned JSONL writer + version-tolerant
// reader for the modlist and load-order files.
//
// Why this exists:
//   The pre-v2 modlist format was tab-separated text with positional
//   fields ("+ /path\tname\tannot\turl\tdate\t...").  Two real risks:
//     1. Adding a column meant editing saveModList AND loadModList AND
//        every test in lockstep, with `parts.size() > N` guards
//        sprinkled through the parser.
//     2. A literal '\t' or '\n' inside a custom name silently corrupted
//        the row split and lost data on the next load.  Wabbajack-imported
//        names with tabs in them were the canonical hit.
//
// The v2 format is JSON Lines:
//   · Line 1 is the schema header:
//       {"format":"nerevarine_modlist","version":2}
//   · Each subsequent non-empty line is one record JSON object:
//       {"type":"sep", ...}            ← separator
//       {"type":"mod", ...}            ← mod row
//   · Optional fields can be omitted; the parser treats missing keys as
//     defaults, and unknown keys are silently ignored.  That's the
//     forward-compat property: a v3 writer can add fields, v2 readers
//     load the file but lose only the new fields.
//
// Backward compatibility: parseModlist sniffs the first non-empty line.
// If it starts with `{"format":"nerevarine_modlist"` we go down the JSONL
// path; otherwise we dispatch to the legacy v1 (tab-separated) parser
// so existing user files load on first run under the new code.  Saving
// always produces v2 - the next save migrates the file in place.
//
// Same scheme for the load-order file: header line `{"format":
// "nerevarine_loadorder","version":2}`, then one plugin filename per line
// (no JSON wrapper - the field is already just a name, no escaping
// needed).  Legacy files (no header, optional `#`-comments) are detected
// by the absence of the format header and parsed line-by-line.

#include <QList>
#include <QString>
#include <QStringList>

struct ModEntry;

namespace modlist_serializer {

// Render the modlist as v2 JSONL text including the schema header.
// Trailing newline; suitable for direct write to disk.  Pure - never
// touches the filesystem.
QString serializeModlist(const QList<ModEntry> &entries);

// Parse modlist text in either v1 (legacy tab) or v2 (JSONL) format.
// Detection is based on the first non-empty line.  Unrecognized formats
// return an empty list rather than throwing - caller treats that the
// same as a fresh-install file.
QList<ModEntry> parseModlist(const QString &contents);

// Render the load-order list as v2 text (header + one plugin per line).
QString serializeLoadOrder(const QStringList &plugins);

// Parse load-order text.  Accepts both v1 (no header, line-per-plugin
// with optional `#`-comments) and v2 (schema header + line-per-plugin).
QStringList parseLoadOrder(const QString &contents);

// Lowest-level building block for migration tests: declares which
// `version` the parser will emit when it writes.  Bumped when the
// on-disk shape changes incompatibly.
constexpr int kModlistVersion   = 2;
constexpr int kLoadOrderVersion = 2;

} // namespace modlist_serializer
