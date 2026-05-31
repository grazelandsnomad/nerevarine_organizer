// tests/test_fomod_wizard_ui.cpp
//
// UI-level coverage for FomodWizard::buildUi - the widget-construction +
// smart-default pass that turns a parsed FOMOD step/group/plugin tree into
// radio buttons and checkboxes.
//
// Why this exists:
//   buildUi has no other test surface: it builds real QWidgets and applies a
//   stack of default-selection rules. Two bugs lived here unnoticed because
//   nothing exercised it -
//     · case-folder handling aside, SelectAtMostOne ("zero or one") was
//       rendered like SelectExactlyOne ("exactly one") and force-checked the
//       first option, so OAAB_Saplings' optional patch steps could not be
//       declined; and
//     · there was no control that returned such a group to "nothing selected".
//   These tests drive buildUi through a friend hook (FomodWizardTestHook) and
//   assert on the resulting button states, including the synthetic "None"
//   radio that SelectAtMostOne groups now grow.
//
// Runs headless via the offscreen QPA platform (set below), so it works in CI
// with no display.
//
// Build + run:
//   cmake --build build -j$(nproc) && ./build/tests/test_fomod_wizard_ui

#include "fomodwizard.h"

#include <QApplication>
#include <QAbstractButton>
#include <QByteArray>
#include <QRadioButton>
#include <QSet>
#include <QString>
#include <QWidget>

#include <iostream>

// -- Friend hook: reach buildUi() and the private button tree --------------

struct FomodWizardTestHook {
    static FomodWizard *build(const QList<FomodStep> &steps,
                              const QString &prior = {},
                              const QStringList &installed = {})
    {
        auto *w = new FomodWizard(QStringLiteral("/tmp/nrv_fomod_ui_test"));
        w->m_steps            = steps;
        w->m_priorChoices     = prior;
        w->m_installedModNames = installed;
        w->buildUi();
        return w;
    }

    static QAbstractButton *btn(FomodWizard *w, int si, int gi, int pi)
    { return w->m_buttons[si][gi][pi]; }

    static int pluginCount(FomodWizard *w, int si, int gi)
    { return w->m_buttons[si][gi].size(); }

    static QString collect(FomodWizard *w) { return w->collectChoices(); }
};

// -- Test scaffolding ------------------------------------------------------

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok, const QString &hint = {})
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name;
        if (!hint.isEmpty()) std::cout << " (" << hint.toStdString() << ")";
        std::cout << "\n";
        ++s_failed;
    }
}

static FomodPlugin mkPlugin(const QString &name, const QString &type = "Optional")
{
    FomodPlugin p;
    p.name = name;
    p.type = type;
    return p;
}

static FomodGroup mkGroup(const QString &type, const QList<FomodPlugin> &plugins)
{
    FomodGroup g;
    g.name    = QStringLiteral("Group");
    g.type    = type;
    g.plugins = plugins;
    return g;
}

static QList<FomodStep> oneGroup(const FomodGroup &g)
{
    FomodStep s;
    s.name = QStringLiteral("Step");
    s.groups.append(g);
    return { s };
}

// The synthetic "None" radio is a QRadioButton child of the group box that is
// NOT one of the plugin buttons. Returns nullptr when the group has no such
// radio (SelectExactlyOne, or the checkbox group types).
static QRadioButton *findNoneRadio(FomodWizard *w, int si, int gi)
{
    const int n = FomodWizardTestHook::pluginCount(w, si, gi);
    if (n == 0) return nullptr;
    QWidget *box = FomodWizardTestHook::btn(w, si, gi, 0)->parentWidget();
    if (!box) return nullptr;
    QSet<QAbstractButton *> plugins;
    for (int pi = 0; pi < n; ++pi)
        plugins.insert(FomodWizardTestHook::btn(w, si, gi, pi));
    const auto radios = box->findChildren<QRadioButton *>();
    for (QRadioButton *r : radios)
        if (!plugins.contains(r)) return r;
    return nullptr;
}

