#ifndef VANILLA_TEXT_H
#define VANILLA_TEXT_H

// Keep Morrowind's own words out of a mod's translation.
//
// Two questions, one table, one walk of the game files: which game settings a
// mod actually changed, and which display names it merely re-saved. Both
// answer the same underlying one - is this the mod talking, or the base game?
//
// -- Part two: the names ----------------------------------------------
//
// Familiar Looks - Unique Characters MacKom-ed is a head replacer. It offered
// twenty-one strings to translate - Fargoth, Neloth, Orvas Dren, Hlormar
// Wine-Sot - and earned an HTTP 429 reaching for them. All twenty-one are
// byte-identical to the name Morrowind.esm already gives that very record: the
// mod re-saves each NPC to swap a face and leaves the name alone.
//
// term_protect cannot catch these, and not by oversight. It needs a name
// REPEATED to notice it, and an NPC record carries its name exactly once. See
// saysExactly, which needs no heuristic at all because the answer is on disk.
//
// Measured across 331 installed plugins: of 44,844 display names offered,
// 9,775 - 22% - are the base game re-saved untouched. NPC names 34%, clothing
// 31%, weapons 24%, containers 13%.
//
// -- Part one: the game settings --------------------------------------
//
// Daedric Maul is a 7 KB mod whose entire content is one weapon. It offered
// TWENTY-SEVEN strings to translate - "You cannot rest in werewolf form.",
// "Teleportation magic does not work here.", "sMagicCreature04ID" - and earned
// an HTTP 429 twenty-one requests in. None of it is the mod's content: the
// plugin carries 72 GMST records its author's editor re-saved without meaning
// to, and plugin_strings treats every GMST string as top-tier translatable
// text with no check on where it came from.
//
// -- Why this is not just about wasted requests -----------------------
//
// A translation mod loads AFTER the mod it translates, so a translated GMST
// does not stay inside that mod - it replaces the setting for the whole game.
// Machine-translating Morrowind's UI, from a weapon replacer, for the rest of
// the playthrough.
//
// It gets worse in the specific case that prompted this. Daedric Maul stores
//
//     sEffectSummonCreature01 = "sEffectSummonCreature01"
//
// where Bloodmoon says "Call Wolf". That is an editor which loaded without the
// expansion and wrote the setting's own name back as its value. Sending that
// to a translator, and writing the answer into a vanilla game setting, is how
// a spell ends up named after a variable.
//
// -- The rule, both halves --------------------------------------------
//
// A GMST worth translating is one the mod actually CHANGED. Everything else is
// the base game talking. Measured across 866 installed plugins: of 190 GMST
// rows offered today, 144 are the game's own and 46 are real edits - and the
// 46 are exactly the mods that mean it, Patch for Purists and PrettyLoot and
// MultiMark, which keep every one of theirs.
//
// Two of the three rules need no game files at all, so a setup where the
// vanilla folder cannot be found still gets most of the benefit. See isDirty.

#include <QHash>
#include <QString>
#include <QStringList>

namespace vanilla_text {

// The base game's string settings, read once from Morrowind.esm and its
// expansions. An empty table is a legitimate state, not an error - see
// isDirty, which still answers without one.
class Table {
public:
    // Reads the GMSTs of Morrowind/Tribunal/Bloodmoon out of `dataFolder`.
    // Missing expansions are skipped silently; a missing Morrowind.esm leaves
    // the table empty and returns false.
    bool load(const QString &dataFolder);

    // The base game's text for `setting`, or a null QString when the table
    // does not know it. Null and empty are distinguished on purpose: a setting
    // the game defines as "" is not the same as one it never defines.
    QString value(const QString &setting) const;

    bool contains(const QString &setting) const { return m_map.contains(setting); }
    int  size()  const { return int(m_map.size()); }
    bool empty() const { return m_map.isEmpty(); }

    // For tests, and for a caller that has the settings from somewhere else.
    void insert(const QString &setting, const QString &text) { m_map.insert(setting, text); }
    void insertKey(const QString &key, const QString &text) { m_byKey.insert(key, text); }

