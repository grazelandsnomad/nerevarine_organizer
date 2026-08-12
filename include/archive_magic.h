#pragma once

// archive_magic - tell an archive from a loose file by content, and pick a
// sensible name for a loose download.
//
// Downloads land under whatever name the CDN URL gives (sometimes a bare id with
// no extension). A mod uploaded as a loose plugin (.omwaddon/.esp, common for
// small OpenMW mods) isn't an archive, so 7z/unzip "fatal"-errors on it. Peek
// the magic bytes: known archive -> extract; anything else -> place as loose.

#include <QByteArray>
#include <QString>

namespace archive_magic {

// Archive container recognized from the leading magic bytes. Drives extractor
// routing (a .rar mislabeled or extensionless still goes to unrar, not 7z-only)
// and the corrected on-disk name.
enum class Format {
    Unknown,   // not a recognized archive (place loose / leave name as-is)
    SevenZip,
    Zip,
    Rar,
    Gzip,
    Xz,
    Bzip2,
    Zstd,
    Cab,
};

// The archive container `header` (first bytes of a file) starts with, or
// Format::Unknown. Recognizes 7z, zip, rar, gzip, xz, bzip2, zstd, cab. tar has
// no magic at offset 0 and is rare for mods, so it reads as Unknown and gets
// placed loose (harmless).
Format sniff(const QByteArray &header);

// True if `header` matches any known archive signature. Kept as the single
// question most callers ask; defined as `sniff(header) != Format::Unknown` so
// the signature table lives in exactly one place.
bool looksLikeArchive(const QByteArray &header);

// File extension (with leading dot) for a sniffed format, e.g. Format::Rar ->
// ".rar". Format::Unknown -> "" (no extension to add).
QString extensionFor(Format fmt);

// Name to give a loose (non-archive) download when placing it as a mod.
//   currentName - downloaded file's name (may be a bare id with no ext)
//   header      - first bytes; a TES3/TES4 plugin gets .esp when the name has
//                 no usable extension
//   nameHint    - human title (e.g. Nexus mod name) used as base when currentName
//                 has no usable extension; sanitized for the FS
// A name already ending in a known plugin/asset extension is kept as-is.
QString looseFileName(const QString &currentName, const QByteArray &header,
                      const QString &nameHint);

// Corrected on-disk name for an ARCHIVE download whose CDN name may be a bare,
// extensionless id (the loose-file analogue is looseFileName above).
//   currentName - downloaded file's name (may be a bare id with no ext)
//   nexusName   - authoritative Nexus files.json "name" (may be empty)
//   fmt         - container sniffed from the magic bytes
// Priority: keep currentName when it already has a known archive extension (the
// normal premium-download case, untouched); else use nexusName when it carries
// one (sanitized for the FS); else append extensionFor(fmt) to currentName.
// Returns currentName unchanged when fmt is Unknown or no extension is needed.
QString archiveFileName(const QString &currentName, const QString &nexusName,
                        Format fmt);

} // namespace archive_magic
