#ifndef VANILLA_GMST_H
#define VANILLA_GMST_H

// Keep Morrowind's own game settings out of a mod's translation.
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
// -- The rule ---------------------------------------------------------
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

namespace vanilla_gmst {

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

private:
    QHash<QString, QString> m_map;
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

// Deliberately no "find the vanilla folder" helper here. That needs
// openmw::looksLikeVanillaDataFolder, and dragging openmwconfigwriter into
// this module would drag it into every test target that links it - for a
// three-line loop the caller can write. This header stays cheap on purpose.

// The GMST setting named by a plugin_strings key ("GMST:sWerewolfPopup:STRV:0"
// -> "sWerewolfPopup"), or empty when the key is not a GMST string. Keeps the
// key format in one place rather than spelled out at the call site.
QString settingOfKey(const QString &key);

} // namespace vanilla_gmst

#endif // VANILLA_GMST_H