    // True when the base game already says exactly this, for this very record -
    // so the mod re-saved it without changing a character.
    //
    // Keyed on the RECORD, not on the text, because those are different
    // questions. "Fargoth" is a vanilla name wherever it turns up, but a mod
    // that invents an NPC and calls him Fargoth wrote that, and deserves to be
    // asked about it. On the live list 3,961 display-name rows match by record;
    // another 831 carry a vanilla name under a new id and are left alone.
    //
    // `key` is a plugin_strings key, "TYPE:<editorid>:SUB:index".
    bool saysExactly(const QString &key, const QString &text) const;

    int keyCount() const { return int(m_byKey.size()); }

private:
    QHash<QString, QString> m_map;     // GMST setting name -> the game's text
    QHash<QString, QString> m_byKey;   // plugin_strings key -> the game's text
};

// True when this GMST is the base game talking rather than the mod.
//
//   1. the value matches what the game already says   - a re-saved duplicate
//   2. the value IS the setting's own name            - an editor placeholder
//   3. the value is blank                             - nothing to translate
//
// Rules 2 and 3 hold without any game files, which is what makes a missing
// vanilla folder a degraded answer rather than no answer.
//
// Deliberately NOT a rule: "the game defines this setting at all". Patch for
// Purists exists to correct vanilla wording and MultiMark repurposes the
// summon spells into Greater Mark and Greater Recall - both change settings
// the game defines, and both mean it.
bool isDirty(const QString &setting, const QString &value, const Table &vanilla);

// True when this setting's VALUE is an object id the engine looks up, rather
// than text anybody reads. Morrowind names them all "...ID":
//
//     sMagicBoundBattleAxeID = "Bound_Battle_Axe"
//     sMagicCreature01ID     = "BM_wolf_grey_summon"
//
// Thirty-four of them in the base game. Translating one means the engine looks
// for a creature that does not exist, and the spell then does nothing at all -
// no error, no crash, just silence.
//
// Keyed on the setting NAME, not on the shape of the value. A value-shape guess
// gets it wrong in both directions: it would flag "Abilities:" and
// "Cost/Chance", and it would MISS "Winged Twilight_summon" and "Golden
// Saint_summon", which have spaces in them and are still ids.
//
// Deliberately independent of isDirty. A mod that repoints one of these means
// it - MultiMark aims two at its own Mark and Recall summons - so "did the mod
// change it" answers KEEP, which is exactly the case that breaks.
bool holdsObjectId(const QString &setting);

// Deliberately no "find the vanilla folder" helper here. That needs
// openmw::looksLikeVanillaDataFolder, and dragging openmwconfigwriter into
// this module would drag it into every test target that links it - for a
// three-line loop the caller can write. This header stays cheap on purpose.

// The GMST setting named by a plugin_strings key ("GMST:sWerewolfPopup:STRV:0"
// -> "sWerewolfPopup"), or empty when the key is not a GMST string. Keeps the
// key format in one place rather than spelled out at the call site.
//
// Reads the fields from the LEFT, so an editor id containing a colon would
// throw it off. No vanilla setting name has one and none of Bethesda's do;
// isDisplayNameKey below counts from the right, where ids are not so tame.
QString settingOfKey(const QString &key);

// True when a plugin_strings key names a record's DISPLAY NAME - an NPC, a
// creature, a weapon, a door. "NPC_:fargoth:FNAM:0" yes, "BOOK:sc_x:TEXT:0" no.
//
// The filter that makes the table affordable. plugin_strings collects
// Morrowind's whole book library and its sixty thousand lines of dialogue into
// the same hash; keeping all of it would cost hundreds of megabytes for the
// life of the process, where the display names are about 8,600 short strings.
//
// Counts fields from the RIGHT. TES3 editor ids are whatever the author typed
// and a colon in one would shift every field after it, which reading from the
// left could not survive.
bool isDisplayNameKey(const QString &key);

} // namespace vanilla_text

#endif // VANILLA_TEXT_H
