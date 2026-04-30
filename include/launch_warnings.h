#ifndef LAUNCH_WARNINGS_H
#define LAUNCH_WARNINGS_H

#include <QString>
#include <QStringList>

class ForbiddenModsRegistry;
class QListWidget;
class QWidget;

namespace launch_warnings {

struct Result {
    QStringList missingDeps;        // "Mod: Interface Reimagined - disabled"
    QStringList emptyInstalls;      // "Mod: no plugin files found on disk"
    QStringList forbiddenEnabled;   // "Mod: on the forbidden list (reason)"

    int total() const {
        return missingDeps.size() + emptyInstalls.size() + forbiddenEnabled.size();
    }
};

// Pure scan: walk the modlist and collect launch-blocking warnings. Missing-
// master plugins are intentionally NOT reported here - syncOpenMWConfig
// suppresses them at the cfg layer, so flagging them on launch would be a
// false alarm. The delegate still paints the per-row diamond for awareness.
Result scan(QListWidget *list,
            const ForbiddenModsRegistry *forbidden,
            const QString &gameId);

struct Choice {
    bool proceed;   // false → user picked Cancel
    bool suppress;  // true → "don't warn me this session" was checked
};

// Modal "Launch with warnings" dialog. Lists are capped at 15 entries each
// with a "(+N more)" tail so the dialog stays readable on huge modlists.
Choice showDialog(QWidget *parent, const Result &warnings);

// Combines isRebootPending() (Debian marker / Arch missing-modules) with a
// blocking warning dialog. Returns true if the launch should be refused.
bool refuseIfRebootPending(QWidget *parent);

} // namespace launch_warnings

#endif // LAUNCH_WARNINGS_H
