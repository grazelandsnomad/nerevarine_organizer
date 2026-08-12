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
// -- Why an allowlist of record types ---------------------------------
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
// Restricted to the types below - all of them under 2.5% identical on that same
// pair - the figure drops to 73 of 5372 (1.4%), and the residual is proper
// nouns that are genuinely the same word in both languages ("Riften",
// "Sujamma", "Vokun"). That 1.4% is the noise floor a threshold has to clear.
//
// Adding a type here means measuring it first. A noisy one does not just add
// noise, it drowns the signal: a mod is judged by the ratio.

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
    QHash<QString, QString> byKey;
};

// Reads `path`. Cheap to call on a non-plugin: the magic is checked first.
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

Comparison compare(const StringSet &a, const StringSet &b, int maxSamples = 8);

// Thresholds for calling a covered mod "partly untranslated". Both must be met:
// the ratio clears the 1.4% noise floor with room to spare, and the absolute
// count keeps a tiny plugin from being condemned by two coincidental words.
constexpr double kPartialRatio = 0.05;
constexpr int    kPartialCount = 20;

} // namespace plugin_strings

#endif // PLUGIN_STRINGS_H
