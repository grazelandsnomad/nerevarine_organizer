#include "archive_magic.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

#include <initializer_list>

namespace archive_magic {

bool looksLikeArchive(const QByteArray &header)
{
    auto match = [&](std::initializer_list<unsigned char> sig) {
        if (header.size() < static_cast<int>(sig.size())) return false;
        int i = 0;
        for (unsigned char b : sig)
            if (static_cast<unsigned char>(header[i++]) != b) return false;
        return true;
    };

    return match({0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C})        // 7z
        || match({0x50, 0x4B, 0x03, 0x04})                    // zip (local file)
        || match({0x50, 0x4B, 0x05, 0x06})                    // zip (empty)
        || match({0x50, 0x4B, 0x07, 0x08})                    // zip (spanned)
        || match({0x52, 0x61, 0x72, 0x21, 0x1A, 0x07})        // rar (4.x/5.x)
        || match({0x1F, 0x8B})                                // gzip (.gz/.tgz)
        || match({0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00})        // xz
        || match({0x42, 0x5A, 0x68})                          // bzip2
        || match({0x28, 0xB5, 0x2F, 0xFD})                    // zstd
        || match({0x4D, 0x53, 0x43, 0x46});                   // cab
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

} // namespace archive_magic
