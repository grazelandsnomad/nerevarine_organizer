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

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace bethesda_loadorder {

// Stable-partition so every master (.esm/.esl) precedes every regular plugin,
// relative order within each group preserved. Timestamp engines load masters
// first regardless of mtime, so the encoded order must match.
QStringList mastersFirst(const QStringList &plugins);

// A plugin left out of the load order, and why: masters in no file in Data/
// (`missing`), and masters that are in Data/ but were left out themselves
// (`heldMasters`). At least one of the two is non-empty.
struct Held {
    QString     plugin;
    QStringList missing;
    QStringList heldMasters;
};

struct Settled {
    QStringList order;   // what to activate: `active` minus the held, each after its masters
    QList<Held> held;    // in `active`'s order
};

// Settle `active` (already in the order to load) against what Data/ holds.
//
// A plugin whose master is in no file in Data/ stops the game while it loads
// - Fallout 4 with a Vehicle Overhaul Continued add-on for a police mod that
// was not installed - so it is held back, and so is everything that needs a
// held plugin. Generous on purpose: `presentLower` is every plugin file in
// Data/, active or not, the game's own included, because dropping a working
// mod on a wrong guess would be worse than the crash this prevents.
//
// The rest keep their order, except that a plugin comes after its masters:
// before one is placed, any master of it still to come is placed first.
// IAF - Far Harbor & Nuka World.esp loaded ahead of the framework it extends.
//
// `mastersOf` is keyed by lower-cased file name; a plugin missing from it
// (an unreadable header) counts as having no masters.
Settled settle(const QStringList &active,
               const QHash<QString, QStringList> &mastersOf,
               const QSet<QString> &presentLower);

// What a held list comes down to: each file that is really missing, with
// every held plugin that needs it - directly, or through a master held for
// it. 22 plugins that all need Skyrim.esm are one fact, and this is how the
// deploy summary says it once. Most-needed first, then by name; the plugins
// in `held`'s order. A held master that is in Data/ is never listed, since
// it is not what is missing.
struct MissingMaster {
    QString     master;     // as first spelled in `held`
    QStringList neededBy;
};
QList<MissingMaster> missingMasters(const QList<Held> &held);

// Classic Plugins.txt body (Oblivion / FO3 / FNV): active filenames one per
// line, CRLF (Windows file read through the prefix), no '*' prefix. Order here
// is cosmetic for these engines (mtime governs).
QString pluginsTxtContent(const QStringList &activeInOrder);

// Modern Plugins.txt body (Skyrim SE / FO4): each active plugin '*'-prefixed,
// in load order (masters first), CRLF. File order IS the load order, no mtime
// stamping needed. We only deploy enabled mods so every line is active;
// base-game masters are implicit and omitted.
QString asteriskPluginsTxtContent(const QStringList &activeInOrder);

// Oblivion Remastered's Plugins.txt: the game's own list with the mods'
// plugins added after it. The file already names Oblivion.esm, the DLCs and
// the remaster's AltarESPMain.esp, and the game loads exactly what it lists,
// in list order - so replacing it with the mods' plugins alone would unload
// the base game.
//
// `original` is the file as the game shipped it (the deploy's one-time
// backup), so a mod that has since been removed drops out rather than
// lingering. Its lines are kept verbatim and in order, blank lines and
// comments aside; a mod plugin it already lists is not repeated. The mods
// follow, masters first. CRLF, like the file the game writes.
QString mergedPluginsTxtContent(const QString &original, const QStringList &modsInOrder);

// Encode load order by stamping ascending mtimes onto the plugin files in
// `dataDir`, in `pluginsInOrder` (index 0 = oldest = loads first). `baseEpochMs`
// is the first plugin's mtime, each next one `stepMs` later. Files missing from
// Data/ are collected as errors, not fatal.
struct StampResult { int stamped = 0; QStringList errors; };
StampResult applyTimestampOrder(const QString &dataDir,
                                const QStringList &pluginsInOrder,
                                qint64 baseEpochMs, qint64 stepMs = 2000);

} // namespace bethesda_loadorder
