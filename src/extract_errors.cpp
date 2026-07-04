#include "extract_errors.h"

namespace extract_errors {

QString failureKey(bool programMissing, const QString &detail,
                   archive_magic::Format fmt)
{
    // A failed RAR always means the same thing: unrar (or a p7zip with a RAR
    // codec) is needed. This is the one message that helps, and it carries the
    // pacman/apt hint - so it applies whether unrar+7z were missing or both ran
    // and errored, and whether or not the download had a .rar extension.
    if (fmt == archive_magic::Format::Rar)
        return QStringLiteral("extraction_error_failed_rar");

    if (programMissing) {
        // The genuine "binary not found" case - the only place it's correct to
        // tell the user to install something.
        return QStringLiteral("extraction_error_no_program");
    }

    // Nonzero exit: a program RAN and failed - never imply it's missing.
    // .zip is handled by unzip; everything else (incl. extensionless token-named
    // downloads, which go through 7z) is extracted by 7z, so its exit codes
    // apply.
    const int code = detail.toInt();
    if (fmt != archive_magic::Format::Zip) {
        if (code == 1)   return QStringLiteral("extraction_error_7z_code1");
        if (code == 2)   return QStringLiteral("extraction_error_7z_code2");
        if (code == 255) return QStringLiteral("extraction_error_7z_code255");
    }
    return QStringLiteral("extraction_error_failed");
}

} // namespace extract_errors
