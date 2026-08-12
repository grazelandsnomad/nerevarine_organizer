#include "archive_magic.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

#include <initializer_list>

namespace archive_magic {

Format sniff(const QByteArray &header)
{
    auto match = [&](std::initializer_list<unsigned char> sig) {
        if (header.size() < static_cast<int>(sig.size())) return false;
        int i = 0;
        for (unsigned char b : sig)
            if (static_cast<unsigned char>(header[i++]) != b) return false;
        return true;
    };

    if (match({0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C})) return Format::SevenZip;
    if (match({0x50, 0x4B, 0x03, 0x04})              // zip (local file)
        || match({0x50, 0x4B, 0x05, 0x06})           // zip (empty)
        || match({0x50, 0x4B, 0x07, 0x08}))          // zip (spanned)
        return Format::Zip;
    if (match({0x52, 0x61, 0x72, 0x21, 0x1A, 0x07})) return Format::Rar;   // 4.x/5.x
    if (match({0x1F, 0x8B}))                          return Format::Gzip;  // .gz/.tgz
    if (match({0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00})) return Format::Xz;
    if (match({0x42, 0x5A, 0x68}))                    return Format::Bzip2;
    if (match({0x28, 0xB5, 0x2F, 0xFD}))              return Format::Zstd;
    if (match({0x4D, 0x53, 0x43, 0x46}))              return Format::Cab;
    return Format::Unknown;
}

bool looksLikeArchive(const QByteArray &header)
{
    return sniff(header) != Format::Unknown;
}

QString extensionFor(Format fmt)
{
    switch (fmt) {
        case Format::SevenZip: return QStringLiteral(".7z");
        case Format::Zip:      return QStringLiteral(".zip");
        case Format::Rar:      return QStringLiteral(".rar");
        case Format::Gzip:     return QStringLiteral(".gz");
        case Format::Xz:       return QStringLiteral(".xz");
        case Format::Bzip2:    return QStringLiteral(".bz2");
        case Format::Zstd:     return QStringLiteral(".zst");
        case Format::Cab:      return QStringLiteral(".cab");
        case Format::Unknown:  return QString();
    }
    return QString();
}

QString looseFileName(const QString &currentName, const QByteArray &header,
                      const QString &nameHint)
{
    const QFileInfo fi(currentName);
    const QString ext = fi.suffix().toLower();

    // Already a plugin/asset name (CDN gave the real filename) - keep it.
    static const QStringList known = {
        QStringLiteral("esp"), QStringLiteral("esm"), QStringLiteral("esl"),
        QStringLiteral("omwaddon"), QStringLiteral("omwscripts"),
        QStringLiteral("bsa"), QStringLiteral("ba2"),
        QStringLiteral("dds"), QStringLiteral("nif"), QStringLiteral("kf"),
        QStringLiteral("wav"), QStringLiteral("mp3"),
        QStringLiteral("txt"), QStringLiteral("ini"), QStringLiteral("json"),
    };
    if (known.contains(ext)) return fi.fileName();

    // No usable extension: use the human hint (else the bare name), sanitized,
    // and tack on .esp when the bytes say it's a TES plugin.
    QString base = nameHint.trimmed();
    if (base.isEmpty()) base = fi.completeBaseName();
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|]"));
    base.replace(bad, QStringLiteral("_"));
    base = base.trimmed();
    if (base.isEmpty()) base = QStringLiteral("mod");

    QString outExt;
    if (header.startsWith("TES3") || header.startsWith("TES4"))
        outExt = QStringLiteral(".esp");
    else if (!ext.isEmpty())
        outExt = QStringLiteral(".") + ext;

    return base + outExt;
}

QString archiveFileName(const QString &currentName, const QString &nexusName,
                        Format fmt)
{
    // Extensions that already tell the extractor (and the FS) what a file is.
    auto hasArchiveExt = [](const QString &name) {
        static const QStringList exts = {
            QStringLiteral("7z"),  QStringLiteral("zip"),  QStringLiteral("rar"),
            QStringLiteral("gz"),  QStringLiteral("tgz"),  QStringLiteral("tar"),
            QStringLiteral("xz"),  QStringLiteral("bz2"),  QStringLiteral("zst"),
            QStringLiteral("zstd"),QStringLiteral("cab"),  QStringLiteral("fomod"),
            QStringLiteral("iso"),
        };
        return exts.contains(QFileInfo(name).suffix().toLower());
    };

    // Normal premium download ("Mod-1234-1-0.7z"): already usable, keep it.
    if (hasArchiveExt(currentName)) return currentName;

    // Authoritative Nexus files.json name when it carries a real extension.
    if (!nexusName.isEmpty() && hasArchiveExt(nexusName)) {
        static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|]"));
        QString clean = nexusName;
        clean.replace(bad, QStringLiteral("_"));
        clean = clean.trimmed();
        if (!clean.isEmpty()) return clean;
    }

    // Bare, extensionless CDN id: append the sniffed container's extension so
    // the on-disk file and its extract folder get a sane name. Unknown -> leave.
    const QString ext = extensionFor(fmt);
    if (ext.isEmpty()) return currentName;
    return currentName + ext;
}

} // namespace archive_magic
