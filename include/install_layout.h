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

// True when `folderName` is itself a data root rather than a wrapper around
// one - the asset dirs OpenMW reads under data= (textures, meshes, …) and the
// folders Bethesda games read under Data/ (SKSE, Interface, Strings, …).
//
// Shared with the Bethesda deploy, which has to answer the same question from
// the other end: a mod whose stored root is one of these was installed a level
// too deep and must be deployed from its parent, or the folder name is lost.
// Case-insensitive.
bool isDataRootName(const QString &folderName);

// `<modsDir>/<name sanitised>`, with `_2`, `_3`... when that folder is taken.
//
// The one place a mod folder's name is decided from something human. Both
// installers that knew a mod's title already did this by hand and only the
// FOMOD/BAIN path ever ran - which is why a plain archive landed under its
// download's name, a bare CDN id in the worst case, and the same mod could
// be a readable folder or a UUID depending on whether it shipped an
// installer. `fallback` names the folder when `name` sanitises to nothing.
QString folderFor(const QString &modsDir, const QString &name, const QString &fallback);

// True when `folderName` is a conventional CONTAINER for a mod's payload -
// "Data" (Bethesda) or "Data Files" (Morrowind) - rather than a wrapper the
// packer happened to add.
//
// The difference matters only when lifting a single top-level folder out of
// the extract directory: a wrapper named after the mod is noise and comes
// out, but a container is where the deploy and the config writer expect the
// payload to sit, so it stays. Case-insensitive.
bool isDataContainerName(const QString &folderName);

// The folder a Bethesda deploy merges into Data/ for a mod stored at
// `modPath`: its parent when the stored root is itself one of the folders
// above, else `modPath`. Never `modsDir` itself - a top-level mod folder that
// merely happens to be called "meshes" would deploy the whole library.
// Deploy and the FOMOD wizard's file lookups both ask it, so they agree on
// where a mod's files land.
QString deployRoot(const QString &modPath, const QString &modsDir);

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
