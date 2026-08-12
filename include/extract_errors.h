#pragma once

// extract_errors - pick the translation key for an extraction failure.
//
// Split out of MainWindow::onExtractionFailed to make routing testable, and to
// fix a wrong message: downloads stage under a bare install-token UUID with no
// extension, so the old `ext == "7z"/"fomod"` check never matched and every
// nonzero-exit failure showed "Make sure 7z (p7zip-full) is installed" - which
// contradicts itself, since a nonzero exit means the extractor ran.
//
// Rules: a nonzero exit never blames a missing program; routing keys on the
// sniffed container (NOT the extension, which is empty for a token download), so
// a failed RAR gets the unrar/p7zip message and everything else 7z's exit codes.

#include "archive_magic.h"

#include <QString>

namespace extract_errors {

// programMissing - the extractor binary couldn't start.
// detail         - program name when programMissing, else the exit-code string.
// fmt            - container sniffed from the archive's magic bytes.
// Returns a translation key (english.ini "-- Extraction --").
QString failureKey(bool programMissing, const QString &detail,
                   archive_magic::Format fmt);

} // namespace extract_errors
