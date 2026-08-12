#pragma once

// modlist_serializer - schema-versioned JSONL writer + version-tolerant
// reader for the modlist and load-order files.
//
// The pre-v2 format was tab-separated positional fields
// ("+ /path\tname\tannot\turl\tdate\t..."). Two problems it caused:
//   1. Adding a column meant touching saveModList, loadModList, and every
//      test in lockstep, with parts.size() > N guards everywhere.
//   2. A literal '\t' or '\n' in a custom name split the row wrong and lost
//      data on next load - Wabbajack-imported names with tabs hit this.
//
// v2 is JSON Lines:
//   · Line 1 is the schema header:
//       {"format":"nerevarine_modlist","version":2}
//   · Each later non-empty line is one record object:
//       {"type":"sep", ...}            ← separator
//       {"type":"mod", ...}            ← mod row
//   · Missing keys default; unknown keys are ignored. So a v3 writer can add
//     fields and v2 readers still load the file, losing only the new ones.
//
// parseModlist sniffs the first non-empty line: starts with
// {"format":"nerevarine_modlist" -> JSONL path, else the legacy v1
// (tab-separated) parser, so old user files still load. Saving always
// writes v2, migrating the file in place.
//
// Load-order file is the same scheme: header
// {"format":"nerevarine_loadorder","version":2} then one plugin name per
// line (no JSON wrapper - a name needs no escaping). Legacy files (no
// header, optional #-comments) are parsed line-by-line.

#include <QList>
#include <QString>
#include <QStringList>

struct ModEntry;

namespace modlist_serializer {

// Render the modlist as v2 JSONL (schema header included, trailing newline),
// ready to write to disk. Never touches the filesystem.
QString serializeModlist(const QList<ModEntry> &entries);

// Parse modlist text in v1 (legacy tab) or v2 (JSONL), detected from the
// first non-empty line. Unrecognized input returns an empty list (caller
// treats that as a fresh-install file).
QList<ModEntry> parseModlist(const QString &contents);

// Render the load-order list as v2 text (header + one plugin per line).
QString serializeLoadOrder(const QStringList &plugins);

// Parse load-order text. Accepts v1 (no header, line-per-plugin, optional
// #-comments) and v2 (schema header + line-per-plugin).
QStringList parseLoadOrder(const QString &contents);

// Version the writer emits. Bumped when the on-disk shape changes
// incompatibly.
constexpr int kModlistVersion   = 2;
constexpr int kLoadOrderVersion = 2;

} // namespace modlist_serializer
