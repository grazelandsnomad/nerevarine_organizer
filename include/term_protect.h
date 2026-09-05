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

// Proper nouns the setting already knows, protected wherever a mod uses them.
//
// Repetition is the only evidence this module has, and it cannot see a name a
// mod mentions ONCE. The Ashlanders lists forty strings, nineteen of which are
// an Ashlander's given name appearing exactly once each - "Shalapli",
// "Shulhaz", "Yalit" - so every one of them was sent to the translator, which
// is both nonsense to ask and enough requests in a row to earn a rate-limit
// block before the real strings were reached.
//
// -- What belongs here -------------------------------------------------
//
// Only a name that is not also an ordinary word in any language a mod is
// likely to be written in. "Quiver" is NOT here: it arrives as "Chitin
// Quiver", and a quiver is a thing, not a person. When in doubt leave it out -
// a missing entry costs one wasted request, a wrong one silently freezes a
// word that should have been translated.
//
// This is the built-in floor. `[protect]` in the user's rules file adds to it
// per language, and `[ordinary]` overrides it, so nothing here is final.
QStringList knownNames();

// Which of `vocabulary` this mod actually says, in the order given.
//
// The same case-sensitive whole-word test a knownNames() entry has to pass, so
// a caller with its own vocabulary - the lore table's protected terms - admits
// them on the same terms rather than inventing a second rule.
QStringList mentionedFrom(const QStringList &vocabulary,
                          const QStringList &sources);

// The token standing in for terms[i]. Word-shaped on purpose - see above.
QString tokenFor(int index);

// Replace each term with its token. Longest terms first, so a name that
// contains a shorter name is not half-substituted. Case-sensitive: these are
// proper nouns and the case is part of them.
//
// WHOLE WORDS. A plain substring replace turned "Blighted crops" into
// "Nrvaaed crops", which came back "Tizoned" - 47 strings on one real modlist
// would have been mangled that way. It also makes this agree with
// mentionedIn, which has always used the same boundary to decide whether a
// term is present at all: deciding a term is here on one rule and substituting
// it on another is how a term gets replaced somewhere it was never found.
QString mask(const QString &text, const QStringList &terms);

// Put the terms back. Tolerant of the translator having changed the token's
// case, spacing, or ACCENTS - handed "Nrvaa" on its own, Google returned
// "Nrvaá", having decided a bare unknown word wanted Spanish orthography.
QString unmask(const QString &text, const QStringList &terms);

// True when `masked` holds nothing but tokens and punctuation, i.e. the whole
// string was a proper noun. Such a string must not be sent anywhere: there is
// nothing in it to translate, and asking anyway is what produced "Nrvaá".
bool isOnlyNames(const QString &masked, int termCount);

// True when the whole of `text` reads as somebody's name rather than as a
// description of something. Every word must be either part of a found term or
// a word this module does not recognise as ordinary English.
//
// The case it exists for: Sixth House Obsidian Weapon names nine creatures
// "Dagoth Andas", "Dagoth Balen", "Dagoth Faras". "Dagoth" repeats, so it is
// found and masked; "Andas" appears once and is not, so the row is not
// isOnlyNames and went to the translator anyway - which returned "sin
// respirar" and "apenas lo hace". Nothing in that row was ever going to be
// translatable.
//
// The judgement is made by ordinaryWords() plus `extraOrdinary`, so it is
// only as good as that list: a real phrase built from words the list does not
// know is held back too. That is the trade, and `[ordinary]` in the rules
// file is the correction for it.
bool looksLikeName(const QString &text, const QStringList &terms,
                   const QSet<QString> &extraOrdinary = {});

} // namespace term_protect

#endif // TERM_PROTECT_H