// -- Scenarios -------------------------------------------------------------

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    std::cout << "=== fomod_wizard_ui (buildUi) tests ===\n";

    // 1. SelectAtMostOne with nothing required -> starts on "none". This is
    //    the OAAB_Saplings regression.
    {
        std::cout << "\n[SelectAtMostOne defaults to none]\n";
        auto *w = FomodWizardTestHook::build(
            oneGroup(mkGroup("SelectAtMostOne",
                             { mkPlugin("Alpha"), mkPlugin("Beta") })));
        check("no plugin auto-checked (A)",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("no plugin auto-checked (B)",
              !FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        QRadioButton *none = findNoneRadio(w, 0, 0);
        check("a None radio exists", none != nullptr);
        check("None is selected by default", none && none->isChecked());
        check("nothing serialized while None is active",
              FomodWizardTestHook::collect(w).isEmpty(),
              FomodWizardTestHook::collect(w));
        delete w;
    }

    // 2. SelectAtMostOne exclusivity: picking a plugin clears None and vice
    //    versa (confirms None joined the same exclusive button group).
    {
        std::cout << "\n[SelectAtMostOne None <-> plugin are mutually exclusive]\n";
        auto *w = FomodWizardTestHook::build(
            oneGroup(mkGroup("SelectAtMostOne",
                             { mkPlugin("Alpha"), mkPlugin("Beta") })));
        QRadioButton *none = findNoneRadio(w, 0, 0);
        check("precondition: None starts on", none && none->isChecked());
        FomodWizardTestHook::btn(w, 0, 0, 0)->setChecked(true); // user picks Alpha
        check("picking a plugin clears None", none && !none->isChecked());
        check("the picked plugin becomes the serialized choice",
              FomodWizardTestHook::collect(w) == QLatin1String("0:0:0"),
              FomodWizardTestHook::collect(w));
        delete w;
    }

    // 3. SelectAtMostOne with a Recommended plugin -> that plugin is on, None
    //    exists but is off.
    {
        std::cout << "\n[SelectAtMostOne honours a Recommended default]\n";
        auto *w = FomodWizardTestHook::build(
            oneGroup(mkGroup("SelectAtMostOne",
                             { mkPlugin("Alpha"),
                               mkPlugin("Beta", "Recommended") })));
        check("recommended plugin checked",
              FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        QRadioButton *none = findNoneRadio(w, 0, 0);
        check("None radio still present", none != nullptr);
        check("None NOT selected when a plugin is recommended",
              none && !none->isChecked());
        delete w;
    }

    // 4. SelectExactlyOne -> first selectable forced on, and NO None radio
    //    (a choice there is genuinely mandatory).
    {
        std::cout << "\n[SelectExactlyOne forces a pick and offers no None]\n";
        auto *w = FomodWizardTestHook::build(
            oneGroup(mkGroup("SelectExactlyOne",
                             { mkPlugin("Alpha"), mkPlugin("Beta") })));
        check("first option forced on",
              FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("second option off",
              !FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        check("no None radio", findNoneRadio(w, 0, 0) == nullptr);
        delete w;
    }

    // 5. SelectExactlyOne with a NotUsable first option -> forced selection
    //    skips to the next usable one (and it's disabled).
    {
        std::cout << "\n[SelectExactlyOne skips a NotUsable first option]\n";
        auto *w = FomodWizardTestHook::build(
            oneGroup(mkGroup("SelectExactlyOne",
                             { mkPlugin("Alpha", "NotUsable"),
                               mkPlugin("Beta") })));
        check("NotUsable option disabled",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isEnabled());
        check("NotUsable option not checked",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("forced selection lands on the next usable option",
              FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        delete w;
    }

    // 6. SelectAll -> every plugin checked, no None.
    {
        std::cout << "\n[SelectAll checks everything]\n";
        auto *w = FomodWizardTestHook::build(
            oneGroup(mkGroup("SelectAll",
                             { mkPlugin("Alpha"), mkPlugin("Beta") })));
        check("all checked (A)", FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("all checked (B)", FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        check("no None radio", findNoneRadio(w, 0, 0) == nullptr);
        delete w;
    }

    // 7. SelectAny single plugin -> on by default; multi -> off by default.
    {
        std::cout << "\n[SelectAny default-on only for a lone plugin]\n";
        auto *w1 = FomodWizardTestHook::build(
            oneGroup(mkGroup("SelectAny", { mkPlugin("Alpha") })));
        check("single SelectAny plugin defaults on",
              FomodWizardTestHook::btn(w1, 0, 0, 0)->isChecked());
        delete w1;

        auto *w2 = FomodWizardTestHook::build(
            oneGroup(mkGroup("SelectAny",
                             { mkPlugin("Alpha"), mkPlugin("Beta") })));
        check("multi SelectAny A defaults off",
              !FomodWizardTestHook::btn(w2, 0, 0, 0)->isChecked());
        check("multi SelectAny B defaults off",
              !FomodWizardTestHook::btn(w2, 0, 0, 1)->isChecked());
        delete w2;
    }

    // 8. Required plugin -> checked and disabled.
    {
        std::cout << "\n[Required plugin is forced on and locked]\n";
        auto *w = FomodWizardTestHook::build(
            oneGroup(mkGroup("SelectAny", { mkPlugin("Alpha", "Required") })));
        check("required plugin checked",
              FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("required plugin disabled",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isEnabled());
        delete w;
    }

    // 9. Smart default still works: OpenMW is preferred over MGE XE in an
    //    exclusive group, and such a group still has no None.
    {
        std::cout << "\n[smart default: OpenMW wins over MGE XE]\n";
        auto *w = FomodWizardTestHook::build(
            oneGroup(mkGroup("SelectExactlyOne",
                             { mkPlugin("MGE XE version"),
                               mkPlugin("OpenMW version") })));
        check("OpenMW option chosen",
              FomodWizardTestHook::btn(w, 0, 0, 1)->isChecked());
        check("MGE option not chosen",
              !FomodWizardTestHook::btn(w, 0, 0, 0)->isChecked());
        check("still no None radio (SelectExactlyOne)",
              findNoneRadio(w, 0, 0) == nullptr);
        delete w;
    }

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
