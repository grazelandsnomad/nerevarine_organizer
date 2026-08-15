#ifndef FOMOD_HINT_H
#define FOMOD_HINT_H

// Is a FOMOD Yes/No question asking about a mod the user might already have?
//
// The wizard annotates Yes/No steps with a modlist verdict. The "present" half
// is safe: something in the modlist actually matched the step name, so the step
// demonstrably names a mod. The "absent" half is not - a step that matches
// nothing may simply not be about a mod at all. Dunmer Lanterns Replacer asks
// "Would you like the lanterns to use a glow effect?" and got back "No.
// Recommended. This mod is not currently present in the modlist", which reads
// as a missing dependency being reported and pre-picked the wrong answer.
//
// A failed lookup is not evidence of absence unless the thing looked up was a
// mod name, and the modlist cannot establish that for a mod it does not hold.
// The only evidence left is the wording: a question about what the user already
// has ("Do you use X?", "for users of X") is about another mod, a question
// about what the user would like is about this one. No phrasing match means no
// verdict - silence rather than a guess.

#include "nxmurl.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace fomod {

// True when the step/group wording asks about the user's existing setup rather
// than about an option of the mod being installed. Case- and
// whitespace-insensitive; the two strings are matched as one question.
bool asksAboutAnotherMod(const QString &stepName, const QString &groupName);

// -- Compatibility options for mods the user does not have ------------
//
// Plenty of installers offer "compatibility" options whose whole purpose is
// another mod: An Addendum to Tamrielic Lore Data ships Ashfall-compatible
// meshes, and pre-ticks a Glass Glowset option too. Tick those without the
// target mod and you install meshes nothing will ever load, on top of files
// that other mods may want - a silent mess.
//
// The wording heuristic above cannot help here: the group is called "Ashfall"
// or "Compatibility Options", and a name that matches nothing in the modlist
// is no evidence at all - "Normal Maps" matches nothing either, and is not a
// mod. What IS evidence is a citation. These installers routinely link the
// mod they are for, right in the option description:
//
//   "Installs Ashfall (https://www.nexusmods.com/morrowind/mods/49057)
//    compatible meshes. Use the HD version for the HD meshes."
//
// A Nexus mod-page URL names one specific mod page and nothing else, so it
// can be matched against the modlist by id - no name matching, no variant
// expansion, no false hits. Options that cite nothing get no verdict, which
// is what keeps "Normal Maps" and every ordinary option quiet.

// Every Nexus mod page cited in a block of text (an option description), in
// order of appearance, deduplicated by (game, modId). Empty for the common
// case of a description that cites nothing - and an empty result must be read
// as "no evidence", never as "no dependency".
QList<NexusModRef> citedMods(const QString &text);

// The best name to call a missing mod, given the names of the options that
// cite it and the name of the group holding them. Installers split the same
// mod across options in two shapes, and one rule covers both: the longest
// common prefix of the option names ("Ashfall" + "Ashfall (HD)" -> "Ashfall"),
// falling back to the group name when the options share nothing worth showing
// ("Core" + "HD" under a group called "Ashfall"). Empty when neither yields
// anything usable, and the caller should then word the warning without a name.
QString missingModLabel(const QStringList &optionNames, const QString &groupName);

// -- Skyrim runtime pairs ---------------------------------------------
//
// SKSE-plugin mods routinely offer their DLL per game runtime: "SSE v1.6.629+
// ('Anniversary Edition')" vs "SSE v1.5.97 ('Special Edition')". The manager
// knows which of the two the active profile is - it is the reason Skyrim AE
// and SE are separate games here at all - so a wizard leaving the FOMOD's own
// default ticked on the wrong runtime is the manager withholding an answer it
// has. The classifier reads one option name; the wizard recommends only when
// a group contains BOTH kinds, so a lone "AE" in some unrelated name never
// draws a tick.

enum class SkyrimRuntime { None, SE, AE };

// Which runtime an option name is built for. Version numbers are the strongest
// signal (1.6.x = AE, 1.5.x = SE), then the words ("anniversary" / the phrase
// "special edition"), then bare AE/SE word-tokens ("SSE" never matches SE).
// Conflicting signals return None rather than a guess.
SkyrimRuntime classifyRuntimeVariant(const QString &optionName);

// The runtime the active game profile runs: skyrimanniversaryedition is 1.6.x,
// skyrimspecialedition 1.5.97, and Enderal SE ships pinned to 1.5.97 too.
// None for everything else, which disables the pass entirely.
SkyrimRuntime runtimePreferenceForGame(const QString &gameId);

} // namespace fomod

#endif // FOMOD_HINT_H
