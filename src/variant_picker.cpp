#include "variant_picker.h"

#include "fomod_copy.h"
#include "fomod_path.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>

namespace variant_picker {

namespace {

bool hasDataChild(const QString &dir)
{
    const QStringList subs =
        QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    return std::any_of(subs.cbegin(), subs.cend(), [](const QString &s) {
        return s.compare(QLatin1String("data"), Qt::CaseInsensitive) == 0;
    });
}

// The chooser test for one candidate folder. Strict on purpose - see header.
bool isChooser(const QString &dir, QStringList &optionsOut)
{
    const QStringList subs =
        QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (subs.size() < 2) return false;
    for (const QString &s : subs)
        if (!hasDataChild(QDir(dir).filePath(s))) return false;
    optionsOut = subs;
    return true;
}

} // namespace

Chooser find(const QString &modRoot)
{
    Chooser out;
    if (modRoot.isEmpty() || !QDir(modRoot).exists()) return out;

    // Breadth-first, three levels: Main Menu Redone's chooser sits at
    // <mod>/Data/mainmenuwallpapers, and one more level covers a wrapper.
    // Deeper than that stops being an install choice and starts being the
    // mod's own content.
    QStringList frontier{modRoot};
    for (int depth = 0; depth < 3 && !frontier.isEmpty(); ++depth) {
        QStringList next;
        for (const QString &dir : frontier) {
            const QStringList subs =
                QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString &s : subs) {
                const QString cand = QDir(dir).filePath(s);
                QStringList options;
                if (isChooser(cand, options)) {
                    out.dir     = cand;
                    out.destDir = dir;
                    out.options = options;
                    std::sort(out.options.begin(), out.options.end(),
                              [](const QString &a, const QString &b) {
                        return a.compare(b, Qt::CaseInsensitive) < 0;
                    });
                    return out;
                }
                next << cand;
            }
        }
        frontier = next;
    }
    return out;
}

ApplyResult apply(const Chooser &ch, const QString &option)
{
    ApplyResult res;
    if (ch.dir.isEmpty() || option.isEmpty()) {
        res.errors << QStringLiteral("no chooser/option");
        return res;
    }

    // The option's data/ child, with whatever casing it actually has.
    const QString optionDir = QDir(ch.dir).filePath(option);
    QString dataDir;
    for (const QString &s :
         QDir(optionDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (s.compare(QLatin1String("data"), Qt::CaseInsensitive) == 0) {
            dataDir = QDir(optionDir).filePath(s);
            break;
        }
    if (dataDir.isEmpty()) {
        res.errors << QStringLiteral("option has no data folder: %1").arg(option);
        return res;
    }

    const QDir base(dataDir);
    QDirIterator it(dataDir, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString abs = it.next();
        const QString rel = base.relativeFilePath(abs);
        // resolveDest merges "DATA/TEXTURES" into an existing "textures"
        // instead of forking a case-variant duplicate - the reason this
        // does not hand-roll the copy.
        const auto dst = fomod::resolveDest(ch.destDir, rel);
        if (fomod_copy::copyFile(abs, dst)) ++res.copied;
        else res.errors << QStringLiteral("copy failed: %1").arg(rel);
    }
    return res;
}

} // namespace variant_picker
