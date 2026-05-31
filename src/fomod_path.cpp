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

QString resolveDest(const QString &root, const QString &relativeIn)
{
    QString relative = relativeIn;
    relative.replace('\\', '/');
    if (relative.isEmpty()) return root;

    QString current = root;
    const QStringList segments = relative.split('/', Qt::SkipEmptyParts);
    for (const QString &seg : segments) {
        QDir d(current);
        // Reuse the on-disk casing of a component that already exists (exact
        // match wins, else the first case-insensitive match). This is what
        // makes the install behave like the case-insensitive filesystem the
        // mod was authored for: a FOMOD option spelling a folder "meshes"
        // lands in an already-staged "Meshes/" instead of forking a duplicate
        // case-variant directory on a case-sensitive filesystem.
        QString match;
        if (d.exists()) {
            const QStringList entries =
                d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
            for (const QString &e : entries) {
                if (e == seg) { match = e; break; }
                if (match.isEmpty() && e.compare(seg, Qt::CaseInsensitive) == 0)
                    match = e;
            }
        }
        // Nothing exists yet (any case): adopt the authored casing and let the
        // caller create it. The first option to use a path fixes its casing;
        // later options reconcile onto it. Unlike resolvePath this never fails
        // — it is a destination resolver, so a missing component is normal.
        current = d.filePath(match.isEmpty() ? seg : match);
    }
    return current;
}

} // namespace fomod
