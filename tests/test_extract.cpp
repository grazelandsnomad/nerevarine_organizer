#include "archive_magic.h"
#include "archive_policy.h"
#include "download_integrity.h"
#include "extract_errors.h"
#include "installcontroller.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>

#include <initializer_list>
#include <iostream>

using archive_magic::Format;

#include "test_harness.h"

// Build a header from raw bytes.
static QByteArray hdr(std::initializer_list<unsigned char> bytes)
{
    QByteArray b;
    for (unsigned char c : bytes) b.append(static_cast<char>(c));
    return b;
}

// === archive_magic::sniff + looksLikeArchive ===

static void testSniff()
{
    std::cout << "\n[sniff: recognizes each container by magic bytes]\n";
    check("7z",    archive_magic::sniff(hdr({0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C})) == Format::SevenZip);
    check("zip (local file)", archive_magic::sniff(hdr({0x50, 0x4B, 0x03, 0x04})) == Format::Zip);
    check("zip (empty)",      archive_magic::sniff(hdr({0x50, 0x4B, 0x05, 0x06})) == Format::Zip);
    check("zip (spanned)",    archive_magic::sniff(hdr({0x50, 0x4B, 0x07, 0x08})) == Format::Zip);
    check("rar (shared 4.x/5.x sig)", archive_magic::sniff(hdr({0x52, 0x61, 0x72, 0x21, 0x1A, 0x07})) == Format::Rar);
    check("gzip",  archive_magic::sniff(hdr({0x1F, 0x8B})) == Format::Gzip);
    check("xz",    archive_magic::sniff(hdr({0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00})) == Format::Xz);
    check("bzip2", archive_magic::sniff(hdr({0x42, 0x5A, 0x68})) == Format::Bzip2);
    check("zstd",  archive_magic::sniff(hdr({0x28, 0xB5, 0x2F, 0xFD})) == Format::Zstd);
    check("cab",   archive_magic::sniff(hdr({0x4D, 0x53, 0x43, 0x46})) == Format::Cab);

    std::cout << "\n[sniff: non-archives read as Unknown]\n";
    check("TES3 plugin",  archive_magic::sniff(QByteArray("TES3\0\0\0\0", 8)) == Format::Unknown);
    check("TES4 plugin",  archive_magic::sniff(QByteArray("TES4\0\0\0\0", 8)) == Format::Unknown);
    check("HTML error body", archive_magic::sniff(QByteArray("<!DOCTYPE html><html>")) == Format::Unknown);
    check("empty",        archive_magic::sniff(QByteArray()) == Format::Unknown);
    check("short (1 byte)", archive_magic::sniff(hdr({0x50})) == Format::Unknown);
    check("random bytes",  archive_magic::sniff(hdr({0x00, 0x01, 0x02, 0x03})) == Format::Unknown);
}

static void testLooksLikeArchiveConsistency()
{
    std::cout << "\n[looksLikeArchive == (sniff != Unknown)]\n";
    const QList<QByteArray> samples = {
        hdr({0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C}),   // 7z
        hdr({0x50, 0x4B, 0x03, 0x04}),               // zip
        hdr({0x52, 0x61, 0x72, 0x21, 0x1A, 0x07}),   // rar
        hdr({0x1F, 0x8B}),                           // gzip
        QByteArray("TES3\0\0\0\0", 8),               // plugin
        QByteArray("<!DOCTYPE html>"),               // html
        QByteArray(),                                // empty
    };
    bool allConsistent = true;
    for (const auto &h : samples)
        if (archive_magic::looksLikeArchive(h)
            != (archive_magic::sniff(h) != Format::Unknown))
            allConsistent = false;
    check("looksLikeArchive agrees with sniff for every sample", allConsistent);
}

// === Regression: extensionless RAR routes to the unrar chain, not 7z-only ===

static void testBareUuidRarRegression()
{
    std::cout << "\n[regression: a bare-UUID RAR sniffs as Rar (extractArchive -> unrar chain)]\n";
    // The download lands as "28a3b9d5-bb49-4030-b5e3-8e13655db7a8" (no ext), so
    // extension-based routing sent it to 7z-only and it died with exit 2. Routing
    // is now by sniff(), which sees the RAR magic regardless of the filename.
    const QByteArray rarBytes = hdr({0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x01, 0x00});
    check("RAR5 magic -> Format::Rar", archive_magic::sniff(rarBytes) == Format::Rar);
    check("RAR is recognized as an archive", archive_magic::looksLikeArchive(rarBytes));
}

// === extract_errors::failureKey ===

