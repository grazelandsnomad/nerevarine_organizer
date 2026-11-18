// test_separator_theme - the colour maths behind a separator's on-screen
// colours (separator_theme.h) and the contrast helper it shares with the
// toolbar (color_contrast.h). QtGui only: no window, no painter.

#include "color_contrast.h"
#include "separator_theme.h"

#include <QColor>
#include <QString>

#include <cmath>
#include <iostream>

#include "test_harness.h"

namespace {

QString hex(const QColor &c)
{
    return c.isValid() ? c.name() : QStringLiteral("<invalid>");
}

// Two of SeparatorDialog's pale presets with a dark label - the pairing that
// dark mode used to break by swapping the label for a fixed grey.
const QColor kAudioBg(QStringLiteral("#EDD9F5"));
const QColor kAudioFg(QStringLiteral("#6B1A92"));
const QColor kCharactersBg(QStringLiteral("#F5D9EE"));
const QColor kCharactersFg(QStringLiteral("#7A1A5C"));
// A preset that is already dark on both sides.
const QColor kTexturesBg(QStringLiteral("#1A1F2E"));
const QColor kTexturesFg(QStringLiteral("#EC4899"));
// A dark band with a light label.
const QColor kArmorBg(QStringLiteral("#6B1A1A"));
const QColor kArmorFg(QStringLiteral("#E8D4C8"));

int hueDistance(const QColor &a, const QColor &b)
{
    const int d = std::abs(a.hslHue() - b.hslHue());
    return d > 180 ? 360 - d : d;
}

void testContrastMaths()
{
    using namespace color_contrast;
    check("white on black is 21:1",
          std::fabs(contrastRatio(Qt::white, Qt::black) - 21.0) < 0.01);
    check("a colour against itself is 1:1",
          std::fabs(contrastRatio(kAudioFg, kAudioFg) - 1.0) < 1e-9);
    check("the ratio is symmetric", std::fabs(contrastRatio(kAudioFg, kAudioBg) -
                                              contrastRatio(kAudioBg, kAudioFg)) < 1e-9);
    check("the Audio preset reads in light mode as authored",
          contrastRatio(kAudioFg, kAudioBg) >= kLargeTextRatio);
}

void testLightenedToRead()
{
    using namespace color_contrast;
    const QColor band = separator_theme::resolve(kAudioBg, Qt::white, true).bg;

    const QColor fine = lightenedToRead(Qt::white, band, kLargeTextRatio);
    check("a label that already reads comes back untouched", fine == QColor(Qt::white),
          hex(fine));

    const QColor lit = lightenedToRead(kAudioFg, band, kLargeTextRatio);
    check("a dark label is lightened to a valid colour", lit.isValid(), hex(lit));
    check("the lightened label reads at 3:1 on the band",
          contrastRatio(lit, band) >= kLargeTextRatio,
          QString::number(contrastRatio(lit, band)));
    check("the lightened label keeps its hue", hueDistance(lit, kAudioFg) <= 2,
          QStringLiteral("%1 vs %2").arg(lit.hslHue()).arg(kAudioFg.hslHue()));
    check("the lightened label keeps its saturation",
          std::fabs(lit.hslSaturationF() - kAudioFg.hslSaturationF()) < 0.02);
    check("the lightened label is paler than the original",
          lit.lightnessF() > kAudioFg.lightnessF());
    check("it is lightened just far enough, not to white",
          contrastRatio(lit, band) < kLargeTextRatio + 1.0,
          QString::number(contrastRatio(lit, band)));

    const QColor grey = lightenedToRead(QColor(0x30, 0x30, 0x30),
                                        QColor(0x2e, 0x2e, 0x2e), kLargeTextRatio);
    check("an achromatic label lightens to a readable grey",
          grey.isValid() && grey.hslSaturationF() < 0.01 &&
              contrastRatio(grey, QColor(0x2e, 0x2e, 0x2e)) >= kLargeTextRatio,
          hex(grey));

    const QColor none = lightenedToRead(QColor(0x80, 0x80, 0x80), Qt::white, 21.0);
    check("an impossible pairing comes back invalid, not a surprise colour",
          !none.isValid(), hex(none));
}

void testLightModePassesThrough()
{
    const auto c = separator_theme::resolve(kAudioBg, kAudioFg, false);
    check("light mode keeps the authored background", c.bg == kAudioBg, hex(c.bg));
    check("light mode keeps the authored label", c.fg == kAudioFg, hex(c.fg));

    const auto d = separator_theme::resolve(QColor(), QColor(), false);
    check("a never-customised separator gets the default background",
          d.bg == separator_theme::defaultBg(), hex(d.bg));
    check("a never-customised separator gets the default label",
          d.fg == separator_theme::defaultFg(), hex(d.fg));
}

void testDarkModeBand()
{
    const auto c = separator_theme::resolve(kAudioBg, kAudioFg, true);
    check("a pale background is pulled down to the dark band",
          std::fabs(c.bg.lightnessF() - 0.18f) < 0.01f,
          QString::number(c.bg.lightnessF()));
    check("the band keeps the background's hue", hueDistance(c.bg, kAudioBg) <= 2,
          QStringLiteral("%1 vs %2").arg(c.bg.hslHue()).arg(kAudioBg.hslHue()));

    const auto t = separator_theme::resolve(kTexturesBg, kTexturesFg, true);
    check("an already-dark background is not darkened further", t.bg == kTexturesBg,
          hex(t.bg));
    check("its light label is kept verbatim", t.fg == kTexturesFg, hex(t.fg));

    const auto a = separator_theme::resolve(kArmorBg, kArmorFg, true);
    check("a label that reads on the darkened band is kept verbatim", a.fg == kArmorFg,
          hex(a.fg));
}

void testDarkModeLightensTheLabelInsteadOfReplacingIt()
{
    using color_contrast::contrastRatio;
    using color_contrast::kLargeTextRatio;

    for (const auto &[name, bg, fg] :
         {std::tuple{"Audio", kAudioBg, kAudioFg},
          std::tuple{"Characters", kCharactersBg, kCharactersFg}}) {
        const auto    c   = separator_theme::resolve(bg, fg, true);
        const QString tag = QString::fromLatin1(name);
        check(qPrintable(tag + ": the label is not the fixed fallback grey"),
              c.fg != separator_theme::darkModeFallbackFg(), hex(c.fg));
        check(qPrintable(tag + ": the label reads on the band"),
              contrastRatio(c.fg, c.bg) >= kLargeTextRatio,
              QString::number(contrastRatio(c.fg, c.bg)));
        check(qPrintable(tag + ": the label keeps its hue"), hueDistance(c.fg, fg) <= 2,
              QStringLiteral("%1 vs %2").arg(c.fg.hslHue()).arg(fg.hslHue()));
        check(qPrintable(tag + ": the label is paler, not a different colour"),
              c.fg.lightnessF() > fg.lightnessF() &&
                  std::fabs(c.fg.hslSaturationF() - fg.hslSaturationF()) < 0.02,
              hex(c.fg));
    }

    const auto w = separator_theme::resolve(Qt::black, Qt::white, true);
    check("white on black stays white", w.fg == QColor(Qt::white), hex(w.fg));
}

// While any update is waiting or on its way, a separator wears the neutral
// grey instead of its colours - whatever they are - until the last one lands.
void testPendingUpdateWearsTheGreyUntilItLands()
{
    using color_contrast::contrastRatio;
    using color_contrast::kLargeTextRatio;
    const auto light = separator_theme::resolve(kAudioBg, kAudioFg, false, true);
    check("light mode: the pending band is the grey, not the preset",
          light.bg == QColor(210, 210, 210) && light.fg == QColor(40, 40, 40),
          hex(light.bg) + " / " + hex(light.fg));
    const auto dark = separator_theme::resolve(kAudioBg, kAudioFg, true, true);
    check("dark mode: a dark grey band with a light label",
          dark.bg == QColor(74, 74, 74) && dark.fg == QColor(230, 230, 230),
          hex(dark.bg) + " / " + hex(dark.fg));
    check("both pairs read", contrastRatio(light.fg, light.bg) >= kLargeTextRatio &&
                                 contrastRatio(dark.fg, dark.bg) >= kLargeTextRatio);
    const auto back = separator_theme::resolve(kAudioBg, kAudioFg, false, false);
    check("with nothing pending the authored colours are back",
          back.bg == kAudioBg && back.fg == kAudioFg, hex(back.bg));
    check("the default argument is 'nothing pending'",
          separator_theme::resolve(kAudioBg, kAudioFg, false).bg == kAudioBg);
}

} // namespace

int main()
{
    std::cout << "color_contrast\n";
    testContrastMaths();
    testLightenedToRead();
    std::cout << "separator_theme\n";
    testLightModePassesThrough();
    testDarkModeBand();
    testDarkModeLightensTheLabelInsteadOfReplacingIt();
    testPendingUpdateWearsTheGreyUntilItLands();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
