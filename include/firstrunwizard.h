#ifndef FIRSTRUNWIZARD_H
#define FIRSTRUNWIZARD_H

// Four-page welcome dialog: game profile, mods directory, optional Nexus
// API key, and integration prompts (nxm:// handler, link to LOOT). The
// caller (MainWindow) reads the Result and applies side effects.

#include <QList>
#include <QString>

class QWidget;

namespace firstrun {

struct GameChoice {
    QString id;                 // Nexus slug, e.g. "morrowind"
    QString displayName;        // human-readable, e.g. "OpenMW (Morrowind)"
    QString defaultModsDirName; // e.g. "nerevarine_mods" - combined with $HOME/Games/
};

struct Result {
    QString gameId;
    QString modsDir;
    QString apiKey;        // empty = skipped
    bool    registerNxm = true;
};

// Modal run.  Returns true if the user clicked Finish, false on Cancel / close.
// `games` is the list the wizard's first page will offer; the caller normally
// passes MainWindow's built-in game table.
bool runWizard(QWidget *parent,
               const QList<GameChoice> &games,
               Result &out);

} // namespace firstrun

#endif // FIRSTRUNWIZARD_H
