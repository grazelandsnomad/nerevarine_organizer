#ifndef TERM_PROTECT_H
#define TERM_PROTECT_H

// Keep a mod's own proper nouns out of the machine translator's hands.
//
// Forfeoranna Heim SSE is a dungeon mod. Handed its nine strings, Google
// translated the dungeon's NAME in some of them and not others:
//
//   Forfeoranna Heim             -> Hogar de los precursores
//   Forfeoranna Heim Catacombs   -> Catacumbas de Forfeoranna Heim
//   Forfeoranna Heim Depths      -> Profundidades de Forfeoranna Heim
//   Forfeoranna Heim Lair        -> Hogar de la Guarida de los Forerunners
//
// Inconsistency is worse than being uniformly wrong: the map, the key and the
// door now disagree about what the place is called. A translator cannot know
// "Heim" is part of a name here rather than the German for "home", so it is
// not asked. The name is replaced with an opaque token, the rest is
// translated, and the name is put back exactly as it was.
//
// -- Finding the names ------------------------------------------------
//
// No dictionary and no model: a mod's proper nouns announce themselves by
// REPEATING. "Forfeoranna Heim" is the longest capitalised phrase common to
// five of the nine strings, which is a far stronger signal than anything a
// per-string guess could produce. A phrase made entirely of ordinary English
// words is excluded, so "Iron Key" appearing twice stays translatable while
// "Dwemer" appearing twice does not.
//
// -- Why the token looks like a word ----------------------------------
//
// Measured against the live endpoint. Every candidate came back intact, but
// they differ in whether the translator will REORDER around them:
//
//   "NR0 Catacombs"    -> "Catacumbas NR0"      (natural Spanish)
//   "{0} Catacombs"    -> "{0} Catacumbas"      (English word order)
//   "XX0XX Catacombs"  -> "XX0XX Catacumbas"    (English word order)
//
// A bracketed token is treated as opaque punctuation and left where it sits;
// a word-shaped one is treated as the noun it stands for and moved to where
// Spanish wants it. So the token is word-shaped.

#include <QSet>
#include <QString>
#include <QStringList>

namespace term_protect {

// The proper-noun phrases worth protecting across `sources`, longest first.
// Empty when nothing repeats, which is the common case for a mod whose
// strings are unrelated item names.
QStringList findNames(const QStringList &sources);

// Same, plus the user's own rules: `alwaysProtect` names are protected even
// when they appear only once (repetition cannot catch those), and
// `extraOrdinary` words are treated as ordinary English so repeating them does
// not freeze them. See translation_rules.h.
QStringList findNames(const QStringList &sources,
                      const QStringList &alwaysProtect,
                      const QSet<QString> &extraOrdinary);

// The token standing in for terms[i]. Word-shaped on purpose - see above.
QString tokenFor(int index);

// Replace each term with its token. Longest terms first, so a name that
// contains a shorter name is not half-substituted. Case-sensitive: these are
// proper nouns and the case is part of them.
QString mask(const QString &text, const QStringList &terms);

// Put the terms back. Tolerant of the translator having changed the token's
// case, spacing, or ACCENTS - handed "Nrvaa" on its own, Google returned
// "Nrvaá", having decided a bare unknown word wanted Spanish orthography.
QString unmask(const QString &text, const QStringList &terms);

// True when `masked` holds nothing but tokens and punctuation, i.e. the whole
// string was a proper noun. Such a string must not be sent anywhere: there is
// nothing in it to translate, and asking anyway is what produced "Nrvaá".
bool isOnlyNames(const QString &masked, int termCount);

} // namespace term_protect

#endif // TERM_PROTECT_H
