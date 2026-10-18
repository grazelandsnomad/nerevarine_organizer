#pragma once

// fomod_facts - what a FOMOD's <fileDependency> asks of this profile: will the
// game, as this manager deploys it, hold this file, and load it?
//
// Necessity NG-AE asks it about forty times - its DLC group about the six DLC
// masters, its Bunker Hill fix about 3DNPC_FO4.esp, a patch about prp.esp and
// ppf.esm, the RobCo build about F4SE\Plugins\RobCo_Patcher.dll. The answer
// is the ticked mods plus the game's own files:
//
//   Data-folder games (Fallout 4, Skyrim, Starfield, Oblivion, ...): a plugin
//     at the root of a ticked mod's folder, or in Data/ without this manager
//     having put it there - the game's masters, DLC, Creation Club. Every
//     plugin this manager deploys it also activates, so there is no Inactive.
//     Any other path, under a ticked mod's folder or Data/ the same way.
//   OpenMW: the content files the ticked mods carry and the three base
//     masters; one held back from openmw.cfg for a missing master is
//     Inactive. Other paths are not answered.
//
// A file found is found. One not found is Missing only when the picture is
// complete - every ticked row has its folder, Data/ was found - and unknown
// otherwise: the same "empty or complete, never partial" rule the BAIN picker
// keeps, because a half-seen list makes a present mod look absent.
//
// Folders are listed on the first question, not before: most installers ask
// none. Qt Core only.

#include "fomod_conditions.h"

#include <QSet>
#include <QString>
#include <QStringList>

namespace fomod_facts {

struct Profile {
    enum class Layout {
        Unknown,      // a game this cannot speak for: nothing is answered
        DataFolder,   // plugins at the root of Data/
        OpenMW,       // content files in the mods' data folders
    };
    Layout      layout = Layout::Unknown;
    // The folders of the installed, ticked mods - what a deploy puts into the
    // game. `complete` is false when one of those rows has no folder.
    QStringList modRoots;
    bool        complete = true;

    // DataFolder: the game's Data/ ("" when it cannot be found) and the
    // Data-relative paths this manager deployed there, lower-cased with '/'.
    QString       dataDir;
    QSet<QString> deployed;

    // OpenMW: the content files the ticked mods carry, and the ones held back
    // from openmw.cfg for a missing master. Lower-cased file names.
    QSet<QString> content;
    QSet<QString> heldBack;
};

fomod::FileStateFn fileState(const Profile &profile);

} // namespace fomod_facts
