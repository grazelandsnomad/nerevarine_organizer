#include "post_install.h"

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QPair>
#include <QRegularExpression>

namespace post_install {

namespace {

// Grass mods whose name has neither "grass" nor "groundcover" in it.
// Add more as they show up.
const QStringList &groundcoverNameHints()
{
    static const QStringList hints = { QStringLiteral("lush synthesis") };
    return hints;
}

bool containsAnyHint(const QString &s)
{
    for (const QString &h : groundcoverNameHints())
        if (s.contains(h, Qt::CaseInsensitive)) return true;
    return false;
}

const QStringList &splashImageGlobs()
{
    static const QStringList g = { QStringLiteral("*.tga"), QStringLiteral("*.bmp"),
                                   QStringLiteral("*.png"), QStringLiteral("*.jpg") };
    return g;
}

} // namespace

bool looksLikeGroundcover(const QString &modPath, const QString &displayName)
{
    return modPath.contains(QStringLiteral("groundcover"), Qt::CaseInsensitive)
        || modPath.contains(QStringLiteral("grass"),       Qt::CaseInsensitive)
        || displayName.contains(QStringLiteral("groundcover"), Qt::CaseInsensitive)
        || displayName.contains(QStringLiteral("grass"),       Qt::CaseInsensitive)
        || containsAnyHint(modPath)
        || containsAnyHint(displayName);
}

QString findSplashDir(const QString &modRoot)
{
    const QFileInfo fi(modRoot);
    // Mod root is itself a splash/ dir (case-insensitive).
    if (fi.fileName().compare(QStringLiteral("splash"), Qt::CaseInsensitive) == 0) {
        QDir sd(modRoot);
        if (!sd.entryList(splashImageGlobs(), QDir::Files).isEmpty())
            return sd.absolutePath();
    }
    // Otherwise BFS to depth 3 for a splash/ subdir holding an image.
    QList<QPair<QString, int>> queue;
    queue.append({ modRoot, 0 });
    while (!queue.isEmpty()) {
        const auto [path, depth] = queue.takeFirst();
        QDir d(path);
        for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (sub.compare(QStringLiteral("splash"), Qt::CaseInsensitive) == 0) {
                QDir sd(d.filePath(sub));
                if (!sd.entryList(splashImageGlobs(), QDir::Files).isEmpty())
                    return sd.absolutePath();
            }
            if (depth < 3)
                queue.append({ d.filePath(sub), depth + 1 });
        }
    }
    return {};
}

QString normalizeModName(const QString &s)
{
    QString n;
    n.reserve(s.size());
    for (const QChar &c : s)
        if (c.isLetterOrNumber()) n.append(c.toLower());
    return n;
}

bool bundledPatchMatchesMod(const QString &subfolderName,
                            const QString &normalizedModName)
{
    if (normalizedModName.length() < 4) return false;

    static const QRegularExpression prefixed(
        QStringLiteral("^\\s*\\d+[a-zA-Z]?\\s+(.+)$"));
    static const QRegularExpression forPat(
        QStringLiteral("\\bfor\\s+(.+?)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);

    const auto pm = prefixed.match(subfolderName);
    if (!pm.hasMatch()) return false;
    const auto fm = forPat.match(pm.captured(1));
    if (!fm.hasMatch()) return false;
    return normalizeModName(fm.captured(1)).contains(normalizedModName);
}

} // namespace post_install
