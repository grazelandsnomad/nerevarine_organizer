#pragma once

// asset_collisions - pure detector for files within the same mod data root
// whose relative paths differ only in case.  On a case-sensitive filesystem
// (Linux ext4/btrfs) "Player.lua" and "player.lua" are distinct files; OpenMW
// loads both and one silently shadows the other in the VFS.  The Inspector
// uses this to surface that shape without touching any files on disk.
//
// No Qt Widgets, no filesystem I/O, no MainWindow state - same pattern as
// plugin_collisions.h.  The caller walks each mod's data roots and packs the
// relative paths into AssetCaseInput; this module does the bucketing.

#include <QList>
#include <QString>
#include <QStringList>

namespace openmw {

struct AssetCaseInput {
    QString modLabel;     // display name shown in the Inspector
    QString dataRoot;     // absolute path of the data= folder
    QStringList relPaths; // relative paths as found on disk (actual casing)
};

struct AssetCaseHit {
    QString lowercasedRel; // e.g. "scripts/player.lua"
    QStringList spellings; // 2+ actual spellings that differ only by case
};

struct AssetCaseModReport {
    QString modLabel;
    QString dataRoot;
    QList<AssetCaseHit> hits; // non-empty by construction
};

struct AssetCollisionReport {
    QList<AssetCaseModReport> mods; // only entries with at least one hit
    int totalFilesChecked = 0;
};

// For each (modLabel, dataRoot, relPaths) triple, bucket relPaths by
// relPath.toLower().  Any bucket with 2+ distinct actual spellings is a
// case-variant collision.  Exact duplicate paths within the same input are
// de-duplicated and do not self-collide.
AssetCollisionReport findAssetCaseCollisions(const QList<AssetCaseInput> &inputs);

} // namespace openmw
