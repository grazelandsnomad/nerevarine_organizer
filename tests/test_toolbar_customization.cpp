// ToolbarCustomization: a toolbar entry's two gates (the profile's and the
// user's) and the user's half of it by id, which the View menu flips and the
// Customize dialog writes through. Plus the setting the View menu's other
// toggle keeps. Real QSettings, isolated by the test mode and an organisation
// name of their own, as test_settings does.

#include "settings.h"
#include "toolbar_customization.h"

#include <QAction>
#include <QApplication>
#include <QSettings>
#include <QStandardPaths>

#include <iostream>

#include "test_harness.h"

static void testRegisterReadsTheSavedChoice()
{
    QSettings().clear();
    ToolbarCustomization tb;
    QAction hidden(QStringLiteral("hidden by default"));
    tb.registerAction(QStringLiteral("a"), &hidden, QStringLiteral("A"),
                      /*defaultVisible=*/false);
    check("a default-hidden entry starts hidden", !hidden.isVisible());
    check("and reads as not user-visible", !tb.userVisible(QStringLiteral("a")));

    QAction shown(QStringLiteral("shown by default"));
    tb.registerAction(QStringLiteral("b"), &shown, QStringLiteral("B"));
    check("a default-shown entry starts shown",
          shown.isVisible() && tb.userVisible(QStringLiteral("b")));

    Settings::setToolbarActionVisible(QStringLiteral("c"), false);
    QAction saved(QStringLiteral("saved off"));
    tb.registerAction(QStringLiteral("c"), &saved, QStringLiteral("C"));
    check("a saved choice beats the default",
          !saved.isVisible() && !tb.userVisible(QStringLiteral("c")));
}

static void testSetUserVisibleAppliesAndPersists()
{
    QSettings().clear();
    ToolbarCustomization tb;
    QAction act(QStringLiteral("move"));
    tb.registerAction(QStringLiteral("move_mods"), &act, QStringLiteral("Move"),
                      /*defaultVisible=*/false);

    tb.setUserVisible(QStringLiteral("move_mods"), true);
    check("switching an entry on shows its action", act.isVisible());
    check("and reads back as on", tb.userVisible(QStringLiteral("move_mods")));
    check("and is saved under toolbar/<id>",
          Settings::toolbarActionVisible(QStringLiteral("move_mods"), false));

    tb.setUserVisible(QStringLiteral("move_mods"), false);
    check("switching it off hides it again",
          !act.isVisible() && !tb.userVisible(QStringLiteral("move_mods")));
    check("and saves that too",
          !Settings::toolbarActionVisible(QStringLiteral("move_mods"), true));
}

static void testProfileGateStillWins()
{
    QSettings().clear();
    ToolbarCustomization tb;
    QAction act(QStringLiteral("loot"));
    act.setProperty("nerev_profile_visible", false);
    tb.registerAction(QStringLiteral("sort_loot"), &act, QStringLiteral("LOOT"));
    tb.setUserVisible(QStringLiteral("sort_loot"), true);
    check("a profile without the feature hides it whatever the user chose",
          !act.isVisible() && tb.userVisible(QStringLiteral("sort_loot")));
    act.setProperty("nerev_profile_visible", true);
    tb.applyAll();
    check("and a profile with it shows it", act.isVisible());
}

static void testUnregisteredIdIsOnlySaved()
{
    QSettings().clear();
    ToolbarCustomization tb;
    check("an id nothing registered reads the saved value, off by default",
          !tb.userVisible(QStringLiteral("later")));
    tb.setUserVisible(QStringLiteral("later"), true);
    check("switching it on saves it",
          Settings::toolbarActionVisible(QStringLiteral("later"), false)
          && tb.userVisible(QStringLiteral("later")));
    QAction act(QStringLiteral("later"));
    tb.registerAction(QStringLiteral("later"), &act, QStringLiteral("Later"),
                      /*defaultVisible=*/false);
    check("and a registration made afterwards picks it up", act.isVisible());
}

static void testTranslationToolsSetting()
{
    QSettings().clear();
    check("the translation tools are shown by default", Settings::translationToolsVisible());
    Settings::setTranslationToolsVisible(false);
    check("and stay hidden once switched off", !Settings::translationToolsVisible());
    Settings::setTranslationToolsVisible(true);
    check("and come back", Settings::translationToolsVisible());
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("nerevarine-test");
    QCoreApplication::setApplicationName("toolbar-customization-test");
    QStandardPaths::setTestModeEnabled(true);  // isolate QSettings access

    std::cout << "toolbar_customization\n";
    testRegisterReadsTheSavedChoice();
    testSetUserVisibleAppliesAndPersists();
    testProfileGateStillWins();
    testUnregisteredIdIsOnlySaved();
    testTranslationToolsSetting();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