static void testFailureKey()
{
    std::cout << "\n[failureKey: a failed RAR always gets the unrar/p7zip message]\n";
    check("Rar + nonzero exit -> failed_rar",
          extract_errors::failureKey(false, "2", Format::Rar) == QLatin1String("extraction_error_failed_rar"));
    check("Rar + program missing -> failed_rar",
          extract_errors::failureKey(true, "unrar", Format::Rar) == QLatin1String("extraction_error_failed_rar"));

    std::cout << "\n[failureKey: 7z exit codes route by container]\n";
    check("7z + code 1 -> code1",
          extract_errors::failureKey(false, "1", Format::SevenZip) == QLatin1String("extraction_error_7z_code1"));
    check("7z + code 2 -> code2",
          extract_errors::failureKey(false, "2", Format::SevenZip) == QLatin1String("extraction_error_7z_code2"));
    check("7z + code 255 -> code255",
          extract_errors::failureKey(false, "255", Format::SevenZip) == QLatin1String("extraction_error_7z_code255"));
    check("7z + odd code -> generic failed",
          extract_errors::failureKey(false, "9", Format::SevenZip) == QLatin1String("extraction_error_failed"));
    check("Unknown container + code 2 -> 7z code2 (extensionless token goes through 7z)",
          extract_errors::failureKey(false, "2", Format::Unknown) == QLatin1String("extraction_error_7z_code2"));

    std::cout << "\n[failureKey: zip nonzero is generic (unzip codes != 7z codes)]\n";
    check("Zip + code 2 -> generic failed (not a 7z code)",
          extract_errors::failureKey(false, "2", Format::Zip) == QLatin1String("extraction_error_failed"));

    std::cout << "\n[failureKey: a genuinely missing non-rar tool -> no_program]\n";
    check("7z missing -> no_program",
          extract_errors::failureKey(true, "7z", Format::SevenZip) == QLatin1String("extraction_error_no_program"));
    check("unzip missing -> no_program",
          extract_errors::failureKey(true, "unzip", Format::Zip) == QLatin1String("extraction_error_no_program"));
}

// === archive_magic::extensionFor + archiveFileName ===

static void testExtensionFor()
{
    std::cout << "\n[extensionFor]\n";
    check("Rar -> .rar",      archive_magic::extensionFor(Format::Rar)      == QLatin1String(".rar"));
    check("Zip -> .zip",      archive_magic::extensionFor(Format::Zip)      == QLatin1String(".zip"));
    check("SevenZip -> .7z",  archive_magic::extensionFor(Format::SevenZip) == QLatin1String(".7z"));
    check("Gzip -> .gz",      archive_magic::extensionFor(Format::Gzip)     == QLatin1String(".gz"));
    check("Unknown -> empty", archive_magic::extensionFor(Format::Unknown).isEmpty());
}

static void testArchiveFileName()
{
    std::cout << "\n[archiveFileName: corrects a bare, extensionless download name]\n";
    check("bare id + no nexus name + Rar -> append .rar",
          archive_magic::archiveFileName("28a3b9d5-bb49-4030-b5e3-8e13655db7a8", "", Format::Rar)
              == QLatin1String("28a3b9d5-bb49-4030-b5e3-8e13655db7a8.rar"));
    check("bare id + no nexus name + SevenZip -> append .7z",
          archive_magic::archiveFileName("28a3b9d5-bb49", "", Format::SevenZip)
              == QLatin1String("28a3b9d5-bb49.7z"));

    std::cout << "\n[archiveFileName: prefers the authoritative Nexus files.json name]\n";
    check("bare id + nexus name -> use nexus name (even over the sniffed ext)",
          archive_magic::archiveFileName("28a3b9d5-bb49", "Cool Mod-12-1-0.7z", Format::Zip)
              == QLatin1String("Cool Mod-12-1-0.7z"));
    check("nexus name is sanitized for the filesystem",
          archive_magic::archiveFileName("id", "Bad/Name:x.zip", Format::Zip)
              == QLatin1String("Bad_Name_x.zip"));

    std::cout << "\n[archiveFileName: leaves already-usable / unknowable names alone]\n";
    check("currentName already has an archive ext -> unchanged (normal premium DL)",
          archive_magic::archiveFileName("Mod-12-1-0.7z", "ignored", Format::SevenZip)
              == QLatin1String("Mod-12-1-0.7z"));
    check("bare id + no nexus name + Unknown -> unchanged",
          archive_magic::archiveFileName("28a3b9d5-bb49", "", Format::Unknown)
              == QLatin1String("28a3b9d5-bb49"));
    check("nexus name without an archive ext is ignored, falls to sniff",
          archive_magic::archiveFileName("28a3b9d5-bb49", "readme.txt", Format::Rar)
              == QLatin1String("28a3b9d5-bb49.rar"));
}

