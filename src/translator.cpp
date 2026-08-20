#include "translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

QMap<QString, QString> Translator::s_strings;
QMap<QString, QString> Translator::s_fallback;
QString                Translator::s_language;

QString Translator::findTranslationsDir()
{
    QStringList candidates = {
        // Next to the binary (installed)
        QCoreApplication::applicationDirPath() + "/translations",
        // One up - dev build with binary in build/
        QCoreApplication::applicationDirPath() + "/../translations",
        // share/<app> install layout (AppImage)
        QCoreApplication::applicationDirPath()
            + "/../share/nerevarine_organizer/translations",
        QDir::currentPath() + "/translations",
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + "/translations",
    };
    // First candidate holding a real english.ini. Probe for the file, not the
    // dir: under AppImage AppDir/usr/translations is Qt's own (empty) dir.
    for (const QString &p : candidates) {
        if (QFile::exists(p + "/english.ini"))
            return QDir(p).absolutePath();
    }
    return {};
}

// Read a translations .ini.
//
// Hand-parsed rather than handed to QSettings, which reads these files as
// configuration and quietly destroys prose in three ways:
//
//   ";"  starts a comment ANYWHERE on the line, so "The modlist still saves;
//        just without the sort step." shipped as "The modlist still saves"
//        with the rest of the sentence deleted. 24 strings were losing text.
//   ","  splits the value into a QStringList whose pieces come back trimmed,
//        so every ", " in the file rendered as ",": "(Flatpak,custom
//        XDG_CONFIG_HOME,snap)". 109 strings were affected.
//   '"'  is a quoting delimiter, removed along with the space beside it, so
//        Created "%1". came out as Created %1. and "overwritten by" captions
//        as overwritten bycaptions.
//
// The format here is one key=value per line, which needs none of that. A
// comment is a line that starts with ; or #, everything after the first = is
// the value verbatim, and the only escapes are the ones english.ini's own
// header documents.
QMap<QString, QString> Translator::loadFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return parseIni(f.readAll());
}

QMap<QString, QString> Translator::parseIni(const QByteArray &bytes)
{
    QMap<QString, QString> result;

    // UTF-8 regardless of the user's locale: these files carry accented text
    // and CJK, and a locale-decoded read would mangle them.
    const QStringList lines =
        QString::fromUtf8(bytes).split(QLatin1Char('\n'));

    for (const QString &raw : lines) {
        const QString line = raw.trimmed();       // also drops a trailing \r
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char(';')) || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')))    // [General], left over from QSettings
            continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = line.left(eq).trimmed();
        if (key.isEmpty()) continue;

        // Escapes, in one left-to-right pass so "\\n" stays a literal
        // backslash-n instead of being re-read as a line break.
        const QString rawValue = line.mid(eq + 1).trimmed();
        QString value;
        value.reserve(rawValue.size());
        for (int i = 0; i < rawValue.size(); ++i) {
            if (rawValue[i] != QLatin1Char('\\') || i + 1 >= rawValue.size()) {
                value.append(rawValue[i]);
                continue;
            }
            const QChar next = rawValue[++i];
            if      (next == QLatin1Char('n'))  value.append(QLatin1Char('\n'));
            else if (next == QLatin1Char('t'))  value.append(QLatin1Char('\t'));
            else if (next == QLatin1Char('\\')) value.append(QLatin1Char('\\'));
            else { value.append(QLatin1Char('\\')); value.append(next); }
        }

        result[key] = value;   // last definition wins, as QSettings did
    }
    return result;
}

void Translator::init(const QString &language)
{
    s_language = language.toLower().trimmed();

    const QString dir = findTranslationsDir();
    if (dir.isEmpty())
        return; // no translations found; T() will return key names

    // English is always the fallback
    const QString engPath = dir + "/english.ini";
    if (QFile::exists(engPath))
        s_fallback = loadFile(engPath);

    if (s_language == "english") {
        s_strings = s_fallback;
        return;
    }

    const QString langPath = dir + "/" + s_language + ".ini";
    if (QFile::exists(langPath)) {
        s_strings = loadFile(langPath);
    } else {
        // Unknown language - fall back to English
        s_language = "english";
        s_strings  = s_fallback;
    }
}

QString Translator::t(const QString &key)
{
    if (s_strings.contains(key))
        return s_strings.value(key);
    if (s_fallback.contains(key))
        return s_fallback.value(key);
    return key; // surface the key so missing strings are visible
}

QString Translator::nativeName(const QString &language)
{
    static const QMap<QString, QString> names = {
        {"arabic",              "العربية"},
        {"catalan",             "Català"},
        {"chinese_simplified",  "简体中文"},
        {"english",             "English"},
        {"french",              "Français"},
        {"german",              "Deutsch"},
        {"greek",               "Ελληνικά"},
        {"italian",             "Italiano"},
        {"japanese",            "日本語"},
        {"russian",             "Русский"},
        {"spanish",             "Español"},
        {"thai",                "ภาษาไทย"},
        {"ukrainian",           "Українська"},
    };
    return names.value(language.toLower(), language);
}

QStringList Translator::available()
{
    const QString dir = findTranslationsDir();
    if (dir.isEmpty())
        return {"english"};

    QStringList result;
    for (const QFileInfo &fi :
         QDir(dir).entryInfoList({"*.ini"}, QDir::Files, QDir::Name))
    {
        result << fi.completeBaseName().toLower();
    }
    if (result.isEmpty())
        result << "english";
    return result;
}
