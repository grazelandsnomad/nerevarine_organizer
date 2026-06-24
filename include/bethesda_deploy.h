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
    bool isEmpty() const { return files.isEmpty(); }
};

// A mod to deploy: `label` for the manifest/reporting, `dir` the absolute
// folder whose CONTENTS merge into Data/ (caller has already resolved the
// data root, e.g. via plugins::collectDataFolders).
struct DeploySource {
    QString label;
    QString dir;
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

// Deploy `sources` (load order; later overrides earlier) into `dataDir`.
// Displaced pre-existing files move under `backupDir` at the same rel path.
// `preferred` is the first link method to try; falls back Hardlink ->
// Symlink -> Copy on failure (e.g. a cross-filesystem hardlink). Intended
// use: undeploy() the previous manifest, then deploy().
DeployResult deploy(const QString &dataDir,
                    const QString &backupDir,
                    const QList<DeploySource> &sources,
                    LinkMethod preferred = LinkMethod::Hardlink);

// Reverse a manifest: remove every still-ours deployed file from `dataDir`
// and restore any backed-up vanilla file from `backupDir`. Only touches
// paths named in the manifest.
UndeployResult undeploy(const QString &dataDir,
                        const QString &backupDir,
                        const Manifest &manifest);

// JSON (de)serialization to persist the manifest between sessions.
// Schema-versioned, like the modlist serializer's forward-compat shape.
QString  manifestToJson(const Manifest &m);
Manifest manifestFromJson(const QString &json);

} // namespace bethesda_deploy
