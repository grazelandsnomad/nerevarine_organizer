#ifndef PLUGIN_STRINGS_H
#define PLUGIN_STRINGS_H

// The player-visible text a TES4-family plugin carries, and whether two
// plugins say the same thing.
//
// A modlist in a language other than the mod's own is only translated where a
// translation mod actually covers it, and nothing else in the app can say so:
// the overwrite arrows say who wins a FILE, plugin_records says two plugins
// rewrite the same RECORDS, and neither answers "is there a string left to
// translate here". That needs the strings themselves.
//
// Sibling of plugin_records, on purpose. That one seeks past every record body
// because it only needs FormIDs; this one has to READ the bodies, including
// decompressing them. Two traversals with the same shape and different costs,
// so they stay separate rather than growing one parameterised walk.
//
// -- Why the record types are split in two ----------------------------
//
// "The two plugins say the same thing here" only means "untranslated" for text
// the player actually reads. Measured on a real, complete pair - Unofficial
// Skyrim Special Edition Patch and its Spanish translation, 8435 strings each -
// comparing every record type gives 835 identical (9.9%) and is useless. Almost
// all of that is text that SHOULD stay identical:
//
//   NPC_ 55.9%  proper names ("Addvar", "Aia Arria")
//   HAZD 66.7%  internal ("Circle of Protection Hazard")
//   QUST 25.7%  editor names ("DialogueMarkarth", "Riverwood test scene")
//   WRLD 16.3%  RACE 16.3%  CLAS 11.8%  FACT 9.4%  ACTI 9.1%
//   LCTN  6.9%  place names ("Markarth", "Morthal")
//   MGEF  5.9%  effect internals ("Magic Draugr FX")
//
// Restricted to the CORE types (isTranslatableType) - all under 2.5% identical
// on that same pair - the figure drops to 73 of 5372 (1.4%), and the residual
// is proper nouns that are genuinely the same word in both languages ("Riften",
// "Sujamma", "Vokun"). That 1.4% is the noise floor a threshold has to clear.
//
// Adding a type to the CORE list means measuring it first. A noisy one does not
// just add noise, it drowns the signal: a mod is judged by the ratio.
//
// -- ...and why "noisy" was the wrong reason to drop them entirely -----
//
// One allowlist was answering two different questions:
//
//   1. does this mod contain text a player reads?   - a PRESENCE question
//   2. what fraction of it looks untranslated?      - a RATIO question
//
// Only (2) is hurt by a noisy type. Cutting them from (1) as well made whole
// mods invisible: Varuun DLC items in base game (Starfield 11860) carries
// exactly one string, an NPC_ FULL reading "Va'ruun Zealot", and a French
// translation exists for it - but with NPC_ excluded its string set came back
// empty, the scan dropped the plugin as having nothing to say, and the silence
// read as "nothing to translate". Four of the seven plugins on that list were
// invisible the same way, one of them carrying 164 NPC_ strings.
//
// 55.9% identical also means 44.1% DIFFERED. Those are the role and title names
// ("Va'ruun Zealot", "Bandit Chief") that translations really do change; the
// identical remainder is personal names ("Addvar") that correctly do not. The
// measurement conflated real noise with real signal.
//
// So the SECONDARY tier (isSecondaryType) holds types that are player-visible
// but too noisy to compute a ratio from. They answer (1) and they support
// pairing - which is all the "no translation at all" verdict needs, since that
// is a pairing question with no threshold in it. They never feed the ratio, so
// the numbers above keep their meaning. Callers must not report a "partly
// untranslated" percentage from secondary text until it has been measured on a
// real translated pair.
//
// Types in NEITHER tier are internal or editor text (HAZD, MGEF, CLAS), or
// geography that stays put across languages (WRLD, TREE). Silence for those is
// correct, not a blind spot.

#include <QHash>
#include <QString>
#include <QStringList>

