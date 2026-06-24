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

// Dependency-variant classification for Nexus download files.  Some Morrowind
// mods publish two MAIN files: an "OAAB" build (depends on the OAAB Data
// resource pack) and a "No OAAB" build that doesn't.  Classifying a file's
// display name lets the download picker recommend the build that matches the
// user's modlist (OAAB build iff OAAB Data is installed).
//
//   · NoOaab    - name says "No OAAB" / "Non-OAAB" / "without OAAB" (any of
//                 space, underscore or hyphen between the words, or none).
//   · NeedsOaab - name mentions OAAB as a whole word but is NOT a No-OAAB
//                 build (so "No OAAB" never misclassifies as NeedsOaab).
//   · None      - OAAB isn't mentioned; not part of a variant pair.
//
// Matching is case-insensitive and word-boundary anchored so unrelated
// substrings ("Oaaberration") don't trip it.
enum class OaabVariant { None, NeedsOaab, NoOaab };
OaabVariant classifyOaabVariant(const QString &fileName);

} // namespace install_layout
