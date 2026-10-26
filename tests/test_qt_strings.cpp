// test_qt_strings - Qt's own dialog strings follow the app's language.
//
// On a Catalan desktop the English welcome wizard closed on "Acaba": the app's
// text comes from T(), Qt's from the qtbase translation the desktop installs.
// Pinned here with the real qtbase_ca.qm when the machine has one, and with a
// stand-in translator when it does not, so the suite never skips.

#include "qt_strings.h"
#include "standard_buttons.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

#include <iostream>

#include "test_harness.h"

namespace {

// What a desktop that installs qtbase translations does to the app: every
// Qt string comes back in its language.
class CatalanLikeDesktop final : public QTranslator {
public:
    using QTranslator::QTranslator;   // parented to the app, or LeakSanitizer objects
    bool isEmpty() const override { return false; }
    QString translate(const char *context, const char *sourceText, const char *,
                      int) const override
    {
        if (qstrcmp(context, "QWizard") == 0 && qstrcmp(sourceText, "&Finish") == 0)
            return QStringLiteral("&Acaba");
        if (qstrcmp(context, "QPlatformTheme") == 0 && qstrcmp(sourceText, "&Yes") == 0)
            return QStringLiteral("&Sí");
        return {};
    }
};

QString finish() { return QCoreApplication::translate("QWizard", "&Finish"); }
QString yes()    { return QCoreApplication::translate("QPlatformTheme", "&Yes"); }

} // namespace

int main(int argc, char **argv)
{
    // A QApplication, for the button rows below; offscreen, as every suite.
    QApplication app(argc, argv);
    std::cout << "=== qt_strings ===\n";

    check("a language file name maps to its code",
          qt_strings::codeFor("catalan") == "ca" && qt_strings::codeFor("English") == "en"
              && qt_strings::codeFor("chinese_simplified") == "zh_CN");
    check("an unknown one maps to nothing", qt_strings::codeFor("klingon").isEmpty());

    // The desktop speaks first. The real qtbase_ca.qm where Qt ships one,
    // else a stand-in that does the same to the two strings asked about.
    auto *desktop = new QTranslator(&app);
    const QString dir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    bool real = desktop->load(QLocale("ca"), QStringLiteral("qtbase"), QStringLiteral("_"), dir);
    if (real) {
        app.installTranslator(desktop);
    } else {
        delete desktop;
        app.installTranslator(new CatalanLikeDesktop(&app));
    }
    std::cout << (real ? "  (using the machine's qtbase_ca.qm)\n"
                       : "  (no qtbase_ca.qm here - a stand-in translator)\n");
    check("with the desktop's translation alone, Qt's wizard speaks Catalan",
          finish() != QStringLiteral("&Finish"), finish());

    const QString did = qt_strings::install(app, "english");
    check("installing English puts the source text in front",
          finish() == QStringLiteral("&Finish") && yes() == QStringLiteral("&Yes"),
          finish() + " / " + yes());
    check("and says so", did.contains("English"), did);

    check("an unknown language leaves things as they are",
          qt_strings::install(app, "klingon").contains("no code")
              && finish() == QStringLiteral("&Finish"));

    // KDE's platform theme labels a prompt's buttons from KDE's catalogs, in
    // the desktop's language, past every translator. Here the theme is
    // played by setting its Catalan texts on the buttons by hand.
    std::cout << "\n[standard_buttons]\n";
    check("a standard button's text comes through the translators",
          standard_buttons::text(QDialogButtonBox::Yes) == QStringLiteral("&Yes")
              && standard_buttons::text(QDialogButtonBox::Cancel) == QStringLiteral("Cancel")
              && standard_buttons::text(QDialogButtonBox::NoButton).isEmpty());
    {
        QDialogButtonBox row(QDialogButtonBox::Yes | QDialogButtonBox::No | QDialogButtonBox::Ok);
        row.button(QDialogButtonBox::Yes)->setText(QStringLiteral("&Sí"));
        row.button(QDialogButtonBox::No)->setText(QStringLiteral("&No"));
        row.button(QDialogButtonBox::Ok)->setText(QStringLiteral("Update Selected (3)"));
        const QHash<int, QString> kde = {{int(QDialogButtonBox::Yes), QStringLiteral("&Sí")},
                                         {int(QDialogButtonBox::No), QStringLiteral("&No")},
                                         {int(QDialogButtonBox::Ok), QStringLiteral("D'acord")}};
        standard_buttons::relabel(&row, kde);
        check("the theme's Catalan Yes becomes the app's",
              row.button(QDialogButtonBox::Yes)->text() == QStringLiteral("&Yes"),
              row.button(QDialogButtonBox::Yes)->text());
        check("a button the app named itself keeps its name",
              row.button(QDialogButtonBox::Ok)->text() == QStringLiteral("Update Selected (3)"),
              row.button(QDialogButtonBox::Ok)->text());
    }
    {
        // Installed, it acts on show - a QMessageBox's own row included.
        standard_buttons::install(app);
        QMessageBox box(QMessageBox::Question, QStringLiteral("t"), QStringLiteral("body"),
                        QMessageBox::Yes | QMessageBox::No);
        box.show();
        QCoreApplication::processEvents();
        check("a message box shows the app's Yes and No",
              box.button(QMessageBox::Yes)->text() == QStringLiteral("&Yes")
                  && box.button(QMessageBox::No)->text() == QStringLiteral("&No"),
              box.button(QMessageBox::Yes)->text() + " / " + box.button(QMessageBox::No)->text());
        box.hide();
    }

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
