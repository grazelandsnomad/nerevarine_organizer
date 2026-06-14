// tests/test_separator_theme.cpp
//
// Regression tests for separator_theme::resolve - the light/dark colour
// resolution for separator rows.
//
// Why these exist:
//   The dark-mode separator transform was re-tuned repeatedly (darken the
//   default, then darken every separator, ...).  One of those passes
//   ("darken ALL separators in dark mode") started OVERRIDING the user's
//   stored label colour unconditionally, so a user who had picked a readable
//   custom text colour saw it silently swapped to a fixed light grey on the
//   next update - reported as "my separator text colour changed, but the
//   background stayed the same".  These tests lock the rule that a readable
//   custom label colour survives the dark-mode switch and only a genuinely
//   unreadable one is replaced.

#include "separator_theme.h"

#include <QColor>
#include <QString>
#include <cmath>
#include <iostream>

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

static bool approx(double a, double b, double eps = 0.02)
{
    return std::fabs(a - b) <= eps;
}

// -- Scenarios ---

static void testContrastRatioSanity()
{
    std::cout << "\n[WCAG contrast ratio sanity]\n";
    check("black vs white is ~21:1",
          approx(separator_theme::contrastRatio(Qt::black, Qt::white), 21.0, 0.3));
    check("identical colours are 1:1",
          approx(separator_theme::contrastRatio(QColor(80, 90, 100),
                                                QColor(80, 90, 100)), 1.0));
    check("ratio is symmetric",
          approx(separator_theme::contrastRatio(Qt::black, Qt::white),
                 separator_theme::contrastRatio(Qt::white, Qt::black)));
}

static void testLightModePassthrough()
{
    std::cout << "\n[light mode: stored colours pass straight through]\n";
    const QColor bg(240, 210, 225);   // pale pink
    const QColor fg(60, 20, 40);      // dark maroon
    const auto out = separator_theme::resolve(bg, fg, /*darkUi=*/false);
    check("light-mode background unchanged", out.bg == bg);
    check("light-mode label unchanged",      out.fg == fg);
}

static void testInvalidColoursUseDefaults()
{
    std::cout << "\n[invalid stored colours fall back to the defaults]\n";
    const auto out = separator_theme::resolve(QColor(), QColor(), /*darkUi=*/false);
    check("invalid background -> default blue-grey",
          out.bg == separator_theme::defaultBg());
    check("invalid label -> default white",
          out.fg == separator_theme::defaultFg());
}

static void testDarkModeDarkensPaleBackground()
{
    std::cout << "\n[dark mode: a pale background is pulled down to the dark band]\n";
    const QColor bg(240, 210, 225);   // pale pink, HSL lightness ~0.88
    const auto out = separator_theme::resolve(bg, QColor(60, 20, 40), /*darkUi=*/true);
    check("pale background darkened to ~0.18 lightness",
          approx(out.bg.lightnessF(), 0.18, 0.03),
          QString::number(out.bg.lightnessF()));
    check("darkened background keeps its hue (still pink-ish)",
          out.bg.hslHueF() >= 0.0 && approx(out.bg.hslHueF(), bg.hslHueF(), 0.04),
          QString("hue %1 vs %2").arg(out.bg.hslHueF()).arg(bg.hslHueF()));
}

static void testDarkModeKeepsAlreadyDarkBackground()
{
    std::cout << "\n[dark mode: an already-dark background is left untouched]\n";
    // The reported user's case: their separator background was already dark, so
    // it "stayed the same" - only the text colour was getting changed.
    const QColor bg(32, 32, 40);      // HSL lightness ~0.14, below the 0.18 band
    const auto out = separator_theme::resolve(bg, QColor(255, 204, 0), /*darkUi=*/true);
    check("already-dark background is unchanged", out.bg == bg,
          QString("%1 -> %2").arg(bg.name(), out.bg.name()));
}

static void testDarkModeKeepsReadableCustomLabel()
{
    std::cout << "\n[dark mode: a readable custom label colour SURVIVES (the fix)]\n";
    // Dark background + a bright custom label the user deliberately chose.  This
    // is exactly what used to get clobbered; it must now be preserved.
    const QColor bg(32, 32, 40);
    const QColor fg(255, 204, 0);     // gold
    const auto out = separator_theme::resolve(bg, fg, /*darkUi=*/true);
    check("readable custom label is kept, not overridden", out.fg == fg,
          QString("got %1").arg(out.fg.name()));
    check("its readability really is above the 3:1 bar",
          separator_theme::contrastRatio(fg, out.bg) >= 3.0);
}

static void testDarkModeReplacesUnreadableLabel()
{
    std::cout << "\n[dark mode: an unreadable label IS replaced (readability safety)]\n";
    // Dark text authored for a pale background.  Once the background is darkened
    // out from under it the text would vanish, so it falls back to a light label.
    const QColor bg(240, 225, 210);   // cream (light)
    const QColor fg(28, 28, 28);      // near-black text
    const auto out = separator_theme::resolve(bg, fg, /*darkUi=*/true);
    check("unreadable dark label replaced with the light fallback",
          out.fg == separator_theme::darkModeFallbackFg(),
          QString("got %1").arg(out.fg.name()));
}

static void testDarkModeDefaultStaysReadable()
{
    std::cout << "\n[dark mode: the default white-on-blue stays readable]\n";
    // Never-customised separator (invalid stored colours -> blue-grey + white).
    const auto out = separator_theme::resolve(QColor(), QColor(), /*darkUi=*/true);
    check("default white label clears the 3:1 bar and is kept",
          out.fg == separator_theme::defaultFg(),
          QString("got %1").arg(out.fg.name()));
}

int main(int, char **)
{
    std::cout << "=== separator_theme ===\n";
    testContrastRatioSanity();
    testLightModePassthrough();
    testInvalidColoursUseDefaults();
    testDarkModeDarkensPaleBackground();
    testDarkModeKeepsAlreadyDarkBackground();
    testDarkModeKeepsReadableCustomLabel();
    testDarkModeReplacesUnreadableLabel();
    testDarkModeDefaultStaysReadable();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
