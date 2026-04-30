#ifndef MODROLES_H
#define MODROLES_H

#include <Qt>

namespace ModRole {
    constexpr int ItemType      = Qt::UserRole + 1;  // "mod" or "separator"
    constexpr int BgColor       = Qt::UserRole + 2;
    constexpr int FgColor       = Qt::UserRole + 3;
    constexpr int NexusId       = Qt::UserRole + 4;
    constexpr int ModPath       = Qt::UserRole + 5;
    constexpr int CustomName    = Qt::UserRole + 6;  // user-defined display name
    constexpr int Annotation    = Qt::UserRole + 7;  // personal note
    constexpr int NexusUrl      = Qt::UserRole + 8;  // Nexus mod page URL
    constexpr int InstallStatus = Qt::UserRole + 9;  // 0=not installed, 1=installed, 2=installing
    constexpr int DateAdded        = Qt::UserRole + 10; // QDateTime when the mod was added
    constexpr int DownloadProgress  = Qt::UserRole + 11; // int 0-100 while downloading, -1 while extracting
    constexpr int UpdateAvailable   = Qt::UserRole + 12; // bool: true if a newer version exists on Nexus
    constexpr int Collapsed         = Qt::UserRole + 13; // bool: separator section is collapsed
    constexpr int HasConflict       = Qt::UserRole + 14; // bool: conflicts with ≥1 other enabled mod
    constexpr int ConflictsWith     = Qt::UserRole + 15; // QStringList: display names of conflicting mods
    constexpr int HasMissingMaster  = Qt::UserRole + 16; // bool: ≥1 plugin in this mod has a missing master
    constexpr int MissingMasters    = Qt::UserRole + 17; // QStringList: "plugin.esp\tmaster1.esm\tmaster2.esm" per plugin
    // UserRole + 18 was Endorsement (endorse/abstain) - feature removed.
    constexpr int ActiveCount       = Qt::UserRole + 19; // separator only: active mods in this section
    constexpr int TotalCount        = Qt::UserRole + 20; // separator only: total mods in this section
    constexpr int ModSize           = Qt::UserRole + 21; // qint64: recursive size of the mod folder in bytes
    constexpr int NexusTitle        = Qt::UserRole + 22; // QString: human title from the Nexus mod page (name field)
    // Expected checksum / size of the archive currently being downloaded.
    // Set when the file list is resolved, consumed by download-verification
    // before the archive is handed to extractAndAdd.  Cleared after success.
    constexpr int ExpectedMd5       = Qt::UserRole + 23; // QString lower-case hex
    constexpr int ExpectedSize      = Qt::UserRole + 24; // qint64 bytes (size_in_bytes from Nexus)
    constexpr int DependsOn         = Qt::UserRole + 25; // QStringList of Nexus URLs this mod depends on
    constexpr int HighlightRole     = Qt::UserRole + 26; // int: 0=none, 1=dependency of selected, 2=uses selected
    constexpr int HasMissingDependency  = Qt::UserRole + 27; // bool: ≥1 DependsOn URL is missing/disabled/uninstalled
    constexpr int MissingDependencies   = Qt::UserRole + 28; // QStringList of human-readable labels for the tooltip
    constexpr int HasInListDependency   = Qt::UserRole + 29; // bool: DependsOn resolves to ≥1 other mod present in the list (drives the ↳ indent)
    constexpr int IsUtility             = Qt::UserRole + 30; // bool: user flagged this as a framework / library consumed by other mods (grey bg + gear glyph)
    constexpr int IsFavorite            = Qt::UserRole + 31; // bool: user marked this mod as a personal favourite (gold star)
    constexpr int FomodChoices          = Qt::UserRole + 32; // QString: serialized FOMOD install choices ("si:gi:pi;..."), empty for non-FOMOD mods
    // Set only when repairEmptyModPaths() silently rebinds a mod whose
    // canonical ModPath (as written in modlist.txt) doesn't exist on this
    // machine - e.g. cross-machine modlist sync where each machine has a
    // different version folder for the same Nexus modId.  Holds the
    // ORIGINAL missing path so saveModList() can keep writing that path
    // instead of the in-memory rebound sibling, preventing a ping-pong
    // rewrite cycle between machines that share the modlist via git/sync.
    // Cleared on any real (re)install via addModFromPath.
    constexpr int IntendedModPath       = Qt::UserRole + 33; // QString
    // Derived separator-only flag: true iff at least one mod under this
    // separator (up to the next separator, or list end) has UpdateAvailable=true.
    // Recomputed by updateSectionCounts(); drives a temporary light-grey paint
    // override in ModListDelegate to nudge the user to update or remove the
    // offending mods.  Cleared automatically once every pending update in the
    // section is either installed or the mod is deleted.
    constexpr int SepHasUpdate          = Qt::UserRole + 34; // bool
    constexpr int VideoUrl              = Qt::UserRole + 35; // QString: YouTube video URL
    constexpr int SourceUrl             = Qt::UserRole + 36; // QString: non-Nexus download page (GitHub, etc.)
    // Set by handleNxmUrl when an existing INSTALLED row is reused as the
    // placeholder for a re-install / update. Holds the previous ModPath so
    // addModFromPath can remove the stale folder after the new install
    // lands (sibling-prefix dedup misses across version archives whose
    // folder names diverge). Cleared once consumed.
    constexpr int PrevModPath           = Qt::UserRole + 37; // QString
}

namespace ItemType {
    constexpr auto Mod       = "mod";
    constexpr auto Separator = "separator";
}

// Default column widths (used as initial values and minimums)
namespace ColWidth {
    constexpr int Status       = 110;
    constexpr int DateAdded    = 120;
    constexpr int RelativeTime = 115;
    constexpr int Annotation   = 250;
    constexpr int Size         = 90;
    constexpr int VideoReview  = 90;   // icon-only cells, but header reads "Video review"
    constexpr int Min          = 40;   // minimum allowed width when resizing
    constexpr int NameMin      = 120;  // minimum width reserved for the mod-name column
}

// Which right-side columns are currently visible + their runtime pixel widths
struct ColVisibility {
    bool status      = true;
    bool date        = true;
    bool relTime     = true;
    bool annot       = true;
    bool size        = true;
    bool videoReview = true;
    // Runtime widths - persisted in QSettings, resizable by the user
    int wStatus      = ColWidth::Status;
    int wDate        = ColWidth::DateAdded;
    int wRelTime     = ColWidth::RelativeTime;
    int wAnnot       = ColWidth::Annotation;
    int wSize        = ColWidth::Size;
    int wVideoReview = ColWidth::VideoReview;
};

#endif // MODROLES_H
