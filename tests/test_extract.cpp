#include "archive_magic.h"
#include "extract_errors.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>

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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::cout << "=== archive_magic ===\n";
    testSniff();
    testLooksLikeArchiveConsistency();
    testBareUuidRarRegression();
    testExtensionFor();
    testArchiveFileName();

    std::cout << "\n=== extract_errors ===\n";
    testFailureKey();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
