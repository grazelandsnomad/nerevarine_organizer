#pragma once

// mod_match - "does this modlist have a mod called X?", and the small pieces
// of name-reading that question needs.
//
// Split out of fomod_hint.cpp, where it sat in an anonymous namespace and was
// therefore re-implemented by every other caller that wanted the same answer.
// The BAIN picker asks the same question of a package folder name, so the rule
// now lives in one place and both wizards agree on it.
//
// Pure: no widgets, no filesystem, no Qt GUI. Unit-tested directly.

#include <QString>
#include <QStringList>

namespace mod_match {

// Search needles for a mod name, covering the spellings a modlist actually
// uses:
//   "OAAB_Data"              -> ["OAAB_Data", "OAAB Data", "OAAB"]
//   "Ashfall - Survival Sim" -> ["Ashfall - Survival Sim", "Ashfall"]
// Empty for anything under three characters: two letters is too little to
// identify a mod even anchored, and the two-letter entries in the alias table
// reach the modlist through their full spelling instead.
QStringList needlesFor(const QString &modName);

// The installed mod that answers to `name` or to any alias of it, or "" when
// nothing does.
//
// Matching is word-boundary. A needle under eight characters must match the
// START of a mod name, which is what stops "OAAB" firing inside "OAABandoned
// Shack" and "CDF" inside an unrelated word. That anchoring is load-bearing;
// do not loosen it without a replacement guard.
//
// "" is NO EVIDENCE, never "not installed". A mod the user keeps outside the
// manager is absent from the modlist and only the user knows that, so a caller
// that acts on the empty answer has to earn the right to (see bain_hint.h).
//
// Returns the matched name rather than a bool because callers have to say
// WHICH mod answered - a badge reading "Tamriel Rebuilt is installed" needs
// the modlist's own spelling of it, not the needle that found it.
QString installedUnderAnyName(const QString &name, const QStringList &installed);

// Lower-case words that sit INSIDE a mod name rather than ending it:
// "Complete Alchemy and Cooking Overhaul", "Patch for Purists", "Legacy of
// the Dragonborn". Anything else in lower case ends the name.
bool isConnector(const QString &word);

// The leading run of Title-Case words in a plain phrase, connectors allowed
// between them, never ending on one. "" when the phrase does not open with a
// title-cased word - which is what makes "water removal" yield nothing.
//
// The prose parser in fomod_hint.cpp keeps its own overload: that one walks a
// pre-split word list and tracks sentence ends, because a name read out of a
// description must not run past a full stop. A folder name has no sentences.
QString titleRun(const QString &phrase);

// Does this read as a mod name rather than a fragment? Either several
// title-cased words, or an acronym - "Materials" on its own does not qualify.
bool looksLikeModName(const QString &phrase);

} // namespace mod_match
