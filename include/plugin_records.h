#ifndef PLUGIN_RECORDS_H
#define PLUGIN_RECORDS_H

// Which records a TES4-family plugin edits (Oblivion through Starfield).
//
// Two mods can collide without sharing a single file. A translation is the
// usual way: "Better Crowd Citizens.esm" and "Better Crowd Citizens ES.esm" are
// different filenames, so the file-level scan (conflict_direction) correctly
// reports nothing, yet both plugins deploy, both load, and both rewrite the
// same 744 base-game NPCs. Nothing in the file view can see that.
//
// So compare what the plugins TOUCH. Record identity has to be normalized
// first: a FormID's top byte is an index into that plugin's own master list, so
// the same base-game record has a different raw FormID in two plugins that
// declare different masters. Keys come back as "<master file>:<24-bit id>",
// which compares across plugins.
//
// Only records OVERRIDING a master can collide, and they are the only ones
// collected. A plugin's own new records are indexed one past its last master
// and get renumbered from the plugin's slot in the load order, so two plugins
// minting records never overwrite each other however identical their contents -
// they both load, as duplicates. Counting those as clashes is wrong, and
// letting them into the denominator hides real ones: the Better Crowd Citizens
// pair is 412 own records to 332 overrides each, so a naive ratio reads 0.45
// where the truth is that every single overridable record collides.
//
// Walks record headers by seeking, never loading the file, so a multi-gigabyte
// plugin costs a few thousand 24-byte reads. Morrowind plugins are rejected on
// the magic ("TES3"), which keeps this off an OpenMW modlist entirely.

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace plugin_records {

struct Plugin {
    bool          valid = false;   // false when not a readable TES4-family plugin
    QStringList   masters;         // as declared, in order
    QSet<QString> overrides;       // normalized "masterfile:hexid", lower case
    int           ownRecords = 0;  // new records this plugin defines itself
};

// Reads `path`. Returns an invalid Plugin for anything that is not a
// TES4-family plugin, is unreadable, or is structurally broken.
Plugin scan(const QString &path);

// One mod's plugins, for the pairwise pass below.
struct ModPlugins {
    QString       modPath;
    QString       modName;
    QStringList   pluginNames;     // file names, for reporting
    QSet<QString> overrides;       // union across the mod's plugins
};

// Two mods rewriting the same records. `shared` is how many; `ofSmaller` is
// that as a fraction of the smaller set, which is what makes a translation
// (1.0) stand out from an ordinary partial overlap.
struct RecordClash {
    QString     modPath;
    QString     modName;
    QStringList pluginNames;
    int         shared    = 0;
    double      ofSmaller = 0.0;
};

// Every pair of mods overlapping by at least `minShared` records and
// `minRatio` of the smaller set, keyed by modPath (both directions). The
// thresholds exist because a couple of coincidentally shared base-game records
// is normal in any load order and not worth a warning.
QHash<QString, QList<RecordClash>> findClashes(const QList<ModPlugins> &mods,
                                               int minShared = 8,
                                               double minRatio = 0.5);

} // namespace plugin_records

#endif // PLUGIN_RECORDS_H
