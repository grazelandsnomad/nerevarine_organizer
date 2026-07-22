#ifndef SAFE_FS_H
#define SAFE_FS_H

// safe_fs - data-safety filesystem helpers split out of MainWindow.
//
//   snapshotBackup   - runs before every modlist_*.txt / openmw.cfg write.
//   copyTreeVerified - fallback for cross-filesystem "Move Mod Library"; a
//                      dropped byte here followed by deleting src loses the
//                      whole mod library, so it verifies sizes.
//
// Qt Core only (no Widgets) so both are unit-testable against a QTemporaryDir.

#include <QString>

#include <expected>
#include <functional>

namespace safefs {

// Copy `liveFile` to `<liveFile>.bak.YYYYMMDD-HHMMSS`, pruning to the newest
// `keep` snapshots in that directory.
//
// No-op if `liveFile` is missing (fresh machine) or QFile::copy fails (e.g.
// called twice in the same second - the existing snapshot for that timestamp
// survives). Returns the new snapshot path, or a short reason on failure.
// Callers mostly ignore it; tests assert on the rotation.
//
// keep defaults to 20: a burst of saves (a couple of sort/reorder passes plus
// the launcher/openmw.cfg sync each save) can otherwise rotate a still-wanted
// order out of reach within one session. 20 spans that comfortably.
std::expected<QString, QString>
snapshotBackup(const QString &liveFile, int keep = 20);

// Streaming per-file copy with size verification; fallback when QDir::rename
// fails (typically EXDEV on cross-filesystem moves).
//
//   - Copies every regular file under `src` into `dst`, building the tree.
//   - Verifies dest size == src size after each copy; any mismatch fails.
//   - Polls `isCancelled` between entries (empty function = never cancelled).
//   - On error/cancel, best-effort removes the dest tree so `src` stays the
//     only copy. Never touches `src`; the caller deletes it once we succeed.
//
// On failure the reason is machine-readable ("cancelled",
// "copy failed: rel/path", "size mismatch after copy: ...").
std::expected<void, QString>
copyTreeVerified(const QString &src, const QString &dst,
                 std::function<bool()> isCancelled = {});

// QDir::removeRecursively returns EACCES unlinking children of a dir with the
// owner-write bit clear. Zip-extracted mods carry such dirs (Windows ACLs land
// as POSIX `dr-xr-xr-x`), so a partial removeRecursively mid "Move Mod Library"
// strips the writable parts before failing on the read-only ones - data loss.
//
// So: walk the tree granting `u+w` on every entry first (cheap chmod), then
// removeRecursively. Two-phase on purpose - if any chmod fails we return false
// BEFORE touching the tree, so a caller holding a verified copy can keep src.
//
// Returns true iff `path` is gone when the call returns.
bool forceRemoveRecursively(const QString &path);

// Pick a path inside `dir` that a download can actually be opened for writing.
//
// Returns `dir/filename` when that path is free or already a regular file (an
// overwrite is the wanted behaviour for a re-download). When something else
// sits there - in practice a DIRECTORY - it suffixes "_2", "_3", ... before
// the extension until the name is free.
//
// This is not hypothetical tidiness. Nexus CDN links for the free-tier flow
// land under a bare, extensionless UUID, and that UUID is stable per file. The
// installer extracts such an archive into a directory of the same name inside
// the mods dir, so re-downloading an already-installed mod resolves to a save
// path that is now a directory: QFile::open(WriteOnly) fails, and freeing disk
// space does nothing because space was never the problem.
QString writableFilePath(const QString &dir, const QString &filename);

} // namespace safefs

#endif // SAFE_FS_H
