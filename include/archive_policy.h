#pragma once

// archive_policy - what becomes of a downloaded archive when its install ends.
//
// The rule lived in ten call sites in mainwindow_install.cpp, a QFile::remove
// here and a PendingArchive there:
//
// - Cancelling keeps the archive. Throwing it away made a free account pull
//   the whole file down again just to reach the same wizard.
// - A failure deletes it, because the file is bad.
// - A success cleans it up.
//
// Written once, the rule also gets right a case the sites got wrong. When the
// extractor is not installed, that says nothing about the archive, so it is
// kept for the retry after installing 7z or unrar instead of being deleted
// with the "bad" ones.
//
// Qt Core only.

namespace archive_policy {

enum class Outcome {
    Installed,         // the mod is in place
    Cancelled,         // the user said "not now": wizard, picker, extraction
    NothingPicked,     // the picks held no files - as cancelled
    VerifyFailed,      // md5/size mismatch: not the file Nexus listed
    ExtractFailed,     // the extractor ran and failed: a bad or unsupported archive
    ExtractorMissing,  // no 7z/unzip/unrar to run: the archive is fine
    RowGone,           // the row was removed mid-install: nothing will use it
};

enum class Action {
    Keep,     // stays on disk; the row remembers it (ModRole::PendingArchive)
    Delete,
};

Action onOutcome(Outcome outcome);

} // namespace archive_policy
