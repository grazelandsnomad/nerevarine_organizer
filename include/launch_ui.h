#ifndef LAUNCH_UI_H
#define LAUNCH_UI_H

// launch_ui - what the launch buttons look like while they are busy.
//
// Pressing Start used to leave the button untouched, so nothing stopped a
// second and third click and each one launched again. The gap is not small: a
// deploy-on-launch can put a progress dialog up first, and the Steam path can
// wait behind its own dialog for most of a minute before the game appears.
// The Launcher button had the same gap for a different reason: REDprelauncher
// takes a while to show under Proton, and every click in between started
// another one.
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

// Start (and its OpenMW twin) or Launcher (and its openmw-launcher twin). One
// busy face, two words: "Running" would be a lie about a launcher that is
// still coming up, so that button says "Launching".
enum class Button { Start, Launcher };

struct Look {
    QString textKey;   // translation key for the button's label
    QString tipKey;    // tooltip while busy; empty when idle
    QString qss;       // stylesheet, empty = leave the button's own styling be
    bool    enabled;   // false while a launch is under way
};

// Grey, with the word in blue, and not clickable. The `:disabled` rule is not
// decoration: a disabled QToolButton is greyed out by the style itself, which
// would throw the blue away.
inline Look look(Button button, bool busy)
{
    const bool start = button == Button::Start;
    if (!busy)
        return {start ? QStringLiteral("toolbar_launch_game")
                      : QStringLiteral("toolbar_launch_launcher"),
                QString(), QString(), true};
    static const QString kBusyQss = QStringLiteral(
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
    return {start ? QStringLiteral("toolbar_launch_running")
                  : QStringLiteral("toolbar_launcher_launching"),
            start ? QStringLiteral("toolbar_launch_running_tip")
                  : QStringLiteral("toolbar_launcher_launching_tip"),
            kBusyQss, false};
}

// The Start face, as the first callers spelled it.
inline Look look(bool running) { return look(Button::Start, running); }

// How long to hold the busy state when nothing will tell us it ended.
//
// A launch handed to Heroic, to a bare exe or to OpenGothic is detached and
// never reports back, so the only safe thing is a short hold: long enough that
// a double click cannot get through, short enough that nobody waits on it.
//
// A monitored launch reports its own end and waits for that. It still gets a
// backstop, because a lost signal must not stand the button down for the rest
// of the session. Note what "monitored" buys on each path: OpenMW really is a
// QProcess and really does see the quit; the Steam watch and the Launcher
// watch only poll /proc and give their verdict within 150 s, so there the
// button comes back once the app stops knowing anything, not necessarily when
// the window closes.
inline int resetTimeoutMs(bool monitored)
{
    return monitored ? 10 * 60 * 1000 : 30 * 1000;
}

} // namespace launch_ui

#endif // LAUNCH_UI_H
