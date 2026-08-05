#include "translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
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

QMap<QString, QString> Translator::loadFile(const QString &path)
{
    QMap<QString, QString> result;
    QSettings ini(path, QSettings::IniFormat);
    for (const QString &key : ini.allKeys()) {
        // QSettings prefixes default-group keys with "General/"; strip it.
        QString k = key;
        if (k.startsWith("General/", Qt::CaseInsensitive))
            k = k.mid(8);

        // IniFormat splits comma values into a QStringList, and toString() on
        // one returns "". Re-join with "," (lossless: escapes are handled
        // before the split, so join is the exact inverse).
        QVariant v = ini.value(key);
        if (v.typeId() == QMetaType::QStringList)
            result[k] = v.toStringList().join(QLatin1Char(','));
        else
            result[k] = v.toString();
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
