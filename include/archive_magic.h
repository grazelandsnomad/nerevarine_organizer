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

// True if `header` (first bytes of a file) matches a known archive signature:
// 7z, zip, rar, gzip, xz, bzip2, zstd, cab. tar has no magic at offset 0 and is
// rare for mods, so it reads as non-archive and gets placed loose (harmless).
bool looksLikeArchive(const QByteArray &header);

// Name to give a loose (non-archive) download when placing it as a mod.
//   currentName - downloaded file's name (may be a bare id with no ext)
//   header      - first bytes; a TES3/TES4 plugin gets .esp when the name has
//                 no usable extension
//   nameHint    - human title (e.g. Nexus mod name) used as base when currentName
//                 has no usable extension; sanitized for the FS
// A name already ending in a known plugin/asset extension is kept as-is.
QString looseFileName(const QString &currentName, const QByteArray &header,
                      const QString &nameHint);

} // namespace archive_magic
