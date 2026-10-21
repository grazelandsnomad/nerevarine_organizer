#include "qt_strings.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QMap>
#include <QTranslator>

namespace qt_strings {
namespace {

// Qt's source strings are English: a translator that answers every lookup
// with the source text puts English in front of whatever the desktop
// installed. Never empty, or installTranslator would refuse it.
class SourceStrings final : public QTranslator {
public:
    using QTranslator::QTranslator;
    bool isEmpty() const override { return false; }
    QString translate(const char *, const char *sourceText, const char *,
                      int) const override
    {
        return QString::fromUtf8(sourceText);
    }
};

} // namespace

QString codeFor(const QString &language)
{
    static const QMap<QString, QString> codes = {
        {"arabic",             "ar"}, {"catalan",   "ca"}, {"chinese_simplified", "zh_CN"},
        {"english",            "en"}, {"french",    "fr"}, {"german",             "de"},
        {"greek",              "el"}, {"italian",   "it"}, {"japanese",           "ja"},
        {"russian",            "ru"}, {"spanish",   "es"}, {"thai",               "th"},
        {"ukrainian",          "uk"},
    };
    return codes.value(language.trimmed().toLower());
}

QString install(QCoreApplication &app, const QString &language)
{
    const QString code = codeFor(language);
    if (code.isEmpty())
        return QStringLiteral("qt strings: no code for %1, left as the desktop has them")
            .arg(language);

    if (code == QLatin1String("en")) {
        app.installTranslator(new SourceStrings(&app));
        return QStringLiteral("qt strings: English (source text) over the desktop's");
    }

    auto *qt = new QTranslator(&app);
    const QString dir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (qt->load(QLocale(code), QStringLiteral("qtbase"), QStringLiteral("_"), dir)) {
        app.installTranslator(qt);
        return QStringLiteral("qt strings: qtbase_%1 from %2").arg(code, dir);
    }
    delete qt;
    return QStringLiteral("qt strings: no qtbase_%1 in %2, left as the desktop has them")
        .arg(code, dir);
}

} // namespace qt_strings
