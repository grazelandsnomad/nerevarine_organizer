#ifndef SAFE_FS_H
#define SAFE_FS_H

// safe_fs - data-safety filesystem helpers extracted from MainWindow.
//
// Functions here own two of the scariest code paths in the app:
//
//   · snapshotBackup   - runs before every write of modlist_*.txt and
//                         openmw.cfg.  If this silently does nothing, the
//                         README's "back up your files" disclaimer has no
//                         teeth.
//   · copyTreeVerified - runs on cross-filesystem "Move Mod Library"
//                         operations.  If this loses a byte between src and
//                         dst and then deletes src, the user's whole mod
//                         library is gone.
//
// Both live here instead of inside mainwindow.cpp so they can be unit-tested
// against a QTemporaryDir without a full MainWindow / QWidget stack.  No Qt
// Widgets dependency - only Qt Core.

#include <QString>

#include <expected>
#include <functional>

namespace safefs {

// Copy `liveFile` to `<liveFile>.bak.YYYYMMDD-HHMMSS` and prune matching
// snapshots in the same directory so only the newest `keep` remain.
//
// No-op if `liveFile` doesn't exist (first run of the app on a fresh
// machine) or if QFile::copy fails (the common "called twice in the same
// second" case - the prior snapshot for that timestamp stays intact).
//
// On success: the path of the snapshot that was created.
// On failure: a short reason ("no source file", "copy failed"). Callers
// generally ignore the return value; it's here so tests can assert on the
// rotation.
std::expected<QString, QString>
snapshotBackup(const QString &liveFile, int keep = 5);

// Streaming per-file copy with size verification, used as a fallback when
// QDir::rename fails (typically EXDEV on cross-filesystem moves).
//
// Contract:
//   · Copies every regular file under `src` into `dst`, creating the
//     destination tree as needed.
//   · After each copy, checks that the destination file size equals the
//     source size; any mismatch fails the whole operation.
//   · `isCancelled` is polled between entries.  If it returns true, the
//     copy stops and the partial destination is removed.
//   · On any error or cancel, the destination tree is best-effort removed
//     so `src` remains the sole extant copy of the data.  `src` is never
//     touched by this function; the caller is responsible for deleting it
//     once the copy succeeds.
//
// On failure the std::unexpected holds a short machine-readable reason
// ("cancelled", "copy failed: rel/path", "size mismatch after copy: ...").
//
// `isCancelled` may be an empty std::function - interpreted as "never
// cancelled".
std::expected<void, QString>
copyTreeVerified(const QString &src, const QString &dst,
                 std::function<bool()> isCancelled = {});

} // namespace safefs

#endif // SAFE_FS_H
