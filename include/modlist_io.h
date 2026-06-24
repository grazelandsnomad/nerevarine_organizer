#pragma once

#include <QString>

#include <optional>

// modlist_io - the synchronous file-IO half of the async save chain
// in MainWindow.  Carved out in 0.4 so the
// "write modlist content + error reporting" path is exercisable
// without a real MainWindow.
//
// MainWindow::saveModList runs this on a QtConcurrent::run worker,
// then dispatches the optional error string back to the UI thread
// via QMetaObject::invokeMethod.  Tests can drive the helper
// directly with QTemporaryDir + a chmod 0500 dir for the failure
// path.

namespace modlist_io {

// Snapshot-backup `path` (best-effort - failure is non-fatal,
// matches safefs::snapshotBackup contract) and write `content` to it
// in UTF-8 text mode.  Returns nullopt on success, or the
// QFile::errorString() of the open() failure.  The backup is taken
// FIRST so a partial-write doesn't lose the previous good state.
//
// Pure synchronous I/O.  Caller picks the thread.
std::optional<QString> writeModlistFile(const QString &path,
                                          const QString &content);

} // namespace modlist_io
