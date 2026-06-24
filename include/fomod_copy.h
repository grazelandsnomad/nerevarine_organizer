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

namespace fomod_copy {

// Copy every entry inside srcDir into dstDir (children, not the dir itself).
// No-op when srcDir does not exist or is empty.
void copyContents(const QString &srcDir, const QString &dstDir);

// Copy srcDir as a named subdirectory of dstDir's parent (i.e. dstDir IS the
// new directory's full path).  No-op when srcDir does not exist or is empty.
void copyDir(const QString &srcDir, const QString &dstDir);

} // namespace fomod_copy
