#pragma once

// fomod_scripts - rescue lua bodies for .omwscripts manifests whose FOMOD
// plugin only references the manifest, not the script files.
//
// Completionist Patch Hub (Nexus 58523) ships per-plugin folders
// ("00 AJ", "01 BFM", ...) each with a .omwscripts AND its
// scripts/<X>/quests_<X>.lua. The FOMOD only declares the .omwscripts as a
// <file>; the lua is never referenced, so even with every plugin ticked you
// get manifests at the mod root and an empty scripts/, and OpenMW can't load
// what the manifests declare.
//
// installDeclaredScripts reads the manifest (after the wizard copied it),
// parses each `CONTEXT: <path>` line, and pulls the real lua from wherever it
// lives in the archive (root, or the manifest's parent) into installDir.

#include <QString>

namespace fomod_scripts {

// Parse manifestSrc, find each declared script under archiveRoot (falling back
// to the manifest's parent dir), and copy it into installDir at its declared
// path. No-op if the manifest is unreadable or declares nothing. Existing
// destination files are left alone (a FOMOD folder= may have placed them).
void installDeclaredScripts(const QString &manifestSrc,
                            const QString &archiveRoot,
                            const QString &installDir);

} // namespace fomod_scripts
