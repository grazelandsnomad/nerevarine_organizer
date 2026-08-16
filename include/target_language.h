#ifndef TARGET_LANGUAGE_H
#define TARGET_LANGUAGE_H

// The language a modlist's mods should be in - which is not the language
// Nerevarine's own interface is drawn in.
//
// Those two were the same value for a while, and every use of it was wrong.
// A user running the app in English while translating mods to Spanish got
// "Create English translation" in the menu, a translation memory saved as
// translation_memory_english.json, generated mods named "<mod> - English
// (Nerevarine)", and the machine translator asked to turn English into
// English. The scan asked for Strings/<plugin>_english.* when deciding
// whether a localized plugin was covered - the file it would have had already.
//
// -- Why the tokens are spelled this way ------------------------------
//
// The token is not ours to choose. It is compared against the language suffix
// Bethesda puts on string tables - Strings/<plugin>_<token>.STRINGS - so it
// follows their spelling, lowercased. That is why Chinese is "chinese" here
// and not "chinese_simplified" (the app's own UI-translation filename), and
// why the list carries polish, portuguese, czech and korean, which the app has
// no interface translation for but Bethesda ships games in.
//
// Deliberately NOT derived from Translator::available(), which lists the app's
// own *.ini files: in a source checkout that is english.ini alone, so the
// picker would offer exactly the one language that makes no sense.
//
// The Strings/ path is still unexercised - no plugin on any list the author
// has tested against sets the localized flag - so these tokens are documented
// from Bethesda's sLanguage values rather than verified against a real file.
// For non-localized plugins, which is everything in practice, the token only
// names things and picks the machine-translation language.

#include <QString>
#include <QStringList>

namespace target_language {

// Every language a modlist can be translated into, in display order. Tokens
// are unique and lowercase.
QStringList tokens();

// Human-readable name for a token ("spanish" -> "Spanish"), or an empty string
// for a token that is not in the table. Empty rather than the raw token: a
// caller showing a name wants to know when it has nothing to show.
QString displayName(const QString &token);

// ISO 639-1 code for the machine-translation request ("spanish" -> "es"), or
// empty when the token is unknown or has no code. An empty code disables the
// machine-translation button rather than sending a guess.
QString isoCode(const QString &token);

// True when `token` names a language in the table. Case- and
// whitespace-insensitive, like every other lookup here.
bool isKnown(const QString &token);

// The effective language: a modlist profile's own override when it has set
// one, otherwise the shared default. Empty when neither is set, which is the
// "never been asked" state - callers must treat it as unknown, not as English.
//
// A free function rather than a MainWindow method so the rule that decides
// what a user actually gets is testable without a window.
QString resolve(const QString &profileOverride, const QString &fallback);

// Best first guess for the one-time prompt, from a system locale name
// ("es_ES.UTF-8", "ca_ES", "es"). Empty when the locale maps to nothing in the
// table. Only ever a pre-selection - the user still confirms.
QString fromLocale(const QString &localeName);

} // namespace target_language

#endif // TARGET_LANGUAGE_H
