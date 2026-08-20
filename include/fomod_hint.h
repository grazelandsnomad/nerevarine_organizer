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
#include <QStringList>
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

// The best name to call a cited mod, given the names of the options that
// cite it and the name of the group holding them. Used for both halves of
// the verdict, since a mod that IS installed still has to be named. Installers split the same
// mod across options in two shapes, and one rule covers both: the longest
// common prefix of the option names ("Ashfall" + "Ashfall (HD)" -> "Ashfall"),
// falling back to the group name when the options share nothing worth showing
// ("Core" + "HD" under a group called "Ashfall"). Empty when neither yields
// anything usable, and the caller should then word the warning without a name.
QString missingModLabel(const QStringList &optionNames, const QString &groupName);

// -- Options that require another mod ---------------------------------
//
// Grand Solitude offers an "SMIM Rotor" option whose description reads
// "SMIM Rotor for Solitude Windmill. Required Static Mesh Improvement Mod -
// SMIM by Brumbek." It ships ticked. With SMIM absent, that installs a mesh
// nothing will load correctly, and no URL is cited so citedMods cannot see it.
//
// The requirement is stated in prose, so the prose is read - but carefully.
// Every OTHER requirement sentence in the author's whole mod corpus is about
// the mod being installed, not another one:
//
//   "The core files for Voices of Vvardenfell. Required for the mod to
//    function."
//   "Core Files for AATL_Data. ... only required file in the installer."
//   "1K Normals ... (still requires Materials set for OpenMW)"
//
// A keyword rule fires on all three and extracts "for the mod to function",
// "file in the installer", "Materials set for OpenMW". So the keyword alone is
// not the evidence: what follows it has to LOOK like a mod name. Title-cased
// words run together, or an all-caps acronym - "Static Mesh Improvement Mod",
// "SMIM". The three sentences above continue in lower case ("for", "file") or
// with a single capitalised word, and yield nothing.
//
// Absence only becomes meaningful once a name-shaped candidate exists. That is
// the same rule asksAboutAnotherMod is built on, applied to a different kind
// of evidence: a failed lookup means nothing until you know you looked up a
// mod name.

// Mods an option's description says it requires, best candidate first (the
// full name, then any acronym). Empty when the description states no
// requirement, or states one that is not about another mod - which is the
// common case and the reason this is safe.
// `optionName` and `groupName` are what the option calls ITSELF. A FOMOD's
// own required entry reads "Required Main Files." under a group called
// "Required", and without them that parses as a missing mod named "Main
// Files" - a warning on the one option the user has no choice about. A
// requirement that names the option it is attached to is describing this
// mod's own files, not another mod.
QStringList requiredMods(const QString &description,
                         const QString &optionName = {},
                         const QString &groupName = {});

// -- Exclusive groups offering alternative frameworks -----------------
//
// Producers of Skyrim asks how to inject its orc-stronghold blacksmith goods:
//
//   Orc Stronghold Blacksmiths
//     ( ) Don't Install
//     (o) Container Distribution Framework      <- the FOMOD's default
//     ( ) SkyPatcher
//
// The options are not content, they are FRAMEWORKS the user must already have.
// With neither installed the pre-selected one installs config files that
// silently do nothing: no crash, no error, the blacksmiths just have no goods.
// Nothing in the manager said so, though it knew the modlist.
//
// Unlike the checkbox passes, an exclusive group always has something
// selected, so this must choose rather than merely warn.

struct FrameworkChoice {
    // Index to select, or -1 to leave the FOMOD's own default alone.
    int  index = -1;
    // Per-option verdicts, index-parallel with the names passed in. A name
    // this cannot identify as a mod gets Unknown and is never judged.
    enum class State { Unknown, Installed, Missing, OptOut };
    QList<State> states;
    // True when at least one named framework is installed, so the group can
    // actually do something.
    bool anyInstalled = false;
    // Set when more than one was installed and the preference order broke the
    // tie, so the UI can say that is what happened.
    bool brokeTie = false;
};

// Decide an exclusive group whose options name alternative frameworks.
//
// Only acts on options it can identify as mods (via mod_aliases), which is the
// positive evidence that makes a missing one meaningful - the same rule the
// requirement passes turn on. A group with no identifiable option is left
// entirely alone.
//
// Picks an installed framework when there is one, breaking a tie by
// mod_aliases::frameworkPreference(); otherwise the opt-out option
// ("Don't Install", "None") when the group offers one.
FrameworkChoice chooseFrameworkOption(const QStringList &optionNames,
                                      const QStringList &installedModNames);

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

// Also used by the DOWNLOAD path, not just the wizard: a mod page routinely
// carries one file per runtime, and the names say which is which -
//
//   Scrambled Bugs - Anniversary Edition (1.6.629.0 and later)
//   Scrambled Bugs - Anniversary Edition (1.6.318.0 to 1.6.353.0)
//   Scrambled Bugs - Special Edition (1.5.97.0 and earlier)
//
// Downloading the 1.5.97 build onto a 1.6.x game gives an SKSE plugin that
// simply refuses to load, with nothing in the manager saying why.
//
// Returns the candidate that suits `pref` when `chosen` does not, or an empty
// string when there is nothing to say: `chosen` already matches, either side
// is unclassifiable, or no candidate matches. Never guesses - a page whose
// files carry no version marking produces silence, exactly as the wizard pass
// does.
QString betterRuntimeFile(const QString &chosen, const QStringList &candidates,
                          SkyrimRuntime pref);

} // namespace fomod

#endif // FOMOD_HINT_H
