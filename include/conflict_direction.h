#ifndef CONFLICT_DIRECTION_H
#define CONFLICT_DIRECTION_H

// Which of two mods sharing a file actually provides the copy that loads.
//
// The list is the load order and the LAST entry wins: OpenMW resolves its VFS
// that way, and the Bethesda deploy links files into Data/ in the same order
// with the last writer keeping the file. So an overlap is never symmetric - the
// mod further down shadows the one above it.
//
// Knowing two mods share a file says nothing about which copy is live; only the
// ordering does, and that is the part worth showing. A translation is the plain
// case: it ships the original's filename and takes effect only when it sits
// BELOW the mod it translates. Above it, it deploys and is immediately
// overwritten, with nothing anywhere to say so.
//
// Pure: the caller's worker thread walks the filesystem and hands over the
// finished owners map.

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace conflict_direction {

// A mod in load order. Later index = further down the list = wins.
struct Mod {
    QString path;   // absolute mod folder; the caller's identity for a row
    QString name;   // display name, shown in the tooltip
};

// The other mod in one directed conflict.
struct Counterpart {
    QString     path;
    QString     name;
    QStringList files;   // shared paths, capped, last entry may be "+N more"
};

// One mod's view of every conflict it takes part in. A mod can appear in both
// lists at once - shadowing the mod above it while being shadowed by the one
// below.
struct Directions {
    QList<Counterpart> overwrites;      // mods this one shadows (they sit above)
    QList<Counterpart> overwrittenBy;   // mods that shadow this one (below)
    bool isEmpty() const { return overwrites.isEmpty() && overwrittenBy.isEmpty(); }
};

// `owners` maps a lowercased mod-relative file path to the indices into `mods`
// that ship it, in any order; single-provider files are ignored. Counterparts
// come back sorted by display name, their files case-insensitively sorted and
// capped at `maxShownFiles` followed by a "+N more" marker. Keyed by Mod::path.
QHash<QString, Directions> resolve(const QList<Mod> &mods,
                                   const QHash<QString, QList<int>> &owners,
                                   int maxShownFiles = 8);

} // namespace conflict_direction

#endif // CONFLICT_DIRECTION_H
