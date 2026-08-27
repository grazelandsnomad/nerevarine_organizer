#ifndef MOD_ALIASES_H
#define MOD_ALIASES_H

// The handful of mods the modding scene refers to by acronym.
//
// A FOMOD that says "Required Static Mesh Improvement Mod - SMIM by Brumbek"
// has to be matched against a modlist where the same mod might be installed
// as "SMIM", "Static Mesh Improvement Mod SE", or "SMIM - Static Mesh
// Improvement Mod". Without a translation between the two forms the lookup
// fails and the manager reports a missing dependency the user actually has -
// which is worse than saying nothing, because it is confidently wrong.
//
// -- Kept deliberately small ------------------------------------------
//
// Only acronyms that are unambiguous in the scene and name ONE mod. The test
// for inclusion is whether a modder writing "SMIM" could mean anything else;
// if the answer is maybe, it does not go in.
//
// Game acronyms are excluded on purpose. "SSE" is Skyrim Special Edition, the
// GAME, and appears in half the mod names on that Nexus page - treating it as
// a mod name would match nearly everything. Same for LE, AE, FO4, NV.
//
// This is a matching aid, not a dependency database. It never asserts that a
// mod is required; it only widens the net when something else already has.

#include <QString>
#include <QStringList>

namespace mod_aliases {

// Other names the same mod goes by, NOT including `name` itself. Empty when
// the name is not one this table knows, which is the overwhelmingly common
// case. Case-insensitive.
QStringList aliasesFor(const QString &name);

// `names` plus every alias of every entry, deduplicated, original order first.
// The shape a caller matching against a modlist wants.
QStringList expand(const QStringList &names);

// Does the table hold this spelling at all?
//
// Not the same question as aliasesFor() coming back empty: that happens both
// for a name the table has never heard of and for a one-row entry that simply
// has no second spelling ("SkyUI", "Ashfall"). A caller deciding whether a
// bare word is confident enough to act on needs to tell those apart.
bool isKnownMod(const QString &name);

// Distribution frameworks a FOMOD may offer as alternatives for the same
// content, most preferred first.
//
// This is a TIEBREAK, used only when the user has more than one installed and
// something has to be picked. It is NOT a claim that one crashes less than
// another - no such evidence exists, and a manager inventing one would be
// worse than useless. The order is by how widely each is depended upon, on the
// reasoning that the framework hundreds of mods already load is the more
// field-tested one.
//
// When only one is installed, that fact decides and this order is never
// consulted.
QStringList frameworkPreference();

} // namespace mod_aliases

#endif // MOD_ALIASES_H
