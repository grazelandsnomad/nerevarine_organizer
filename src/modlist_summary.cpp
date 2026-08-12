#include "modlist_summary.h"

#include <QFileInfo>

namespace modlist_summary {

Stats computeStats(const QList<ModEntry> &entries,
                   const std::function<qint64(const QString &modPath)> &sizeLookup)
{
    Stats s;
    for (const ModEntry &e : entries) {
        if (e.isSeparator()) { ++s.sepCount; continue; }
        if (e.installStatus != 1) continue;   // skip not-installed / installing
        ++s.modCount;

        qint64 bytes = e.modSize;
        if (bytes <= 0 && sizeLookup && !e.modPath.isEmpty())
            bytes = sizeLookup(e.modPath);

        if (bytes > 0) {
            s.totalBytes += bytes;
            if (e.checked) { s.enabledBytes += bytes; ++s.enabledCount; }
        } else if (e.checked) {
            ++s.enabledCount;   // size still unknown, but it is enabled
        }
    }
    return s;
}

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0) return QStringLiteral("0 B");
    constexpr double KB = 1024.0;
    constexpr double MB = KB * 1024.0;
    constexpr double GB = MB * 1024.0;
    if (bytes >= GB) return QString::number(bytes / GB, 'f', 2) + " GB";
    if (bytes >= MB) return QString::number(bytes / MB, 'f', 1) + " MB";
    if (bytes >= KB) return QString::number(bytes / KB, 'f', 0) + " KB";
    return QString::number(bytes) + " B";
}

int countOutsideModsDir(const QList<ModEntry> &entries, const QString &modsDir)
{
    if (modsDir.isEmpty()) return 0;
    const QString root = QFileInfo(modsDir).absoluteFilePath();

    int outside = 0;
    for (const ModEntry &e : entries) {
        if (!e.isMod() || e.installStatus != 1) continue;
        if (e.modPath.isEmpty() || !QFileInfo(e.modPath).isDir()) continue;

        // Prefix-compare on the absolute path, with the trailing separator so a
        // sibling like "<root>_old" isn't mistaken for a child of "<root>".
        const QString abs = QFileInfo(e.modPath).absoluteFilePath();
        if (abs != root && !abs.startsWith(root + "/")) ++outside;
    }
    return outside;
}

} // namespace modlist_summary
