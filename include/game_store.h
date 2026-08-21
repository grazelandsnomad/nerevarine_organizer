#ifndef GAME_STORE_H
#define GAME_STORE_H

// game_store - which shop a copy of a game came from, and which shop a file on
// a mod page was built for.
//
// The two stores ship different executables. Skyrim's GOG release is not the
// Steam release with the DRM taken off: it is a separate build, so anything
// that patches the exe (SKSE and every SKSE plugin, ENB, address libraries)
// has to be built once per store. Nexus pages carry both, named only by the
// store word in the filename:
//
//   Skyrim Script Extender (SKSE64) GOG    [v2.2.6]  MAIN  0.9 MB
//   Skyrim Script Extender (SKSE64) Steam  [v2.3.0]  MAIN  0.9 MB
//
// Picking the wrong one produces no error worth the name. The game starts and
// the mods that needed the extender are silently absent.
//
// The manager already knows the answer: the profile's own game path says
// where the copy came from. This turns both halves of that into data - what a
// path says, and what a file name says - so the picker can compare them.
//
// Pure: strings in, verdict out. The filesystem evidence (which install roots
// Heroic knows about) is passed in by the caller, so all of it is testable.

#include <QString>
#include <QStringList>

namespace game_store {

enum class Store {
    Unknown,   // no evidence either way, which is not the same as neither
    Steam,
    Gog,
};

// What a mod file's name says it was built for. A name has to say one store
// and only one: "GOG" and "Steam" both present, or neither, is a name that
// tells us nothing. Whole words only, so a mod about goggles is not a GOG
// build.
Store fromFileName(const QString &name);

// The same words taken out, for comparing two names with the store part
// removed: "Skyrim Script Extender (SKSE64) GOG" and "... Steam" are the same
// file under different builds, and that only shows once the store word is
// gone. Kept here so the word list has one home.
QString stripStoreWords(const QString &s);

// What an installed game's path says about where it came from.
//
// A "steamapps" path component is Steam's own layout and nothing else uses
// it. GOG has no such marker in the path, so the evidence is Heroic's list of
// install roots, which the caller passes in (store_scan::heroicInstalls()).
// Anything else is Unknown: a manually copied install is not a store.
Store fromInstallPath(const QString &path, const QStringList &gogRoots);

// The store's own name, for putting in a sentence. Not translated - "Steam"
// and "GOG" are what the shops call themselves in every language.
QString name(Store s);

} // namespace game_store

#endif // GAME_STORE_H
