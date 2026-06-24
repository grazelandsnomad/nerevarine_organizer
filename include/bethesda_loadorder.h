#pragma once

// bethesda_loadorder - write a Bethesda load order to disk.
//
// A Bethesda game reads two things: the ACTIVE set (which plugins load) from
// Plugins.txt, and the ORDER they load in.
//
// For the timestamp-ordered engines (OG Oblivion, FO3/FNV, Skyrim LE) the order
// is NOT from Plugins.txt; it's the mtime order of the plugin files in Data/,
// oldest first, masters (.esm) always before regular plugins. So to set order
// we stamp ascending mtimes onto the deployed plugins. (Skyrim SE / FO4 order
// by the '*'-prefixed Plugins.txt lines instead - a later phase; this module is
// the timestamp engines.)
//
// Pure helpers plus one FS helper (mtime stamping), all path-explicit so
// QTemporaryDir can exercise them.

#include <QString>
#include <QStringList>

namespace bethesda_loadorder {

// Stable-partition so every master (.esm/.esl) precedes every regular plugin,
// relative order within each group preserved. Timestamp engines load masters
// first regardless of mtime, so the encoded order must match.
QStringList mastersFirst(const QStringList &plugins);

// Classic Plugins.txt body (Oblivion / FO3 / FNV): active filenames one per
// line, CRLF (Windows file read through the prefix), no '*' prefix. Order here
// is cosmetic for these engines (mtime governs).
QString pluginsTxtContent(const QStringList &activeInOrder);

// Modern Plugins.txt body (Skyrim SE / FO4): each active plugin '*'-prefixed,
// in load order (masters first), CRLF. File order IS the load order, no mtime
// stamping needed. We only deploy enabled mods so every line is active;
// base-game masters are implicit and omitted.
QString asteriskPluginsTxtContent(const QStringList &activeInOrder);

// Encode load order by stamping ascending mtimes onto the plugin files in
// `dataDir`, in `pluginsInOrder` (index 0 = oldest = loads first). `baseEpochMs`
// is the first plugin's mtime, each next one `stepMs` later. Files missing from
// Data/ are collected as errors, not fatal.
struct StampResult { int stamped = 0; QStringList errors; };
StampResult applyTimestampOrder(const QString &dataDir,
                                const QStringList &pluginsInOrder,
                                qint64 baseEpochMs, qint64 stepMs = 2000);

} // namespace bethesda_loadorder
