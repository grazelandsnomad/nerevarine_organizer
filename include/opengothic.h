#ifndef OPENGOTHIC_H
#define OPENGOTHIC_H

// opengothic - the engine facts for managing Gothic II mods under OpenGothic.
//
// OpenGothic (github.com/Try/OpenGothic) is an open re-implementation of the
// Gothic II: Night of the Raven engine. It ships no assets: it is pointed at an
// existing Gothic II install with `-g <path>` and reads everything from there.
// That makes it the Gothic equivalent of OpenMW, and it is the only way to run
// these mods natively on Linux.
//
// Three engine rules decide everything this module does. All three were read
// out of the engine source (game/commandline.cpp, game/gothic.cpp,
// game/resources.cpp and lib/ZenKit/src/Vfs.cc), not guessed, because each one
// fails silently when it is wrong:
//
//   1. A .mod archive does NOT load unless it is named in the [FILES] VDF list
//      of the ini passed as `-game:<name.ini>`. With no -game: ini at all the
//      filter is still ON with an empty list, so EVERY .mod in Data/ is thrown
//      away and the user sees vanilla with no error. This is why the deploy
//      writes an ini of its own: it is what makes the mods exist.
//      (gothic.cpp: modFilter starts true; resources.cpp: loadVdfs erases every
//      isMod archive that no listed name matches.)
//
//   2. .vdf archives are never filtered, so they load whether listed or not.
//
//   3. Conflicts between archives are decided by the archive's own header
//      timestamp: newest wins, whatever the mount order. (Vfs.cc mounts with
//      VfsOverwriteBehavior::OLDER, and every entry of an archive carries the
//      timestamp read from that archive's header.) So load order here is
//      written by stamping headers, not by ordering a list in a file.
//
// The header layout is fixed: 256 bytes of comment, a 16-byte signature
// ("PSVDSC_V2.00\n\r\n\r"), entry count, file count, then the timestamp as a
// little-endian MS-DOS packed date at byte offset 280. Verified against the
// author's Gothic II Gold install: base archives read 2002-11, the Night of the
// Raven addon 2003-07, which is exactly the override the game needs.
//
// Pure: paths in, values out, QtCore only, so all of it is unit-tested.

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace opengothic {

// -- the Gothic II install ---------------------------------------------

// Does `dir` look like a Gothic II root? Mirrors the engine's own check
// (CommandLine::validateGothicPath): Data/, _work/Data/ and the compiled
// scripts must all be there. Case-insensitive per segment, because the engine
// resolves paths that way too and a GOG install unpacked on Linux can spell any
// of them differently.
bool isGameRoot(const QString &dir);

// The Gothic II root that owns `path`, which may be the root itself or
// anything inside it (system/Gothic2.exe is what the storefront locators
// find). Empty when no ancestor validates.
QString gameRootFor(const QString &path);

// Gothic II installs at or under `startDir`, no deeper than `maxDepth` levels
// below it, at most `maxDirs` directories examined.
//
// For turning a wrong answer into a right one: people point the folder picker
// at the ENGINE, which is the thing they just downloaded and the thing the
// profile is named after, and the game is usually a couple of folders away
// from wherever they were looking. Breadth-first and capped, so a pick
// somewhere near the root of a large disk cannot turn into a full-disk walk
// (measured on the author's 4TB games drive: 388 directories, 0.07s).
QStringList findGameRoots(const QString &startDir, int maxDepth = 2, int maxDirs = 2000);

// -- the engine binary -------------------------------------------------

// What the OpenGothic executable is called. The project builds one binary per
// game edition and Gothic II: NotR is the supported one.
QStringList engineNames();

// Look for the engine on PATH and in the usual install locations, plus any
// `extraDirs` the caller wants tried first. Each directory is also tried with
// the layouts a downloaded or self-built copy has (an OpenGothic/ folder, a
// build tree inside it), because there is no install step: people keep the
// binary wherever they unpacked or compiled it.
// Empty when nothing is found; the caller should then ask.
QString findEngine(const QStringList &extraDirs = {});

// Directories worth searching for the engine given where the game is. People
// keep the two side by side far more often than not, and the alternative is a
// file dialog for a binary whose name they may not know. Cheap: a handful of
// exact paths, no directory walk.
QStringList engineSearchHints(const QString &gameRoot);

// The command line for a launch: `-g <root>`, plus `-game:<ini>` when a mod
// ini has been generated. `modIni` is a bare file name that must live in
// <root>/system, which is where the engine looks for it.
QStringList launchArgs(const QString &gameRoot, const QString &modIni = {});

// -- VDFS archives -----------------------------------------------------

// Is this a VDFS archive (.vdf or .mod)? Checked by signature, not extension:
// an archive is the only thing whose header may be rewritten.
bool isArchive(const QString &path);

// The archive's header timestamp as a raw MS-DOS packed value, or -1 if the
// file cannot be read or is not an archive.
qint64 readStamp(const QString &path);

// Rewrite that timestamp in place. Only the four bytes at offset 280 are
// touched. False if the file is not a readable, writable archive.
bool writeStamp(const QString &path, quint32 stamp);

quint32   toDosStamp(const QDateTime &t);
QDateTime fromDosStamp(quint32 stamp);

// The stamp for position `index` in the load order.
//
// Deliberately NOT derived from the current time: a stamp that moves on every
// deploy would rewrite every archive every time, and each rewrite has to break
// the hardlink back to the mod store first. Derived from the position instead,
// so re-deploying an unchanged list writes nothing at all.
//
// The base is far past any archive a human authored (the vanilla ones are
// 2002-2003), so a deployed mod always beats the base game, and later in the
// list beats earlier - the same "lower in the list wins" the conflict view
// shows.
quint32 stampForIndex(int index);

struct StampResult {
    int         stamped = 0;      // archives whose header was rewritten
    int         alreadyRight = 0; // archives already carrying the right stamp
    QStringList errors;
};

// Stamp `archives` (absolute paths, in load order: later wins) so the engine
// resolves their conflicts that way.
//
// Breaks a hardlink before writing: deploy links the game's copy to the one in
// the mod store, and rewriting the header through that link would edit the
// user's stored archive. The copy is made only for archives that actually need
// a new stamp.
StampResult applyOrder(const QStringList &archives);

// -- mod inis ----------------------------------------------------------

// The parts of a GothicStarter mod ini the engine reads. Anything else in the
// file (INFO block, descriptions) is presentation for the original launcher.
struct ModIni {
    QStringList vdf;          // [FILES] VDF, space separated in the file
    QString     gameDat;      // [FILES] GAME, a .DAT base name without extension
    QString     outputUnits;  // [FILES] OUTPUTUNITS
    QString     world;        // [SETTINGS] WORLD
    QString     player;       // [SETTINGS] PLAYER
    QString     title;        // [INFO] Title, for reporting only
    bool isEmpty() const {
        return vdf.isEmpty() && gameDat.isEmpty() && outputUnits.isEmpty()
            && world.isEmpty() && player.isEmpty();
    }
};

ModIni  parseModIni(const QString &text);
QString buildModIni(const ModIni &ini);

// Combine the mod inis of everything enabled, in load order, with the archives
// actually deployed.
//
// The VDF list is a union, because several mods can be on at once and each one
// only knows its own archives. The single-valued keys are a total conversion's
// (its own GOTHIC.DAT, its own starting world), so the LAST mod that declares
// one wins, matching the list's own last-writer-wins rule.
ModIni mergeModInis(const QList<ModIni> &inis, const QStringList &archiveNames);

// -- how a mod folder maps into the game -------------------------------

// Gothic mods come in two shapes and only one of them can be copied as it is.
//
//   * The packaged shape: Data/ (its archives) and often system/ (its ini),
//     which is the game folder's own layout, so it overlays as-is.
//   * A bare archive: one Karibik.mod, sometimes with a Karibik.ini beside it,
//     and nothing around them. The engine only scans <root>/Data and only
//     reads inis from <root>/system, so copying those where they sit puts
//     them where nothing ever looks.
//
// Both are normal on Nexus and World of Gothic, so the shape is worked out
// per mod rather than assumed.
struct FolderMapping {
    // Copy the whole folder into the game root unchanged.
    bool        overlay = false;
    // Otherwise: these top-level file names go to Data/ and system/.
    QStringList archives;
    QStringList inis;
    bool isEmpty() const { return !overlay && archives.isEmpty() && inis.isEmpty(); }
};

FolderMapping mapModFolder(const QString &modDir);

// What a deployment turns into, worked out from the files it placed.
//
// Lives here rather than in the deploy caller because getting it wrong is
// invisible in the game and the shapes are fiddly: the archives are found by
// signature (a readme named .vdf is not one), the mod inis are the ones under
// system/ that are not ours, and both have to stay in the order the deploy
// placed them, which is the load order.
struct ActivationPlan {
    QStringList   archivePaths;   // absolute, in load order (later wins)
    QStringList   archiveNames;   // their bare file names, for the VDF list
    QList<ModIni> modInis;        // the mods' own inis, in the same order
    // Archives whose name has a space in it. The engine splits the VDF list on
    // spaces, so these cannot be expressed in it and will not load; the caller
    // is expected to say so rather than write a list that means something else.
    QStringList   unusable;
    bool isEmpty() const { return archivePaths.isEmpty() && modInis.isEmpty(); }
};

// `deployedRels` are the paths a deploy placed, relative to `gameRoot`, in the
// order it placed them.
ActivationPlan planActivation(const QString &gameRoot, const QStringList &deployedRels);

// The ini this app generates, in <root>/system. Named after the app so it can
// never collide with a mod's own ini, and so it is obvious who wrote it.
QString generatedIniName();

} // namespace opengothic

#endif // OPENGOTHIC_H
