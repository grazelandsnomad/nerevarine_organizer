#pragma once

// fomod_scripts - rescue lua content for .omwscripts manifests whose
// FOMOD plugin only references the manifest itself, not the script bodies.
//
// Completionist Patch Hub (Nexus mod 58523) ships per-plugin folders
// "00 AJ", "01 BFM", ... each containing both `Completionist - <X>.omwscripts`
// and the actual `scripts/Completionist - <X>/quests_<X>.lua`.  The FOMOD
// only declares the .omwscripts as a `<file>` entry; the lua body is never
// referenced, so even with every plugin ticked the user ends up with the
// manifests at the mod root and an empty / nonexistent `scripts/` next to
// them - and OpenMW fails to load the scripts the manifests declare.
//
// installDeclaredScripts reads the manifest after the wizard's main file
// loop has copied it, parses out each `CONTEXT: <path>` declaration, and
// pulls the real lua across from wherever it actually lives in the archive
// (archive root, or the manifest's own parent directory) into the install
// dir at the declared path.

#include <QString>

namespace fomod_scripts {

// Parse one .omwscripts manifest at manifestSrc, locate each declared
// script body relative to archiveRoot (with manifest's parent dir as a
// secondary search root), and copy it into installDir at the declared
// path.  No-op when the manifest is unreadable or declares nothing.
// Files already present at the destination are left alone (a folder= entry
// in the FOMOD may have legitimately put them there first).
void installDeclaredScripts(const QString &manifestSrc,
                            const QString &archiveRoot,
                            const QString &installDir);

} // namespace fomod_scripts
