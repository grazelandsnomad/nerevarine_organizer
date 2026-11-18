#pragma once

// color_contrast - WCAG contrast maths, shared by everything that has to put
// a colour authored for one background onto another: a separator's label on
// the dark-mode band (separator_theme.h) and the coloured toolbar buttons on a
// dark desktop theme (mainwindow_setup.cpp). Header-only and QtGui-only
// (QColor), so it is unit-testable without a window.

#include <QColor>

#include <algorithm>
#include <cmath>

namespace color_contrast {

// WCAG relative luminance of a colour, in [0, 1] (0 = black, 1 = white).
inline double relativeLuminance(const QColor &c)
{
    auto lin = [](double ch) {
        return ch <= 0.03928 ? ch / 12.92 : std::pow((ch + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
}

// WCAG contrast ratio between two colours, in [1, 21].
inline double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double hi = la > lb ? la : lb;
    const double lo = la > lb ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

// WCAG's thresholds: 3:1 reads for large or bold text, 4.5:1 for body text.
inline constexpr double kLargeTextRatio = 3.0;
inline constexpr double kBodyTextRatio  = 4.5;

// `fg` lightened - hue and saturation kept - just far enough to read on `bg`
// at `minRatio`, or `fg` itself when it already does. A purple label stays
// purple; it only gets paler. Invalid when no lightness reads (a pale bg can
// leave no room), so the caller picks the fallback instead of being handed a
// surprise colour.
inline QColor lightenedToRead(const QColor &fg, const QColor &bg, double minRatio)
{
    if (contrastRatio(fg, bg) >= minRatio) return fg;
    float h, s, l, a;
    fg.getHslF(&h, &s, &l, &a);
    if (h < 0.0f) h = 0.0f; // achromatic: Qt reports -1, and the hue is moot
    for (int step = 1; step <= 50; ++step) {
        const float  ll = std::min(1.0f, l + 0.02f * static_cast<float>(step));
        const QColor c  = QColor::fromHslF(h, s, ll, a);
        if (contrastRatio(c, bg) >= minRatio) return c;
        if (ll >= 1.0f) break;
    }
    return {};
}

} // namespace color_contrast