namespace plugin_strings {

struct StringSet {
    // False when the file is not a readable TES4-family plugin (Morrowind's
    // "TES3" magic is rejected, so an OpenMW list costs one open per plugin).
    bool valid = false;
    // TES4 header flag 0x80. A localized plugin stores 4-byte string IDs in
    // place of text, with the real strings in Strings/<base>_<language>.*, so
    // byKey comes back empty and coverage has to be answered from those files.
    bool localized = false;
    // "TYPE:formid:SUB:index" -> text. The index disambiguates repeated
    // subrecords of one kind inside a single record.
    //
    // byKey holds the CORE types and is the only one a ratio may be computed
    // from. auxByKey holds the SECONDARY types: real text, but proper-noun
    // heavy, so it establishes that the mod has something to translate and
    // supports pairing without ever moving a percentage. Same key format, so
    // the two never collide - a record type is in one tier or neither.
    QHash<QString, QString> byKey;
    QHash<QString, QString> auxByKey;

    // TES3 (Morrowind) plugin. Same tiers, same keys (anchored on editor ids
    // instead of FormIDs), but text is CP1252 and there is no Localized
    // concept. Callers that gate a calibrated threshold need to know: the
    // 5%/20 partial thresholds were measured on a TES4 pair only.
    bool tes3 = false;

    // True when the plugin carries no player-visible text in either tier -
    // the mesh/texture replacer case, which must stay silent.
    bool empty() const { return byKey.isEmpty() && auxByKey.isEmpty(); }
};

// Reads `path`. Cheap to call on a non-plugin: the magic is checked first.
// Understands both families: TES4 (Skyrim/Starfield/..., UTF-8, FormIDs) and
// TES3 (Morrowind, CP1252, editor-id identity) - see the TES3 table in the
// implementation for exactly which subrecords count and which identities
// (DIAL topics, CELL names, script text) are deliberately untouchable.
StringSet extract(const QString &path);

// How much two plugins' text overlaps. `common` counts keys present in both;
// `identical` how many of those hold the same text. A complete translation
// leaves `identical` near zero (the proper nouns above); a missing or outdated
// one leaves it high.
//
// Deliberately direction-free - it never decides which side is "the
// translation". That is what lets it work for any language with no classifier,
// and which of the pair actually loads is already shown by the overwrite
// arrows.
struct Comparison {
    int         common    = 0;
    int         identical = 0;
    QStringList samples;    // up to `maxSamples` of the identical strings
    double ratio() const
    { return common > 0 ? double(identical) / double(common) : 0.0; }
};

// Which tier to diff. Secondary results are for the present/absent verdict
// only - see the header notes; do not derive a percentage from them.
enum class Tier { Core, Secondary };

Comparison compare(const StringSet &a, const StringSet &b, int maxSamples = 8,
                   Tier tier = Tier::Core);

// -- TES3 internals shared with plugin_writer -------------------------
//
// The writer must key subrecords exactly as the extractor does, or a typed
// translation can never find its way back into the plugin. Exporting the two
// decisions - "is this subrecord text?" and "what identifies this record?" -
// is what makes identity subrecords (DIAL topics, CELL names, editor ids)
// unwritable BY CONSTRUCTION: they are never text, so no key ever names them.

// True when `sub` (4 bytes) is a translatable text subrecord of record type
// `rec` (4 bytes), in either tier.
bool tes3TextSubrecord(const char *rec, const char *sub);

// The identity string anchoring every key of a TES3 record: its NAME editor
// id, INAM for INFO, the INDX number for SKIL/MGEF. Empty when the record has
// none (such records carry no keys and are copied through untouched).
QString tes3Identity(const char *rec, const char *body, quint32 bodySize);

// Thresholds for calling a covered mod "partly untranslated". Both must be met:
// the ratio clears the 1.4% noise floor with room to spare, and the absolute
// count keeps a tiny plugin from being condemned by two coincidental words.
constexpr double kPartialRatio = 0.05;
constexpr int    kPartialCount = 20;

} // namespace plugin_strings

#endif // PLUGIN_STRINGS_H
