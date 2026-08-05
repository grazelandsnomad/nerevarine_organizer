#ifndef BACKUP_OPS_H
#define BACKUP_OPS_H

// backup_ops - the data-mutating filesystem half of BackupManager, split out so
// it's Widget-free and unit-testable (the modlist_io pattern, extended to the
// backup/restore path).
//
// BackupManager itself is a QDialog-driving QObject: it built QMessageBox /
// QFileDialog UI *and* did the destructive file work (overwrite the live
// modlist with a snapshot, drop a good-state copy, delete a checkpoint) inline,
// popping ui::critical from inside on failure. That made the failure paths -
// the ones that can lose a user's curated load order - impossible to exercise
// without spinning up Widgets. These helpers carry the file work and return a
// machine-readable reason on failure; the dialog code stays a thin presenter
// that maps a failure to the right translated message.
//
// Qt Core only (no Widgets), so tests drive them against a QTemporaryDir.

#include <QString>

#include <expected>

namespace backup_ops {

// Overwrite `livePath` with the bytes of `snapshotPath` (a .bak.* or .good.*
// file). Snapshots the current `livePath` FIRST via safefs::snapshotBackup so
// the restore is itself undoable, then replaces it. Returns {} on success or a
// short reason ("snapshot is gone", "could not remove ...", "could not write
// ..."). Shared by the rotating-backup restore and the good-state restore.
std::expected<void, QString>
restoreSnapshot(const QString &livePath, const QString &snapshotPath);

// Copy `livePath` to `<livePath>.good.<stamp>`, replacing any file already at
// that exact name. `stamp` is supplied by the caller (usually
// QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")) so the op is
// deterministic for tests. Returns the good-state path on success.
std::expected<QString, QString>
markGoodState(const QString &livePath, const QString &stamp);

// Delete a single snapshot file. Returns {} on success or a reason.
std::expected<void, QString>
deleteSnapshot(const QString &path);

} // namespace backup_ops

#endif // BACKUP_OPS_H
