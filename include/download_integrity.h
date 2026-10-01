#pragma once

// download_integrity - is a finished download the file it should be?
//
// A byte-complete download (no reply error, Content-Length satisfied) can
// still be junk: some CDNs return an HTML/JSON error page with a 200, and some
// files arrive with valid archive magic but corrupt contents (Fair Care).
// These answer that before the extractor fails with a confusing 7z "fatal".
//
// Split out of DownloadQueue so the decisions are testable, and so the one
// slow check - a structural `7z t`, which decompresses the whole archive - can
// run on a worker. It ran in the download's completion handler, on the UI
// thread, freezing the window for up to its 30 s timeout.
//
// Qt Core only.

#include <QByteArray>
#include <QString>

namespace download_integrity {

// What the server's word and the first bytes say, on the spot: an error page
// served as a 200 (an HTML/JSON/XML content type or body start - real
// downloads are octet-stream or an archive type, never a rendered page), or a
// body too small to be anything. A short reason, or "" when neither.
QString quickProblem(const QString &contentType, const QByteArray &header,
                     qint64 size);

// Whether to test the archive's structure. Only when nothing downstream will
// check it - Nexus gave neither an md5 nor a size, so InstallController's
// verification is skipped, the gap Fair Care's corruption slipped through -
// and only when the first bytes say archive: a loose plugin (.esp/.omwaddon,
// no magic) is a legitimate non-archive that `7z t` would condemn.
bool needsStructuralTest(const QString &expectedMd5, qint64 expectedSize,
                         const QByteArray &header);

// `7z t` on `path`. Blocks for as long as 7z takes (up to the 30 s timeout):
// run it on a worker. "7z-test-failed exit=N" when 7z opened the archive and
// found it broken; "" when it is fine - or when it could not be checked (7z
// missing, timed out), which is no reason to condemn a download.
QString structuralProblem(const QString &path);

} // namespace download_integrity