// === InstallController::cancelInstall ===
//
// Hermetic slice of the cancel machinery: the paths that never spawn an
// extractor. The kill-a-live-process path shares the exact same
// flag-checked-first logic in every finished handler, so these pin the
// contract without depending on unzip/7z being installed.

static void writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(bytes);
}

// Collects one flag per controller outcome for an install attempt.
struct CancelProbe {
    int cancelled = 0, succeeded = 0, failed = 0, verified = 0, verifyFailed = 0;
    QString cancelledExtractDir;

    explicit CancelProbe(InstallController &ctl)
    {
        QObject::connect(&ctl, &InstallController::extractionCancelled,
            [this](const QString &, const QString &dir, const QUuid &) {
                ++cancelled; cancelledExtractDir = dir; });
        QObject::connect(&ctl, &InstallController::extractionSucceeded,
            [this](const QString &, const QString &, const QString &,
                   const QUuid &) { ++succeeded; });
        QObject::connect(&ctl, &InstallController::extractionFailed,
            [this](const QString &, const QString &, const QUuid &,
                   InstallController::ExtractFailKind, const QString &) {
                ++failed; });
        QObject::connect(&ctl, &InstallController::verified,
            [this](const QString &, const QUuid &) { ++verified; });
        QObject::connect(&ctl, &InstallController::verificationFailed,
            [this](const QString &, const QUuid &,
                   InstallController::VerifyFailKind, const QString &,
                   const QString &) { ++verifyFailed; });
    }

