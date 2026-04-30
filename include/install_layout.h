#pragma once

#include <QString>
#include <QStringList>

// install_layout - pure helpers shared by InstallController and the unit
// tests for the post-extraction "where's the real mod root?" decision.
//
// Lives in its own translation unit (no Qt widgets, no QObject) so the dive
// heuristic can be exercised in isolation without spinning up the QProcess
// extractor pipeline.

namespace install_layout {

// Decide whether to dive into a single top-level subdirectory after
// extracting an archive.  Returns the subdirectory name to dive into, or
// an empty string when the caller should keep the extract directory as
// the mod root.
//
// We dive when ALL hold:
//   · exactly one subdirectory sits at the top level
//   · no top-level files sit alongside it - any top-level file is part
//     of the data root and would be silently orphaned by the dive
//     (the report this fixes: archives whose root holds <Folder>/ +
//     <Mod>.esp had the .esp left behind because the dive ignored it)
//   · the subdirectory's name isn't itself a recognised OpenMW data /
//     asset folder (textures, meshes, fomod, …).  Diving into one of
//     those would bury the data root one level deeper - OpenMW with
//     data="…/textures" would search for "textures/foo.dds" inside the
//     textures/ folder and never find foo.dds.
//
// Comparison is case-insensitive throughout.
QString diveTarget(const QStringList &topSubdirs,
                   const QStringList &topFiles);

} // namespace install_layout
