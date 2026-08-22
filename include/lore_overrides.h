#ifndef LORE_OVERRIDES_H
#define LORE_OVERRIDES_H

// Canonical translations for Elder Scrolls / Bethesda lore terms, consulted
// before the machine translator.
//
// Ported from Nerevarine Scribe (include/lore_overrides.h), which added it
// because Google renders lore terms literally and loses the proper noun:
// "Shadowscales" comes back as "escamas de sombra" - a correct translation of
// the words and the wrong name for the Argonian assassins of the Dark
// Brotherhood, whom the Spanish material calls "Escamas Sombrías".
//
// -- Where this sits ---------------------------------------------------
//
// Three sources fill a row, strongest first:
//
//   1. the translation memory  - what the USER decided, and it wins outright
//   2. this table              - the published name, for terms that have one
//   3. the machine translator  - a guess, for everything else
//
// So a user who disagrees with an entry here simply types their own and the
// memory answers for them from then on; nothing here can overwrite that.
//
// -- What belongs in it ------------------------------------------------
//
// Deliberately small and conservative, the same rule Scribe set: only terms
// whose translation is established in an official Bethesda localisation or the
// wider Spanish-language TES community. A wrong entry here is worse than a
// missing one, because it arrives looking authoritative rather than looking
// like the machine guess it replaced.
//
// Entries that map a term to ITSELF are not redundant. They are how a proper
// noun is protected from being translated at all ("Skooma" is Skooma in every
// language, and a machine translator will happily invent something else). The
// editor drops rows whose translation equals the original, so such a term ends
// up correctly left alone in the plugin rather than written back unchanged.
//
// Whole-cell matches only. "Dark Brotherhood" as an entire string gets the
// canonical name; the same words inside a longer sentence go to the machine
// translator, which needs the surrounding grammar to produce anything usable.

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace lore_overrides {

// The canonical translation of `text` into `token` (a target_language token),
// or an empty string when there is no entry. Case- and whitespace-insensitive
// on the way in; the entry keeps its own capitalisation.
QString lookup(const QString &text, const QString &token);

// Every source term the table holds for a language, for tests and for a
// future management UI. Empty for a language with no entries.
QStringList termsFor(const QString &token);

// Whole-cell shapes rather than whole-cell strings: {"%1 Devotee",
// "Devoto de %1"}, run through translation_rules::applyPatterns.
//
// A table of strings cannot say what Varieties of Faith needs. It names
// nineteen worship titles "<Deity> Devotee", Google keeps the English word
// order and returns "Akatosh Devoto", and listing all nineteen here would
// still be wrong for the next mod's deity. The shape covers the ones nobody
// listed.
//
// Consulted AFTER the exact table above, which is what lets a name the shape
// gets wrong have an entry of its own - "Talos Cult Devotee" is a faction,
// not a deity, and "Devoto de Talos Cult" would be half English.
QList<QPair<QString, QString>> patternsFor(const QString &token);

} // namespace lore_overrides

#endif // LORE_OVERRIDES_H
