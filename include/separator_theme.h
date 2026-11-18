#pragma once

// separator_theme - resolves the on-screen background + label colours for a
// separator row from the user's stored choices and the active light/dark theme.
//
// Pulled out of ModListDelegate::paint() so the colour maths is unit-testable
// without a QPainter (QtGui-only: QColor, no Widgets).  The dark-mode transform
// here has been re-tuned repeatedly; keeping it in one tested place stops the
// next tweak from silently recolouring separators the user customised.

#include "color_contrast.h"

#include <QColor>
#include <Qt>

namespace separator_theme {

// Seed colours for a separator the user never recoloured - match what
// SeparatorDialog hands out for a fresh separator (blue-grey + white).
inline QColor defaultBg() { return QColor(55, 55, 75); }
inline QColor defaultFg() { return QColor(Qt::white); }

// Light label substituted in dark mode when the stored one cannot be made to
// read at any lightness. With the band pulled down to ~0.18 that is all but
// unreachable - it is the last resort, not the first.
inline QColor darkModeFallbackFg() { return QColor(214, 219, 230); }

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
// The stored label colour is KEPT whenever it still reads on that band.  When
// it does not - the classic case being dark text authored for a pale
// background we just darkened out from under it - it is LIGHTENED with its hue
// and saturation kept, just far enough to read, so a purple label stays purple.
// Separator text is bold + enlarged, so WCAG's 3:1 large-text threshold is the
// bar.  The fixed light fallback is used only if no lightness reads.  This is
// the fix for "my separator text colour changed in dark mode": the label used
// to be swapped for that fixed grey outright, so every pale preset's dark
// label (Audio, Characters, ...) lost its colour on the theme switch.
inline Colors resolve(QColor bg, QColor fg, bool darkUi)
{
    if (!bg.isValid()) bg = defaultBg();
    if (!fg.isValid()) fg = defaultFg();
    if (!darkUi) return {bg, fg};

    float h, s, l, a;
    bg.getHslF(&h, &s, &l, &a);
    if (l > 0.18f)
        bg = QColor::fromHslF(h, s, 0.18f, a);

    const QColor lit =
        color_contrast::lightenedToRead(fg, bg, color_contrast::kLargeTextRatio);
    fg = lit.isValid() ? lit : darkModeFallbackFg();
    return {bg, fg};
}

} // namespace separator_theme
