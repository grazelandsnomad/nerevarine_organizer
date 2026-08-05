#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include <QMap>
#include <QString>
#include <QStringList>

class Translator {
public:
    // Call once in main() before creating any window.
    // Loads <language>.ini from the translations directory.
    // Falls back to English for any missing key.
    static void init(const QString &language);

    // Returns the translated string for key.
    // Falls back to English, then returns the key itself.
    static QString t(const QString &key);

    static QString currentLanguage() { return s_language; }

    // Returns true when the active language is written right-to-left.
    static bool isRtl()
    {
        static const QStringList rtlLangs = {"arabic", "hebrew", "farsi", "urdu"};
        return rtlLangs.contains(s_language);
    }

    // Returns the language name as it should appear in its own language,
    // e.g. "catalan" → "Català". Always recognisable regardless of UI language.
    static QString nativeName(const QString &language);

    // Scans the translations directory and returns all available language codes.
    static QStringList available();

private:
    static QString findTranslationsDir();
    static QMap<QString, QString> loadFile(const QString &path);

    static QMap<QString, QString> s_strings;   // current language
    static QMap<QString, QString> s_fallback;  // English fallback
    static QString                s_language;
};

// Short-hand free function - use T("key") anywhere after #include "translator.h"
inline QString T(const QString &key) { return Translator::t(key); }

// Compile-time-checked variant.  `translation_keys.h` is auto-generated
// from translations/english.ini by scripts/gen_translation_enum.sh
// (driven by a CMake custom_command on every configure/build) and
// exposes one enum entry per key, so misspelled keys become compile
// errors instead of runtime fall-through to the literal key string.
// Prefer this form for newly-added call sites; the QString overload
// above is kept for the long tail of existing T("key") sites and for
// runtime-keyed lookups (e.g. log_triage's per-error-class strings,
// where the key name is data, not a literal).
#include "translation_keys.h"
inline QString T(Tk key) { return Translator::t(QString::fromUtf8(tkName(key))); }

#endif // TRANSLATOR_H
