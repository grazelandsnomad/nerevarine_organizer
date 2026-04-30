#include "fomod_path.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace fomod {

QString resolvePath(const QString &root, const QString &relativeIn)
{
    QString relative = relativeIn;
    relative.replace('\\', '/');
    if (relative.isEmpty()) return root;

    QStringList segments = relative.split('/', Qt::SkipEmptyParts);
    QString current = root;
    for (const QString &seg : segments) {
        QDir d(current);
        if (!d.exists()) return {};

        // Scan the directory and pick the entry that matches case-insensitively.
        // We can't trust QFileInfo::exists for the fast path: on Windows /
        // macOS it returns true for any case, leaving the input case in the
        // returned path instead of the on-disk one.
        const QStringList entries =
            d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        QString exact, ci;
        for (const QString &e : entries) {
            if (e == seg) { exact = e; break; }
            if (ci.isEmpty() && e.compare(seg, Qt::CaseInsensitive) == 0)
                ci = e;
        }
        const QString match = !exact.isEmpty() ? exact : ci;
        if (match.isEmpty()) return {};
        current = d.filePath(match);
    }
    return current;
}

} // namespace fomod
