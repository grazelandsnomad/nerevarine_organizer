#ifndef STATE_RECOVERY_H
#define STATE_RECOVERY_H

// state_recovery - "this install has lost its lists", decided away from the
// widgets so it can be tested.
//
// A plain-zip build keeps its state next to the binary: modlist_*.txt,
// loadorder_*.txt, forbidden_mods_*.txt, nerevarine_prefs.ini (the rule is in
// user_state_paths.h). The settings - the profile list, the mods directory,
// the API key - live under ~/.config instead, and moving the app's folder
// does not touch them. So moving or re-extracting that folder without
// carrying the state files leaves the app knowing which game and which mods
// directory, with no list, and saying nothing at all: the first-run wizard
// does not fire, because the setting it reads survived the move, and the
// empty list is then saved into the new folder. A user reported exactly that
// - "I had to move Nerevarine Organizer's folder, which made it forget the
// entire modlist" - and had to ask how to get their mods back.
//
// What this reports is the signature of that, not a guess. All five have to
// hold: the app has run on this machine before, a profile is open, the list
// file it resolves to is absent, the mods directory is not empty, and no rows
// were loaded. A first run cannot match it (no settings yet), a genuinely
// empty library cannot, and neither can a list the user emptied themselves,
// since that still has a file.

namespace state_recovery {

inline bool listLostNotFirstRun(bool wizardCompleted, bool haveProfile,
                                bool modlistFileExists, bool modsDirHasFolders,
                                bool rowsEmpty)
{
    return wizardCompleted && haveProfile && !modlistFileExists
        && modsDirHasFolders && rowsEmpty;
}

} // namespace state_recovery

#endif // STATE_RECOVERY_H
