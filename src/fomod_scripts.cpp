#include "fomod_scripts.h"
#include "fomod_path.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace fomod_scripts {

void installDeclaredScripts(const QString &manifestSrc,
                            const QString &archiveRoot,
                            const QString &installDir)
{
    QFile manifest(manifestSrc);
    if (!manifest.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    const QString manifestParent = QFileInfo(manifestSrc).absolutePath();

    while (!manifest.atEnd()) {
        QString line = QString::fromUtf8(manifest.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) continue;

        // Format: "<CONTEXT>: <path/to/script.lua>".  The path is what
        // OpenMW will resolve against the data dir at runtime, so it's
        // also the path we want under installDir.
        const int colon = line.indexOf(u':');
        if (colon < 0) continue;
        QString scriptPath = line.mid(colon + 1).trimmed();
        scriptPath.replace(u'\\', u'/');
        if (scriptPath.isEmpty()) continue;

        // Try archive root first (the "scripts/ at root has all content"
        // shape), then the manifest's parent (the per-plugin folder shape
        // used by Completionist Patch Hub).  fomod::resolvePath is case-
        // insensitive, so a manifest written for Windows still resolves
        // on Linux extracts.
        QString src = fomod::resolvePath(archiveRoot, scriptPath);
        if (src.isEmpty() || !QFileInfo::exists(src))
            src = fomod::resolvePath(manifestParent, scriptPath);
        if (src.isEmpty() || !QFileInfo::exists(src)) continue;

        const QString dst = installDir + "/" + scriptPath;
        if (QFileInfo::exists(dst)) continue;  // already placed by a folder= entry
        QDir().mkpath(QFileInfo(dst).absolutePath());
        QFile::copy(src, dst);
    }
}

} // namespace fomod_scripts
