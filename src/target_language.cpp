#include "target_language.h"

#include <QHash>
#include <QList>

namespace target_language {
namespace {

struct Entry {
    const char *token;    // Bethesda's Strings/ suffix, lowercased
    const char *name;     // shown in menus and dialogs
    const char *iso;      // ISO 639-1, for machine translation
};

// Display order, not alphabetical by token: the languages Bethesda actually
// ships games in come first, then the rest the app can still target.
const QList<Entry> &table()
{
    static const QList<Entry> kTable = {
        {"english",    "English",              "en"},
        {"french",     "French",               "fr"},
        {"german",     "German",               "de"},
        {"italian",    "Italian",              "it"},
        {"spanish",    "Spanish",              "es"},
        {"polish",     "Polish",               "pl"},
        {"portuguese", "Portuguese",           "pt"},
        {"russian",    "Russian",              "ru"},
        {"czech",      "Czech",                "cs"},
        {"japanese",   "Japanese",             "ja"},
        {"korean",     "Korean",               "ko"},
        {"chinese",    "Chinese",              "zh"},
        // No Bethesda string table uses these, but a mod can still be
        // translated into them, and the app has interface translations for
        // several - so a user who reads the app in Catalan can target it too.
        {"catalan",    "Catalan",              "ca"},
        {"dutch",      "Dutch",                "nl"},
        {"greek",      "Greek",                "el"},
        {"hungarian",  "Hungarian",            "hu"},
        {"swedish",    "Swedish",              "sv"},
        {"turkish",    "Turkish",              "tr"},
        {"ukrainian",  "Ukrainian",            "uk"},
        {"arabic",     "Arabic",               "ar"},
        {"thai",       "Thai",                 "th"},
    };
    return kTable;
}

QString norm(const QString &token) { return token.trimmed().toLower(); }

const Entry *find(const QString &token)
{
    const QString key = norm(token);
    if (key.isEmpty()) return nullptr;
    for (const Entry &e : table())
        if (key == QLatin1String(e.token)) return &e;
    return nullptr;
}

} // namespace

QStringList tokens()
{
    QStringList out;
    out.reserve(table().size());
    for (const Entry &e : table()) out << QString::fromLatin1(e.token);
    return out;
}

QString displayName(const QString &token)
{
    const Entry *e = find(token);
    return e ? QString::fromLatin1(e->name) : QString();
}

QString isoCode(const QString &token)
{
    const Entry *e = find(token);
    return e ? QString::fromLatin1(e->iso) : QString();
}

bool isKnown(const QString &token) { return find(token) != nullptr; }

QString resolve(const QString &profileOverride, const QString &fallback)
{
    const QString own = norm(profileOverride);
    if (!own.isEmpty()) return own;
    return norm(fallback);
}

QString fromLocale(const QString &localeName)
{
    // "es_ES.UTF-8" -> "es". Same shape as detectLanguage() in main.cpp, but
    // resolving against this table rather than the app's *.ini names, so it
    // can land on a language the interface is not translated into.
    const QString code = localeName.trimmed().left(2).toLower();
    if (code.isEmpty()) return {};
    for (const Entry &e : table())
        if (code == QLatin1String(e.iso)) return QString::fromLatin1(e.token);
    return {};
}

} // namespace target_language
