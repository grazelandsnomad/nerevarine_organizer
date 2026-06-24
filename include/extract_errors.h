#pragma once

// extract_errors - pick the translation key for an extraction failure.
//
// Split out of MainWindow::onExtractionFailed to make routing testable, and to
// fix a wrong message: downloads stage under a bare install-token UUID with no
// extension, so the old `ext == "7z"/"fomod"` check never matched and every
// nonzero-exit failure showed "Make sure 7z (p7zip-full) is installed" - which
// contradicts itself, since a nonzero exit means the extractor ran.
//
// Rules: a nonzero exit never blames a missing program; exit codes route
// regardless of extension, since everything but .zip (including an
// extensionless token download) goes through 7z.

#include <QString>

namespace extract_errors {

// programMissing - the extractor binary couldn't start.
// detail         - program name when programMissing, else the exit-code string.
// ext            - archive's lowercased extension ("" for a token download).
// Returns a translation key (english.ini "-- Extraction --").
QString failureKey(bool programMissing, const QString &detail, const QString &ext);

} // namespace extract_errors
