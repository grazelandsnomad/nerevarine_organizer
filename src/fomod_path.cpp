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
        // FOMOD source paths come from an untrusted archive; "../" would read
        // outside the extracted root. Legit paths never contain it.
        if (seg == QLatin1String("..")) return {};
        if (seg == QLatin1String(".")) continue;
        QDir d(current);
        if (!d.exists()) return {};

        // Scan and match case-insensitively. QFileInfo::exists is no good here:
        // on Windows/macOS it returns true for any case and keeps the input
        // case instead of the on-disk one.
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

ResolvedPath resolveDest(const QString &root, const QString &relativeIn)
{
    QString relative = relativeIn;
    relative.replace('\\', '/');
    if (relative.isEmpty()) return ResolvedPath(root);

    QString current = root;
    const QStringList segments = relative.split('/', Qt::SkipEmptyParts);
    for (const QString &seg : segments) {
        // Untrusted FOMOD dest: a "../" segment would let copyContents /
        // QFile::copy write outside the staging dir. Return "" so the caller
        // fails the entry instead of escaping root.
        if (seg == QLatin1String("..")) return ResolvedPath(QString());
        if (seg == QLatin1String(".")) continue;
        QDir d(current);
        // Reuse the casing of an already-staged component (exact match first,
        // else first case-insensitive). Keeps the case-sensitive FS behaving
        // like the case-insensitive one the mod was authored for: a FOMOD
        // "meshes" lands in an existing "Meshes/" instead of forking a
        // case-variant duplicate.
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
        // Nothing exists yet: adopt the authored casing; the first option to
        // use a path fixes it and later options reconcile onto it. Unlike
        // resolvePath this never fails - a missing dest component is normal.
        current = d.filePath(match.isEmpty() ? seg : match);
    }
    return ResolvedPath(current);
}

DestIndex::DestIndex(const QString &dir) : m_dir(dir)
{
    // The listing resolveDest makes, in its order: its "first case-insensitive
    // match" is the first in this order. Never the working directory, which is
    // what QDir makes of an empty path.
    if (dir.isEmpty()) return;
    const QDir d(dir);
    if (!d.exists()) return;
    for (const QString &e : d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
        add(e);
}

ResolvedPath DestIndex::child(const QString &name) const
{
    if (m_dir.isEmpty() || name == QLatin1String("..")) return ResolvedPath(QString());
    if (name.isEmpty() || name == QLatin1String(".")) return ResolvedPath(m_dir);
    if (m_names.contains(name)) return ResolvedPath(QDir(m_dir).filePath(name));
    // Case-folded, as QString::compare(..., Qt::CaseInsensitive) compares.
    const QString match = m_firstByLower.value(name.toCaseFolded());
    return ResolvedPath(QDir(m_dir).filePath(match.isEmpty() ? name : match));
}

void DestIndex::add(const QString &name)
{
    m_names.insert(name);
    const QString folded = name.toCaseFolded();
    if (!m_firstByLower.contains(folded)) m_firstByLower.insert(folded, name);
}

} // namespace fomod
