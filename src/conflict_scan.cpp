#include "conflict_scan.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStringList>

#include "bsareader.h"
#include "pluginparser.h"

ConflictMap scanConflicts(QList<ConflictScanInput> mods,
                          std::shared_ptr<std::atomic<bool>> cancel)
{
    static const QStringList contentExts{".esp", ".esm", ".omwaddon", ".omwscripts"};
    ConflictMap all;

    for (const auto &m : mods) {
        if (cancel->load()) return {};

        // Same data= roots as syncOpenMWConfig: plugin dirs, then resource
        // roots as fallback for pure-asset mods.
        QStringList roots;
        for (const auto &p : plugins::collectDataFolders(m.modPath, contentExts))
            roots << p.first;
        if (roots.isEmpty()) {
            roots = plugins::collectResourceFolders(m.modPath);
            if (roots.isEmpty()) roots << m.modPath;
        }

        for (const QString &root : roots) {
            if (cancel->load()) return {};
            const int rootLen = root.length() + 1;
            QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (cancel->load()) return {};
                it.next();
                const QString full = it.filePath();
                const QString rel  = full.mid(rootLen).toLower();
                if (rel.isEmpty()) continue;

                all[rel].append({m.modLabel, root, QString()});

                // Peek inside .bsa so packed assets show up alongside loose
                // files. listTes3BsaFiles returns empty for non-TES3 BSAs
                // (Skyrim/Oblivion/Fallout); the inspector ignores those.
                if (rel.endsWith(QStringLiteral(".bsa"))) {
                    const QString bsaName = QFileInfo(full).fileName();
                    for (const QString &inner : bsa::listTes3BsaFiles(full)) {
                        if (cancel->load()) return {};
                        if (!inner.isEmpty())
                            all[inner].append({m.modLabel, root, bsaName});
                    }
                }
            }
        }
    }

    // Drop single-provider entries; keep only the conflicts.
    for (auto it = all.begin(); it != all.end(); ) {
        if (it.value().size() < 2) it = all.erase(it);
        else ++it;
    }
    return all;
}
