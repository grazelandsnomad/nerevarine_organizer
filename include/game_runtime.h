#ifndef GAME_RUNTIME_H
#define GAME_RUNTIME_H

// Which build of the game is installed, and is its script extender there?
//
// Skyrim answers both from the profile: AE and SE are separate game ids, so
// knowing which profile is active is knowing the runtime. Fallout 4 does not
// work that way - one id covers the original game and every post-next-gen
// build - so the only honest source is the install itself.
//
// Two facts, two cheap reads:
//
//   the game     Fallout4.exe states its own FileVersion. Measured on a real
//                install: 1.11.240.0, which is NOT 1.10.x - any rule written
//                around the April-2024 "1.10.980" split alone would read that
//                as the original game and recommend the wrong files.
//   the extender f4se_1_11_240.dll sits in the game root and names the runtime
//                it hooks in its own filename. Its PRESENCE is the answer to
//                "is F4SE installed", and it is the reliable one: script
//                extenders are installed straight into the game folder, not
//                through a mod manager, so a mod-list search reports them
//                missing on a machine that has one.
//
// Deliberately NOT skse_check::gather, which also walks every plugin DLL to
// judge staleness. This runs whenever a FOMOD opens; it reads two files.
//
// Path-explicit and FS-touching but global-free: every location is the
// caller's to resolve, so a test can point it at a temporary directory.

#include "pe_info.h"

#include <QString>

namespace game_runtime {

struct Probe {
    pe_info::Version game;              // from <gameRoot>/<exeName>
    pe_info::Version extender;          // from the loader DLL's own name
    bool             extenderPresent = false;
};

// `extenderPrefix` is the loader's stem - "f4se", "skse64", "obse" - matched
// against "<prefix>_*.dll" in the game root. An empty prefix skips that half.
Probe probe(const QString &gameRoot, const QString &exeName,
            const QString &extenderPrefix);

} // namespace game_runtime

#endif // GAME_RUNTIME_H
