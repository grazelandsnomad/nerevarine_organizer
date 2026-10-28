#pragma once

// bethesda_deploy - deploy a load-ordered set of mod folders into a Bethesda
// game's Data/ directory.
//
// Bethesda engines only load content physically in their single Data/ folder
// (unlike OpenMW's arbitrary data= paths), so managing one means putting the
// enabled mods' files there. We link (hardlink, falling back to symlink then
// copy) so it's cheap and the mod store stays the source of truth.
//
// Two hard requirements:
//   1. Last-writer-wins by load order - a later mod's file overrides an
//      earlier one, matching the conflict resolution shown in the list.
//   2. Full reversibility - Data/ is shared with the vanilla game (Steam can
//      re-verify it), so never destroy anything we didn't put there. Every
//      pre-existing file we displace is moved to a backup store first, and a
//      manifest records what we placed and what we displaced. undeploy()
//      reads it to remove only our files and restore the originals.
//
// FS-touching but path-explicit (no global state, $HOME, or QSettings), so
// deploy/undeploy is exercised with QTemporaryDir.

#include <QString>
#include <QStringList>
#include <QList>

#include <functional>

namespace bethesda_deploy {

enum class LinkMethod { Hardlink, Symlink, Copy };

// One file we placed into Data/. `rel` is relative to Data/; `sourceMod` is
// the label of the mod that won it (last writer); `displacedVanilla` is true
// if a pre-existing file was backed up before we wrote ours.
struct DeployedFile {
    QString    rel;
    QString    sourceMod;
    LinkMethod method = LinkMethod::Copy;
    bool       displacedVanilla = false;
};

struct Manifest {
    QList<DeployedFile> files;
    // Wine DLL override names this deployment added to the game's Proton
    // prefix (see dll_overrides). Recorded rather than recomputed so undeploy
    // takes back exactly what it put there and leaves a pre-existing override
    // of the user's own alone. Filled in by the caller after deploy(), which
    // knows nothing about prefixes.
    QStringList dllOverrides;
    bool isEmpty() const { return files.isEmpty(); }
};

// A mod to deploy: `label` for the manifest/reporting, `dir` the absolute
// folder whose CONTENTS merge into Data/ (caller has already resolved the
// data root, e.g. via plugins::collectDataFolders).
struct DeploySource {
    QString label;
    QString dir;
    // Empty = deploy everything under `dir`, recursively (the normal case).
    //
    // Non-empty = deploy only these names, taken relative to `dir` and not
    // recursed into. Script-extender payloads need it: SKSE ships
    // skse64_loader.exe and skse64_*.dll beside its Data/ folder, and those
    // two belong next to the game .exe while Data/ belongs in Data/. One mod,
    // two destinations, so the root files are named explicitly rather than
    // discovered by a walk that would also sweep up Data/ and src/.
    QStringList onlyFiles = {};
    // Folder under the deploy root these files land in, "" for the root itself.
    //
    // For the mods that ship a bare archive with no folder around it. A Gothic
    // mod is often a single Karibik.mod, and the engine only scans <root>/Data,
    // so deploying it where it sits would place a file nothing ever looks at.
    QString destSubdir = {};
};

struct DeployResult {
    Manifest    manifest;
    int         filesDeployed   = 0;   // distinct rels placed
    int         vanillaBackedUp = 0;   // pre-existing files moved to backup
    QStringList errors;                // non-fatal per-file failures
};

struct UndeployResult {
    int         removed  = 0;          // our files taken back out of Data/
    int         restored = 0;          // vanilla files put back
    QStringList errors;
};

// Called periodically while deploying or undeploying, with files handled so
// far out of the total. Fired every ~64 files rather than every file, since
// each call hops a value onto the UI thread.
//
// Passing one makes deploy() walk the sources once up front to count them, so
// the percentage is real; that walk is pure directory traversal and costs
// nothing against the link calls that follow. An empty callback skips the
// count entirely, which is what the tests and every non-UI caller use.
using ProgressFn = std::function<void(int done, int total)>;

// Deploy `sources` (load order; later overrides earlier) into `dataDir`.
// Displaced pre-existing files move under `backupDir` at the same rel path.
// `preferred` is the first link method to try; falls back Hardlink ->
// Symlink -> Copy on failure (e.g. a cross-filesystem hardlink). Intended
// use: undeploy() the previous manifest, then deploy().
//
// Runs off the UI thread in the app (see MainWindow::onDeployBethesda): a
// real list is thousands of hardlinks and doing that synchronously froze the
// window for the duration.
DeployResult deploy(const QString &dataDir,
                    const QString &backupDir,
                    const QList<DeploySource> &sources,
                    LinkMethod preferred = LinkMethod::Hardlink,
                    const ProgressFn &progress = {});

// Reverse a manifest: remove every still-ours deployed file from `dataDir`
// and restore any backed-up vanilla file from `backupDir`. Only touches
// paths named in the manifest.
UndeployResult undeploy(const QString &dataDir,
                        const QString &backupDir,
                        const Manifest &manifest,
                        const ProgressFn &progress = {});

// JSON (de)serialization to persist the manifest between sessions.
// Schema-versioned, like the modlist serializer's forward-compat shape.
QString  manifestToJson(const Manifest &m);
Manifest manifestFromJson(const QString &json);

// What a deployment was made FROM, as one short string: the enabled mods in
// load order (each as whatever identifies its files - the caller passes path
// and install date) and the plugin load order. Written beside the manifest at
// deploy time and recomputed from the list afterwards, so "has the list
// changed since the last deploy?" is answered by what the deploy used rather
// than by the modlist file's mtime, which every save bumps - including the
// ones that change nothing, like switching games or quitting.
QString sourceFingerprint(const QStringList &mods, const QStringList &loadOrder);

// Whether the game itself is in `gameRoot`: empty when one of `exeNames`
// (paths relative to it - the adapter's Steam and GOG executables) is there,
// else the first of them, the file that is missing.
//
// The deploy writes into the Data folder saved for the profile, and a Steam
// uninstall leaves that folder standing: Steam deletes only the files it
// installed, so the mods, the Creation Club downloads and the script extender
// stay, and the game and its own masters go. Every plugin then needs a file
// that is not there - which is true, and which buried the one fact that
// mattered under a line per plugin. The file name is matched in any case, as
// the game's Windows side would see it; an empty list cannot tell, and says
// nothing.
QString missingGameExe(const QString &gameRoot, const QStringList &exeNames);

// Where a game's Data is now. `savedData` is the folder remembered for it,
// `foundData` the one the stores (Steam, GOG) report now, `rootFromData` the
// adapter's way from Data/ to the game folder ("..").
//
// A game reinstalled on another drive leaves its old folder standing - the
// mods, the Creation Club downloads and the script extender are not Steam's
// to delete - and the saved path, still there, kept every deploy pointed at
// those leftovers. The saved folder wins while its game is in it; once it is
// not and the found one has it, the found one wins and `movedFrom` names the
// old one, for the next deploy to take the mods back out of. With neither,
// or no exe names to tell by, the saved folder stays.
struct GameLocation {
    QString dataDir;
    QString movedFrom;   // empty: no move
};
GameLocation followMovedGame(const QString &savedData, const QString &foundData,
                             const QStringList &exeNames, const QString &rootFromData);

} // namespace bethesda_deploy
