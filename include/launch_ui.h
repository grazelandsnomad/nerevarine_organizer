#ifndef LAUNCH_UI_H
#define LAUNCH_UI_H

// launch_ui - what the Start button looks like while a game is up.
//
// Pressing Start used to leave the button untouched, so nothing stopped a
// second and third click and each one launched again. The gap is not small: a
// deploy-on-launch can put a progress dialog up first, and the Steam path can
// wait behind its own dialog for most of a minute before the game appears.
//
// The decision lives here, away from the widget, because the widget half is
// not testable: WindowShell exposes no toolbar action and the toolbar's
// appearance is set with stylesheets.
//
// Filled rather than tinted, which is the same reasoning as the Check Updates
// button: a background colour reads the same on either theme, while coloured
// text has to be re-derived against the surface every time the theme changes.

#include <QString>

namespace launch_ui {

struct Look {
    QString textKey;   // translation key for the button's label
    QString qss;       // stylesheet, empty = leave the button's own styling be
    bool    enabled;   // false while a launch is under way
};

// Grey, with the word in blue, and not clickable. The `:disabled` rule is not
// decoration: a disabled QToolButton is greyed out by the style itself, which
// would throw the blue away.
inline Look look(bool running)
{
    if (!running) return {QStringLiteral("toolbar_launch_game"), QString(), true};
    static const QString kRunningQss = QStringLiteral(
        "QToolButton {"
        "  background-color: #4a4f57;"
        "  color: #8ec9ff;"
        "  font-weight: bold;"
        "  border-radius: 4px;"
        "  padding: 3px 10px;"
        "}"
        "QToolButton:disabled {"
        "  background-color: #4a4f57;"
        "  color: #8ec9ff;"
        "}");
    return {QStringLiteral("toolbar_launch_running"), kRunningQss, false};
}

// How long to hold the running state when nothing will tell us it ended.
//
// A launch handed to Heroic, to a bare exe or to OpenGothic is detached and
// never reports back, so the only safe thing is a short hold: long enough that
// a double click cannot get through, short enough that nobody waits on it.
//
// A monitored launch reports its own end and waits for that. It still gets a
// backstop, because a lost signal must not stand the button down for the rest
// of the session. Note what "monitored" buys on each path: OpenMW really is a
// QProcess and really does see the quit; the Steam watch only polls /proc and
// gives its verdict within 150 s, so there the button comes back once the app
// stops knowing anything, not when the game closes.
inline int resetTimeoutMs(bool monitored)
{
    return monitored ? 10 * 60 * 1000 : 30 * 1000;
}

} // namespace launch_ui

#endif // LAUNCH_UI_H
