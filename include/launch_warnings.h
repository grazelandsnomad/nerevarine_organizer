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

// Walk the modlist, collect launch-blocking warnings. Missing-master plugins
// are NOT reported: syncOpenMWConfig already suppresses them in the cfg, so
// warning on launch would be a false alarm (delegate still paints the diamond).
Result scan(QListWidget *list,
            const ForbiddenModsRegistry *forbidden,
            const QString &gameId);

struct Choice {
    bool proceed;   // false → user picked Cancel
    bool suppress;  // true → "don't warn me this session" was checked
};

// Modal "Launch with warnings" dialog. Each list caps at 15 entries with a
// "(+N more)" tail so huge modlists don't blow up the dialog.
Choice showDialog(QWidget *parent, const Result &warnings);

// isRebootPending() (Debian marker / Arch missing-modules) plus a blocking
// dialog. True means refuse the launch.
bool refuseIfRebootPending(QWidget *parent);

} // namespace launch_warnings

#endif // LAUNCH_WARNINGS_H
