#include "download_integrity.h"

#include "archive_magic.h"
#include "subprocess.h"

#include <QStringList>

namespace download_integrity {

QString quickProblem(const QString &contentType, const QByteArray &header,
                     qint64 size)
{
    const QString ct = contentType.toLower();
    const bool errorBody =
        ct.contains(QStringLiteral("text/html")) ||
        ct.contains(QStringLiteral("application/json")) ||
        ct.contains(QStringLiteral("application/xml")) ||
        ct.contains(QStringLiteral("text/xml")) ||
        header.startsWith("<!") || header.startsWith("<htm") ||
        header.startsWith("<HTM") || header.startsWith("<?xml") ||
        header.startsWith("{\"") || header.startsWith("[{");
    if (errorBody)
        return QStringLiteral("error-body type=%1").arg(contentType);
    if (size < 64)
        return QStringLiteral("too-small bytes=%1").arg(size);
    return {};
}

bool needsStructuralTest(const QString &expectedMd5, qint64 expectedSize,
                         const QByteArray &header)
{
    return expectedMd5.trimmed().isEmpty() && expectedSize <= 0
        && archive_magic::looksLikeArchive(header);
}

QString structuralProblem(const QString &path)
{
    const int code = subprocess::execute(QStringLiteral("7z"),
                                         {QStringLiteral("t"), path});
    // Only a positive exit means 7z opened it and it is broken. -1 is
    // "couldn't launch / timed out" (7z may not be installed; .zip uses
    // unzip). Don't condemn a download we just can't verify.
    if (code > 0)
        return QStringLiteral("7z-test-failed exit=%1").arg(code);
    return {};
}

} // namespace download_integrity
