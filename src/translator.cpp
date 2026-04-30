#include "translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

// Static member definitions
QMap<QString, QString> Translator::s_strings;
QMap<QString, QString> Translator::s_fallback;
QString                Translator::s_language;

QString Translator::findTranslationsDir()
{
    QStringList candidates = {
        // Next to the binary (installed)
        QCoreApplication::applicationDirPath() + "/translations",
        // One level up - typical during CMake development (binary in build/)
        QCoreApplication::applicationDirPath() + "/../translations",
        // Installed under share/<app> (CMake install layout, used by AppImage)
        QCoreApplication::applicationDirPath()
            + "/../share/nerevarine_organizer/translations",
        // Current working directory
        QDir::currentPath() + "/translations",
        // User data directory
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + "/translations",
    };
    // Pick the first candidate that contains a real english.ini.
    // AppDir/usr/translations exists under AppImage as Qt's own translations
    // dir, so a bare exists() check would return that empty path.
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
    // With IniFormat, top-level keys (outside any group) live in [General].
    // allKeys() returns them without a group prefix.
    for (const QString &key : ini.allKeys()) {
        // Strip the "General/" prefix that QSettings adds for the default group
        QString k = key;
        if (k.startsWith("General/", Qt::CaseInsensitive))
            k = k.mid(8);

        // QSettings IniFormat splits values containing commas into QStringList.
        // QVariant::toString() on a QStringList returns "". Join to restore the
        // original string (comma-split is lossless because escapes are processed
        // before the split, so re-joining with "," is the correct inverse).
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

    // Always load English as the fallback
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
        // Unknown or unavailable language - fall back to English
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
    return key; // last resort: return key name so missing strings are visible
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
