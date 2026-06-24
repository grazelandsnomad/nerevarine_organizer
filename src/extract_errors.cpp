#include "extract_errors.h"

namespace extract_errors {

QString failureKey(bool programMissing, const QString &detail, const QString &ext)
{
    if (programMissing) {
        // The genuine "binary not found" case - the only place it's correct to
        // tell the user to install something.
        return detail == QLatin1String("unrar|7z")
                   ? QStringLiteral("extraction_error_failed_rar")
                   : QStringLiteral("extraction_error_no_program");
    }

    // Nonzero exit: a program RAN and failed - never imply it's missing.
    // .zip is handled by unzip; everything else (incl. extensionless token-named
    // downloads, which go through 7z) is extracted by 7z, so its exit codes
    // apply regardless of the file extension.
    const int code = detail.toInt();
    if (ext != QLatin1String("zip")) {
        if (code == 1)   return QStringLiteral("extraction_error_7z_code1");
        if (code == 2)   return QStringLiteral("extraction_error_7z_code2");
        if (code == 255) return QStringLiteral("extraction_error_7z_code255");
    }
    return QStringLiteral("extraction_error_failed");
}

} // namespace extract_errors
