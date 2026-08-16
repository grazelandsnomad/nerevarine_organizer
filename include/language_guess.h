#ifndef LANGUAGE_GUESS_H
#define LANGUAGE_GUESS_H

// Is this mod's text ALREADY in the language the user wants?
//
// The untranslated scan pairs a mod against another that supplies alternative
// text, and reports "no translation" when it finds none. That reasoning has a
// hole: a mod can have no partner because it is itself the translation. Better
// Crowd Citizens Spanish ships only the Spanish version - its page says
// outright "ENGLISH MOD IS NOT NEEDED" - so nothing pairs with it and it was
// flagged red, offering to translate "Ciudadana" and "Trabajador de Generdyne"
// into Spanish.
//
// So before reporting "no translation", ask whether there is anything left to
// translate. Deliberately a narrow question - "does this look like <one
// specific language>" - not "what language is this", which is a much harder
// problem and one this app has no business solving.
//
// -- Why this is allowed to be a heuristic ----------------------------
//
// It only ever SUPPRESSES a warning, and only for a language the user named.
// A false negative costs a red caption that should not have been there; a
// false positive costs a missing one. Neither writes to a plugin. That is a
// far cheaper failure surface than the verdicts elsewhere in this app, which
// is why a word-list is proportionate here where it would not be for deciding
// what to write into a file.
//
// Languages with no marker data return false - "cannot tell" - so the scan
// behaves exactly as it did before for them. Silence over a guess, again.

#include <QString>
#include <QStringList>

namespace language_guess {

// True when `samples` read as `token` (a target_language token) rather than as
// the original English. Needs several independent hits, and more evidence for
// the target language than for English, so one Spanish-looking proper noun in
// an English mod cannot flip it.
bool textLooksLike(const QStringList &samples, const QString &token);

// True when a mod's own name announces the language - "Better Crowd Citizens
// Spanish", "USSEP - Traduccion al Espanol". Mod authors put it there on
// purpose, so it is good evidence, but on its own it is only a name: used to
// lower the bar for the text test, never to decide alone.
bool nameSuggestsLanguage(const QString &modName, const QString &token);

// The question the scan actually asks. Combines both signals: strong text
// evidence stands alone, weaker text evidence counts when the name agrees.
bool alreadyInLanguage(const QString &modName, const QStringList &samples,
                       const QString &token);

} // namespace language_guess

#endif // LANGUAGE_GUESS_H
