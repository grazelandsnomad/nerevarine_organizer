#pragma once

// fomod_copy - filesystem helpers used by the FOMOD installer to materialize
// the user's selections from an extracted archive into the staging dir.
// Pulled out of FomodWizard so they're reachable from unit tests without
// linking Qt Widgets.
//
// Both functions skip empty source directories instead of creating an empty
// destination.  An empty `scripts/` next to a fresh batch of .omwscripts
// declarations looks identical to "the script content failed to copy" - the
// real failure mode that prompted Completionist Patch Hub bug reports - so
// we treat empty-source as a no-op rather than reproducing the placeholder.

#include <QString>

#include "fomod_path.h"   // fomod::ResolvedPath (copyFile's gated destination)

namespace fomod_copy {

// Copy one file to a resolved destination: creates the parent, then last-writer-
// wins (removes any existing target first, since QFile::copy won't clobber), and
// warns + returns false on failure.  The destination is a fomod::ResolvedPath, so
// a raw unresolved path cannot reach a FOMOD copy - that is a compile error,
// which is how the case-variant / empty-install data-loss bug is designed out.
bool copyFile(const QString &src, const fomod::ResolvedPath &dst);

// Copy every entry inside srcDir into dstDir (children, not the dir itself).
// No-op when srcDir does not exist or is empty.
void copyContents(const QString &srcDir, const QString &dstDir);

// Copy srcDir as a named subdirectory of dstDir's parent (i.e. dstDir IS the
// new directory's full path).  No-op when srcDir does not exist or is empty.
void copyDir(const QString &srcDir, const QString &dstDir);

} // namespace fomod_copy
