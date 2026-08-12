#pragma once

// separator_theme - resolves the on-screen background + label colours for a
// separator row from the user's stored choices and the active light/dark theme.
//
// Pulled out of ModListDelegate::paint() so the colour maths is unit-testable
// without a QPainter (QtGui-only: QColor, no Widgets).  The dark-mode transform
// here has been re-tuned repeatedly; keeping it in one tested place stops the
// next tweak from silently recolouring separators the user customised.

#include <QColor>
#include <Qt>

#include <cmath>

namespace separator_theme {

// Seed colours for a separator the user never recoloured - match what
// SeparatorDialog hands out for a fresh separator (blue-grey + white).
inline QColor defaultBg() { return QColor(55, 55, 75); }
inline QColor defaultFg() { return QColor(Qt::white); }

// Light label substituted in dark mode when the stored one would be unreadable.
inline QColor darkModeFallbackFg() { return QColor(214, 219, 230); }

// WCAG relative luminance of a colour, in [0, 1] (0 = black, 1 = white).
inline double relativeLuminance(const QColor &c)
{
    auto lin = [](double ch) {
        return ch <= 0.03928 ? ch / 12.92
                             : std::pow((ch + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.redF())
         + 0.7152 * lin(c.greenF())
         + 0.0722 * lin(c.blueF());
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

struct Colors {
    QColor bg;
    QColor fg;
};

// Resolve (background, label) for a separator given the user's stored colours
// (an invalid QColor means "never customised" -> use the default) and whether
// the active palette is dark.
//
// Light mode: stored colours pass straight through - the list looks exactly as
// the user authored it.
//
// Dark mode: every separator's background is pulled DOWN to a uniform dark band
// (~0.18 HSL lightness) while keeping its hue + saturation, so an authored
// pink/cream/blue section stays recognisable but belongs in a dark UI.  Only
// pulls down - a background the user already made dark is left untouched (this
// is why a user with an already-dark separator sees its background unchanged).
//
// The stored label colour is KEPT whenever it still reads on that band, and is
// only swapped for a light default when it wouldn't - the classic unreadable
// case being dark text authored for a pale background we just darkened out from
// under it.  Separator text is bold + enlarged, so WCAG's 3:1 large-text
// contrast threshold is the bar.  This is the fix for "my separator text colour
// changed in dark mode": a readable custom colour now survives the theme switch
// instead of being clobbered unconditionally.
inline Colors resolve(QColor bg, QColor fg, bool darkUi)
{
    if (!bg.isValid()) bg = defaultBg();
    if (!fg.isValid()) fg = defaultFg();
    if (!darkUi) return {bg, fg};

    float h, s, l, a;
    bg.getHslF(&h, &s, &l, &a);
    if (l > 0.18f)
        bg = QColor::fromHslF(h, s, 0.18f, a);

    if (contrastRatio(fg, bg) < 3.0)
        fg = darkModeFallbackFg();
    return {bg, fg};
}

} // namespace separator_theme
