#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QString>
#include <QStyle>
#include <QStyleFactory>

namespace theme {
namespace {

// Captured once at startup so "Light mode" restores the exact native style +
// palette, not an approximation.
bool      s_captured = false;
QString   s_lightStyle;     // platform default style name (e.g. "Breeze")
QPalette  s_lightPalette;

QPalette darkPalette()
{
    // Standard Fusion dark palette: mid-grey surfaces, light text, blue
    // highlight. Disabled text/buttons dimmed so they still read as disabled.
    const QColor window(53, 53, 53);
    const QColor base(35, 35, 35);
    const QColor alt(45, 45, 45);
    const QColor text(220, 220, 220);
    const QColor disabled(127, 127, 127);
    const QColor highlight(42, 130, 218);
    const QColor button(53, 53, 53);

    QPalette p;
    p.setColor(QPalette::Window,          window);
    p.setColor(QPalette::WindowText,      text);
    p.setColor(QPalette::Base,            base);
    p.setColor(QPalette::AlternateBase,   alt);
    p.setColor(QPalette::ToolTipBase,     window);
    p.setColor(QPalette::ToolTipText,     text);
    p.setColor(QPalette::Text,            text);
    p.setColor(QPalette::Button,          button);
    p.setColor(QPalette::ButtonText,      text);
    p.setColor(QPalette::BrightText,      Qt::red);
    p.setColor(QPalette::Link,            highlight);
    p.setColor(QPalette::Highlight,       highlight);
    p.setColor(QPalette::HighlightedText, Qt::black);
    p.setColor(QPalette::PlaceholderText, disabled);

    p.setColor(QPalette::Disabled, QPalette::WindowText,      disabled);
    p.setColor(QPalette::Disabled, QPalette::Text,            disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,      disabled);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    return p;
}

} // namespace

void captureDefault()
{
    if (s_captured) return;
    s_captured = true;
    if (auto *s = qApp->style())
        s_lightStyle = s->objectName();   // current/native style name
    s_lightPalette = qApp->palette();
}

void applyTheme(bool dark)
{
    // If captureDefault() never ran, capture now so the light branch has
    // something to restore to.
    if (!s_captured) captureDefault();

    if (dark) {
        if (auto *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
            qApp->setStyle(fusion);
        qApp->setPalette(darkPalette());
    } else {
        bool styleOk = false;
        if (!s_lightStyle.isEmpty()) {
            if (auto *s = QStyleFactory::create(s_lightStyle)) {
                qApp->setStyle(s);
                styleOk = true;
            }
        }
        // If the native style can't be re-created (empty/unknown objectName on
        // some platform), don't leave the dark session's Fusion style wearing
        // a light palette - that half-state is visible. Fusion renders
        // s_lightPalette cleanly, so fall back to it.
        if (!styleOk
            && qApp->style()->objectName().compare(
                   QStringLiteral("fusion"), Qt::CaseInsensitive) != 0) {
            if (auto *fusion = QStyleFactory::create(QStringLiteral("Fusion")))
                qApp->setStyle(fusion);
        }
        qApp->setPalette(s_lightPalette);
    }
}

bool backgroundIsDark(bool darkMode)
{
    if (darkMode) return true;            // our dark palette is always dark
    if (!s_captured) captureDefault();    // light mode = the captured default
    return s_lightPalette.color(QPalette::Window).lightness() < 128;
}

} // namespace theme