    // Spin the event loop until any outcome lands (or 10s passes - a hang
    // here means a signal was dropped, and the checks below then fail).
    void waitForAnyOutcome()
    {
        QElapsedTimer t; t.start();
        while (cancelled + succeeded + failed + verified + verifyFailed == 0
               && t.elapsed() < 10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
};

static void testCancelBeforeExtractShortCircuits()
{
    std::cout << "\n[cancelInstall before extractArchive: no FS work, one cancelled signal]\n";
    QTemporaryDir tmp;
    const QString archive = tmp.filePath("mod.zip");
    writeBytes(archive, QByteArrayLiteral("PK\x03\x04 not really"));

    InstallController ctl;
    CancelProbe probe(ctl);
    const QUuid token = QUuid::createUuid();

    ctl.cancelInstall(token);
    ctl.extractArchive(archive, tmp.path(), token);   // emits synchronously

    check("extractionCancelled emitted exactly once", probe.cancelled == 1);
    check("no success/failure alongside it", probe.succeeded == 0 && probe.failed == 0);
    check("extractDir empty (nothing was created to clean up)",
          probe.cancelledExtractDir.isEmpty(), probe.cancelledExtractDir);
    check("no extract dir appeared on disk",
          !QDir(tmp.filePath("mod")).exists());

    std::cout << "\n[the flag is consumed: a rerun of the same token extracts normally]\n";
    // Loose-file path (content is not a real archive header at offset checks
    // aside, "PK\x03\x04" IS zip magic - so use a fresh non-archive file).
    const QString loose = tmp.filePath("plugin.esp");
    writeBytes(loose, QByteArray("TES3\0\0\0\0", 8));
    CancelProbe probe2(ctl);
    ctl.extractArchive(loose, tmp.path(), token);     // same token, flag gone
    check("second run is not cancelled", probe2.cancelled == 0);
    check("second run succeeds (loose file placed)", probe2.succeeded == 1);
}

static void testCancelDuringVerifyBeatsTheHash()
{
    std::cout << "\n[cancelInstall during the MD5 verify: cancelled, not verified/mismatch]\n";
    QTemporaryDir tmp;
    const QString archive = tmp.filePath("big.7z");
    writeBytes(archive, QByteArray(1 << 20, 'x'));

    InstallController ctl;
    CancelProbe probe(ctl);
    const QUuid token = QUuid::createUuid();

    // Wrong MD5 on purpose: without the cancel this MUST end in
    // verificationFailed, so the cancelled outcome below can't be vacuous.
    ctl.verifyArchive(archive, token,
                      QStringLiteral("00000000000000000000000000000000"),
                      /*expectedSize=*/1 << 20);
    // The continuation is queued back to this thread, so setting the flag
    // before processing events deterministically wins the race.
    ctl.cancelInstall(token);
    probe.waitForAnyOutcome();

    check("extractionCancelled emitted", probe.cancelled == 1);
    check("no verified/verificationFailed alongside it",
          probe.verified == 0 && probe.verifyFailed == 0);

    std::cout << "\n[control: same verify without cancel reports the mismatch]\n";
    CancelProbe probe2(ctl);
    ctl.verifyArchive(archive, QUuid::createUuid(),
                      QStringLiteral("00000000000000000000000000000000"),
                      /*expectedSize=*/1 << 20);
    probe2.waitForAnyOutcome();
    check("verificationFailed without a cancel", probe2.verifyFailed == 1);
    check("not cancelled", probe2.cancelled == 0);
}

// === InstallController: a zip unzip can't read goes to 7z ===
//
// Info-ZIP UnZip 6.00 calls some zip64 archives over 4 GB corrupt (exit 3,
// "start of central directory not found"); Sim Settlements 2 Chapter 3 was
// thrown away twice that way. A fake unzip that fails like it stands in for
// the 5.6 GB archive: the zip must still come out, via 7z.

static void testZipFallsBackTo7z()
{
    std::cout << "\n[unzip fails on a zip: 7z extracts it instead]\n";
    if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty()) {
        std::cout << "  (7z not installed - skipped)\n";
        return;
    }
    QTemporaryDir tmp;
    QDir(tmp.path()).mkpath("src/Data");
    writeBytes(tmp.filePath("src/Data/Test.esp"), QByteArrayLiteral("TES4"));
    const QString archive = tmp.filePath("mod.zip");
    QProcess zip;
    zip.setWorkingDirectory(tmp.filePath("src"));
    zip.start(QStringLiteral("7z"), {"a", "-tzip", archive, "Data"});
    zip.waitForFinished(30000);
    check("test zip built", zip.exitCode() == 0 && QFile::exists(archive));

    // A bin dir whose unzip fails as UnZip does on the big archive.
    QDir(tmp.path()).mkpath("bin");
    const QString fake = tmp.filePath("bin/unzip");
    writeBytes(fake, QByteArrayLiteral(
        "#!/bin/sh\necho 'start of central directory not found;' >&2\nexit 3\n"));
    QFile::setPermissions(fake, QFile::permissions(fake) | QFile::ExeOwner);
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", tmp.filePath("bin").toLocal8Bit() + ':' + oldPath);

    const QString modsDir = tmp.filePath("mods");
    QDir().mkpath(modsDir);
    InstallController ctl;
    CancelProbe probe(ctl);
    ctl.extractArchive(archive, modsDir, QUuid::createUuid());
    // unzip fails, then 7z runs: wait past the first process too.
    QElapsedTimer t; t.start();
    while (probe.succeeded + probe.failed + probe.cancelled == 0
           && t.elapsed() < 20000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    qputenv("PATH", oldPath);

    check("extraction succeeded", probe.succeeded == 1);
    check("no failure reported", probe.failed == 0);
    check("the plugin came out",
          QFile::exists(QDir(modsDir).filePath("mod/Data/Test.esp")));
}

// === archive_policy ===

static void testArchivePolicy()
{
    std::cout << "\n[archive_policy: what an ended install does with its archive]\n";
    using archive_policy::Action;
    using archive_policy::Outcome;
    using archive_policy::onOutcome;
    check("installed: cleaned up", onOutcome(Outcome::Installed) == Action::Delete);
    check("cancelled: kept - no second download to reach the same wizard",
          onOutcome(Outcome::Cancelled) == Action::Keep);
    check("nothing picked: kept, as cancelled",
          onOutcome(Outcome::NothingPicked) == Action::Keep);
    check("verification failed: not the file - deleted",
          onOutcome(Outcome::VerifyFailed) == Action::Delete);
    check("the extractor failed on it: deleted",
          onOutcome(Outcome::ExtractFailed) == Action::Delete);
    // Deleted with the bad ones until now, and downloaded again after the user
    // installed 7z - though nothing was ever wrong with it.
    check("no extractor to run: kept, the archive is fine",
          onOutcome(Outcome::ExtractorMissing) == Action::Keep);
    check("its row removed: nothing will use it - deleted",
          onOutcome(Outcome::RowGone) == Action::Delete);
}

// === download_integrity ===

static void testQuickProblem()
{
    std::cout << "\n[quickProblem: an error page served as a 200, a body too small]\n";
    using download_integrity::quickProblem;
    const QByteArray sevenZ = hdr({0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C});
    check("an archive of a sane size passes",
          quickProblem("application/octet-stream", sevenZ, 4096).isEmpty());
    check("an HTML content type is an error page",
          quickProblem("text/html; charset=utf-8", sevenZ, 4096).startsWith("error-body"));
    check("so is a JSON one",
          quickProblem("application/json", sevenZ, 4096).startsWith("error-body"));
    check("and a body that starts like a page, whatever the header says",
          quickProblem("application/octet-stream", "<!DOCTYPE html>", 4096)
              .startsWith("error-body"));
    check("or like a JSON error",
          quickProblem("", "{\"error\":\"x\"}", 4096).startsWith("error-body"));
    check("63 bytes is too small to be a mod",
          quickProblem("application/octet-stream", sevenZ, 63) == "too-small bytes=63");
}

static void testNeedsStructuralTest()
{
    std::cout << "\n[needsStructuralTest: only an archive nothing else will verify]\n";
    using download_integrity::needsStructuralTest;
    const QByteArray sevenZ = hdr({0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C});
    check("an unverified archive is tested", needsStructuralTest("", 0, sevenZ));
    check("one with an md5 is left to InstallController",
          !needsStructuralTest("d41d8cd98f00b204e9800998ecf8427e", 0, sevenZ));
    check("one with a size likewise", !needsStructuralTest("", 2746921791LL, sevenZ));
    check("a blank md5 counts as none", needsStructuralTest("   ", 0, sevenZ));
    check("a loose plugin is not an archive to test",
          !needsStructuralTest("", 0, QByteArray("TES3\x00\x00\x00\x00", 8)));
}

// Faster Decompression (Fallout 4 mod 102435): a free account's update stashed
// the size of the file the app picked, the user fetched another file from the
// website, and the download was failed against the wrong size every time.
static void testExpectationsApply()
{
    std::cout << "\n[expectationsApply: only the file the expectations were read for]\n";
    using download_integrity::expectationsApply;
    check("the same file", expectationsApply(5281, 5281));
    check("another file from the page", !expectationsApply(5281, 5300));
    check("untagged expectations never apply", !expectationsApply(0, 5281));
    check("an install of an unknown file (a drop) is not judged by a tag",
          !expectationsApply(5281, 0));
    check("nothing known either way", !expectationsApply(0, 0));
}

// The real 7z, when the machine has one: the check moved to a worker, and the
// worker's answer is what decides retry-or-extract.
static void testStructuralProblem()
{
    std::cout << "\n[structuralProblem: 7z t on a good and a broken archive]\n";
    if (QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty()) {
        std::cout << "  (7z not installed - skipped)\n";
        return;
    }
    QTemporaryDir tmp;
    QFile src(tmp.filePath("Mod.esp"));
    if (src.open(QIODevice::WriteOnly)) src.write(QByteArray(200000, 'x'));
    src.close();
    const QString good = tmp.filePath("good.7z");
    QProcess::execute(QStringLiteral("7z"), {"a", "-bso0", "-bsp0", good, src.fileName()});
    check("a sound archive passes", download_integrity::structuralProblem(good).isEmpty(),
          download_integrity::structuralProblem(good));

    // Fair Care: the magic is intact, the payload is not.
    QFile f(good);
    QByteArray bytes;
    if (f.open(QIODevice::ReadOnly)) bytes = f.readAll();
    f.close();
    for (int i = 40; i < bytes.size() - 40; ++i) bytes[i] = char(bytes[i] ^ 0x5A);
    const QString broken = tmp.filePath("broken.7z");
    QFile b(broken);
    if (b.open(QIODevice::WriteOnly)) b.write(bytes);
    b.close();
    check("a corrupt one is condemned",
          download_integrity::structuralProblem(broken).startsWith("7z-test-failed"),
          download_integrity::structuralProblem(broken));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::cout << "=== archive_magic ===\n";
    testSniff();
    testLooksLikeArchiveConsistency();
    testBareUuidRarRegression();
    testExtensionFor();
    testArchiveFileName();

    std::cout << "\n=== archive_policy ===\n";
    testArchivePolicy();

    std::cout << "\n=== download_integrity ===\n";
    testQuickProblem();
    testNeedsStructuralTest();
    testExpectationsApply();
    testStructuralProblem();

    std::cout << "\n=== extract_errors ===\n";
    testFailureKey();

    std::cout << "\n=== InstallController cancel ===\n";
    testCancelBeforeExtractShortCircuits();
    testCancelDuringVerifyBeatsTheHash();
    testZipFallsBackTo7z();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
