#pragma once

// bain_hint - what a BAIN package name says the package is FOR, and whether
// the user has that mod.
//
// "01 Patch - Uncharted Artifacts" is a compatibility patch. Install it
// without Uncharted Artifacts and the files go in for a mod nothing will ever
// load them for; if the patch ships a plugin, OpenMW may refuse to start.
// Roughly a third of the numbered packages in a real mods folder are this
// shape - 35 of 101 distinct names, measured.
//
// The hard part is not finding the patches. It is NOT finding the things that
// are not patches. "03 Chuzei Fix" fixes a vanilla helmet. "05 - Patch for
// water removal" removes water. "01 Atlas Patch" is for Project Atlas, which
// IS installed - unticking that is worse than the problem being solved. So a
// package earns a verdict only when its name yields something that reads like
// a mod name, and everything else is left exactly as the author set it.
//
// Pure: no widgets, no filesystem. The one filesystem input (a package's
// foreign masters) is passed in, from bain::foreignMasters().

#include <QSet>
#include <QString>
#include <QStringList>

#include "bain.h"

namespace bain {

// What a package name names, if anything.
struct PackageTarget {
    QString     display;            // best name to show ("Glow in the Dahrk")
    QStringList candidates;         // every string worth matching, best first
    bool        confident = false;  // name-shaped enough to justify a MISS
};

// Read a package folder name. `ownModName` is the mod being installed, so a
// package naming its own mod is not read as naming another one.
PackageTarget targetOfPackageName(const QString &packageName,
                                  const QString &ownModName = {});

struct PackageVerdict {
    enum class State  { Unknown, Installed, Missing };
    // Where the answer came from. Name is an inference off a folder name;
    // Master is a file the engine will demand and not find.
    enum class Source { None, Name, Master };

    State   state  = State::Unknown;
    Source  source = Source::None;
    QString target;    // the mod this package is for, as it should be shown
    QString matched;   // the installed mod that answered for it (Installed)
    QString master;    // the unsatisfiable master file (Missing via Master)
};

// One package's verdict.
//
// `foreignMasters` comes from bain::foreignMasters(). `availablePluginsLower`
// is every plugin filename the modlist currently provides, lower-cased.
//
// An EMPTY availablePluginsLower turns the master pass off. A PARTIAL set is
// not acceptable and the caller must pass empty instead: a mod whose plugins
// were not enumerated would make a satisfied master look unsatisfiable, and
// that is the one direction that unticks something the user needs.
//
// The two signals are deliberately asymmetric:
//
//   an unsatisfiable foreign master -> Missing, and it outranks the name,
//       because the engine will refuse to load the plugin whatever the folder
//       is called;
//   masters that are all satisfied  -> NO EVIDENCE, never Installed, and it
//       never vetoes a name verdict. "Loads without erroring" is not "is
//       wanted": the Lush Synthesis patch in OAAB Shipwrecks declares only
//       base game, Tamriel_Data and OAAB_Data, all present, while the mod it
//       patches is absent. Treating satisfied masters as approval would keep
//       exactly the package that should go.
PackageVerdict judgeOne(const QString      &packageName,
                        const QStringList  &foreignMasters,
                        const QStringList  &installedModNames,
                        const QSet<QString> &availablePluginsLower,
                        const QString      &ownModName = {});

// Every package judged, index-parallel with `packages`. Does the filesystem
// pass (bain::foreignMasters) and then judgeOne.
//
// Applies the whole-archive guard: if the result would leave NOTHING ticked,
// every Missing is downgraded to Unknown and the pass says nothing at all.
// bain::stage() returns "" for an empty selection and the caller reads that as
// a cancel, so an over-eager pass would silently abort the install.
QList<PackageVerdict> judgePackages(const QList<Package> &packages,
                                    const QStringList    &installedModNames,
                                    const QSet<QString>  &availablePluginsLower,
                                    const QString        &ownModName = {});

} // namespace bain
