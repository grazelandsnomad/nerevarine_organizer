#ifndef LAUNCH_WARNINGS_H
#define LAUNCH_WARNINGS_H

#include "skse_check.h"

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
    // Script extender, filled in by addScriptExtender(). Notes are whole
    // sentences about the setup (the extender is for another game version, no
    // address library at all); stale rows are one per plugin that predates
    // the installed game, named by the mod that deployed it.
    QStringList scriptExtenderNotes;
    QStringList scriptExtenderStale;
    // The mod labels behind those rows, in the same order, so the caller can
    // find them in the list without picking a name back out of the text.
    QStringList scriptExtenderMods;
    // Why those rows are there, said once. Not counted and not a row: it is a
    // paragraph, and it goes in a label that wraps rather than in the
    // monospace list, which does not.
    QString     scriptExtenderExplain;

    int total() const {
        return missingDeps.size() + emptyInstalls.size() + forbiddenEnabled.size()
             + scriptExtenderNotes.size() + scriptExtenderStale.size();
    }
};

// Walk the modlist, collect launch-blocking warnings. Missing-master plugins
// are NOT reported: syncOpenMWConfig already suppresses them in the cfg, so
// warning on launch would be a false alarm (delegate still paints the diamond).
Result scan(QListWidget *list,
            const ForbiddenModsRegistry *forbidden,
            const QString &gameId);

// Turn a skse_check verdict into rows for the dialog. The wording lives here
// rather than in skse_check so the module stays pure and so every key stays a
// literal T("...") the parity check can find. A verdict with nothing in it
// adds nothing.
void addScriptExtender(Result &into, const skse_check::Findings &findings);

struct Choice {
    bool proceed;      // false → user picked Cancel
    bool suppress;     // true → "don't warn me this session" was checked
    // true → the user asked to check the listed mods for updates. Implies
    // proceed == false: the answer to "are there newer builds" is worth
    // having before starting the game, not after.
    bool checkUpdates = false;
};

// Modal "Launch with warnings" dialog. Each list caps at 15 entries with a
// "(+N more)" tail so huge modlists don't blow up the dialog.
Choice showDialog(QWidget *parent, const Result &warnings);

// isRebootPending() (Debian marker / Arch missing-modules) plus a blocking
// dialog. True means refuse the launch.
bool refuseIfRebootPending(QWidget *parent);

} // namespace launch_warnings

#endif // LAUNCH_WARNINGS_H
