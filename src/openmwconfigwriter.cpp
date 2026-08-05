#include "openmwconfigwriter.h"

#include "master_satisfaction.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStringView>

#include <algorithm>

namespace openmw {

// Markers wrapping our own data=/content= lines. renderOpenMWConfig,
// prepareForSync and externalDataPaths all key off these.
namespace {
const QString kManagedBegin = QStringLiteral("# --- Nerevarine Organizer BEGIN ---");
const QString kManagedEnd   = QStringLiteral("# --- Nerevarine Organizer END ---");
} // namespace

SyncPrepareResult prepareForSync(const SyncPrepareInputs &in)
{
    const QString &kBegin = kManagedBegin;
    const QString &kEnd   = kManagedEnd;
    static const QStringList contentExts{".esp", ".esm", ".omwaddon", ".omwscripts"};

    SyncPrepareResult out;
    out.mods = in.mods;     // copy; we mutate suppressedPlugins/groundcoverFiles

    QString existing = in.existingCfg;
    const QString modsRootForRescue = in.modsRoot.isEmpty()
        ? QString() : QDir::cleanPath(in.modsRoot);

    // Orphan-managed rescue: on Import or remove-keep-on-disk, the managed
    // section can hold data= lines no current mod claims. The scrub below would
    // drop them (and their content= plugins), wiping the Launcher Content List
    // for mods still on disk. So an orphan path whose folder still has plugins
    // gets re-emitted OUTSIDE the managed section.
    QSet<QString> rescuedManagedPaths;
    {
        QStringList rescuedDataLines;
        if (!modsRootForRescue.isEmpty()) {
            bool inManaged = false;
            QStringList filters;
            for (const QString &ext : contentExts) filters << "*" + ext;
            for (const QString &raw : existing.split('\n')) {
                QString line = raw;
                if (line.endsWith('\r')) line.chop(1);
                if (line == kBegin) { inManaged = true;  continue; }
                if (line == kEnd)   { inManaged = false; continue; }
                if (!inManaged) continue;
                if (!line.startsWith(QStringLiteral("data="))) continue;
                QString path = line.mid(5);
                if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"'))
                    path = path.mid(1, path.size() - 2);
                const QString clean = QDir::cleanPath(path);
                if (clean != modsRootForRescue
                    && !clean.startsWith(modsRootForRescue + "/")) continue;
                bool claimed = false;
                for (const QString &mp : in.managedModPaths) {
                    if (clean == mp || clean.startsWith(mp + "/")) {
                        claimed = true;
                        break;
                    }
                }
                if (claimed) continue;
                QDir d(path);
                if (!d.exists()) continue;
                if (d.entryList(filters, QDir::Files).isEmpty()) continue;
                rescuedDataLines << QStringLiteral("data=\"") + path
                                  + QStringLiteral("\"");
                rescuedManagedPaths.insert(clean);
            }
        }
        if (!rescuedDataLines.isEmpty())
            existing = rescuedDataLines.join('\n') + '\n' + existing;
    }

    // Launcher-only externals: the Launcher only writes selected data=/content=
    // into openmw.cfg on Save/Play. Someone who set up via openmw-launcher and
    // never reopened it can have vanilla data=/content= in launcher.cfg but not
    // openmw.cfg. Treat as real external state, synthesised outside BEGIN/END.
    if (!in.launcherCfgText.isEmpty()) {
        const QStringList lcDataPaths   = readLauncherCfgDataPaths(in.launcherCfgText);
        const QStringList lcContentFiles = readLauncherCfgContentOrder(in.launcherCfgText);

        QSet<QString> existingDataPaths;
        QSet<QString> existingContent;
        for (const QString &raw : existing.split('\n')) {
            QString l = raw;
            if (l.endsWith('\r')) l.chop(1);
            if (l.startsWith(QStringLiteral("data="))) {
                QString p = l.mid(5);
                if (p.size() >= 2 && p.startsWith('"') && p.endsWith('"'))
                    p = p.mid(1, p.size() - 2);
                existingDataPaths.insert(QDir::cleanPath(p));
            } else if (l.startsWith(QStringLiteral("content="))) {
                existingContent.insert(l.mid(8));
            }
        }

        // Filenames any managed mod knows, enabled or not. Disabled-mod plugins
        // are still ours; carrying them back as fake externals would leave their
        // content= in the launcher's Selected list when disabled.
        QSet<QString> allManagedFilenames;
        for (const auto &cm : out.mods)
            for (const auto &p : cm.pluginDirs)
                for (const QString &cf : p.second)
                    allManagedFilenames.insert(cf);

        const QString cleanModsRoot = modsRootForRescue;

        QStringList synth;
        for (const QString &p : lcDataPaths) {
            const QString clean = QDir::cleanPath(p);
            if (existingDataPaths.contains(clean)) continue;
            if (!cleanModsRoot.isEmpty() &&
                (clean == cleanModsRoot ||
                 clean.startsWith(cleanModsRoot + "/")))
                continue;
            synth << QStringLiteral("data=\"") + p + QStringLiteral("\"");
        }
        for (const QString &cf : lcContentFiles) {
            if (existingContent.contains(cf)) continue;
            if (allManagedFilenames.contains(cf)) continue;
            synth << QStringLiteral("content=") + cf;
        }

        if (!synth.isEmpty())
            existing = synth.join('\n') + '\n' + existing;
    }

    // Build providedPlugins, then drop content= lines no data= dir provides,
    // else OpenMW aborts: "Failed loading X.esp: the content file does not exist".
    QSet<QString> providedPlugins;
    for (const auto &cm : out.mods) {
        for (const auto &p : cm.pluginDirs)
            for (const QString &cf : p.second) providedPlugins.insert(cf);
    }
    auto isOrphanedManagedPath = [&](const QString &rawPath) -> bool {
        if (modsRootForRescue.isEmpty()) return false;
        const QString p = QDir::cleanPath(rawPath);
        if (p != modsRootForRescue
            && !p.startsWith(modsRootForRescue + "/")) return false;
        if (rescuedManagedPaths.contains(p)) return false;
        for (const QString &mp : in.managedModPaths)
            if (p == mp || p.startsWith(mp + "/")) return false;
        return true;
    };
    {
        bool inManaged = false;
        for (QString line : existing.split('\n')) {
            if (line.endsWith('\r')) line.chop(1);
            if (line == kBegin) { inManaged = true;  continue; }
            if (line == kEnd)   { inManaged = false; continue; }
            if (inManaged)        continue;
            if (!line.startsWith(QStringLiteral("data="))) continue;
            QString path = line.mid(5);
            if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"'))
                path = path.mid(1, path.size() - 2);
            if (isOrphanedManagedPath(path)) continue;
            QDir d(path);
            if (!d.exists()) continue;
            QStringList filters;
            for (const QString &ext : contentExts) filters << "*" + ext;
            for (const QString &f : d.entryList(filters, QDir::Files))
                providedPlugins.insert(f);
        }
    }

    // Master satisfaction. A plugin missing a master crashes OpenMW with
    // "File X asks for parent file Y, but it is not available or has been
    // loaded in the wrong order" - same for content= and groundcover=.
    // Detected by openmw::findUnsatisfiedMasters.
    {
        static const QSet<QString> baseMasters = {
            "morrowind.esm", "tribunal.esm", "bloodmoon.esm"
        };
        QList<openmw::PluginRef> candidates;
        QSet<QString>            availableLower;
        for (const QString &cf : providedPlugins) availableLower.insert(cf.toLower());
        for (const QString &bm : baseMasters)     availableLower.insert(bm);

        for (const auto &cm : out.mods) {
            if (!cm.enabled || !cm.installed) continue;
            for (const auto &p : cm.pluginDirs)
                for (const QString &cf : p.second) {
                    if (cm.suppressedPlugins.contains(cf)) continue;
                    candidates.append({cf, QDir(p.first).filePath(cf)});
                }
        }

        const QSet<QString> unsatisfied =
            openmw::findUnsatisfiedMasters(candidates, availableLower);

        if (!unsatisfied.isEmpty()) {
            for (auto &cm : out.mods) {
                QSet<QString> hits;
                for (const auto &p : cm.pluginDirs)
                    for (const QString &cf : p.second)
                        if (unsatisfied.contains(cf)) hits.insert(cf);
                if (hits.isEmpty()) continue;
                cm.groundcoverFiles  -= hits;
                cm.suppressedPlugins += hits;
            }
        }
    }

    // Scrub: build scrubbedExisting + droppedOrphans count. groundcover= lines
    // for unprovided plugins drop too; groundcover stays out of the load order.
    QSet<QString> groundcoverPlugins;
    QSet<QString> suppressedFromLoadOrder;
    for (const auto &cm : out.mods) {
        for (const QString &gf : cm.groundcoverFiles)
            groundcoverPlugins.insert(gf);
        for (const QString &sp : cm.suppressedPlugins)
            suppressedFromLoadOrder.insert(sp);
    }

    QStringList scrubbedLines;
    for (QString line : existing.split('\n')) {
        QString probe = line;
        if (probe.endsWith('\r')) probe.chop(1);
        if (probe.startsWith(QStringLiteral("data="))) {
            QString path = probe.mid(5);
            if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"'))
                path = path.mid(1, path.size() - 2);
            if (isOrphanedManagedPath(path)) continue;
        }
        if (probe.startsWith(QStringLiteral("content="))) {
            QString cf = probe.mid(8);
            if (!providedPlugins.contains(cf)) {
                ++out.droppedOrphans;
                continue;
            }
        }
        if (probe.startsWith(QStringLiteral("groundcover="))) {
            QString cf = probe.mid(12);
            if (!providedPlugins.contains(cf)) continue;
        }
        scrubbedLines << line;
    }
    out.scrubbedExisting = scrubbedLines.join('\n');

    // Effective load order: drop unprovided plugins, groundcover (loaded
    // separately) and suppressed (missing masters). Any left in resurrect on
    // the next absorb cycle and the crash recurs.
    out.effectiveLoadOrder.reserve(in.loadOrder.size());
    for (const QString &cf : in.loadOrder)
        if (providedPlugins.contains(cf)
         && !groundcoverPlugins.contains(cf)
         && !suppressedFromLoadOrder.contains(cf))
            out.effectiveLoadOrder << cf;

    return out;
}

namespace {

// .esm first, then the rest, each group case-insensitive alphabetical.
// Deterministic so reruns produce identical cfg bytes.
QStringList pluginOrderWithinMod(QStringList files)
{
    std::sort(files.begin(), files.end(),
              [](const QString &a, const QString &b) {
        bool ae = a.endsWith(".esm", Qt::CaseInsensitive);
        bool be = b.endsWith(".esm", Qt::CaseInsensitive);
        if (ae != be) return ae;
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return files;
}

} // namespace

QString renderOpenMWConfig(const QList<ConfigMod> &mods,
                           const QStringList   &loadOrder,
                           const QString       &existingCfg)
{
    const QString &BEGIN = kManagedBegin;
    const QString &END   = kManagedEnd;

    // Pass 1: build lists from the mod list. Each mod emits every plugin-bearing
    // subdir as a data= path; filenames collected in modlist order since OpenMW
    // loads content= in file order, so reordering mods reorders content=.
    //   availableContentOrdered  content from enabled+installed mods, modlist
    //                            order, deduped.
    //   allManagedContent        every content file in ANY installed mod (enabled
    //                            or not); Pass 3 uses it to tell ours from
    //                            base-game/external.
    QStringList   dataLines;
    QStringList   availableContentOrdered;
    QSet<QString> availableSeen;
    QSet<QString> allManagedContent;
    QStringList   groundcoverOrdered;
    QSet<QString> groundcoverSeen;
    QSet<QString> allManagedGroundcover;
    QSet<QString> allSuppressed;
    // Mod BSAs in modlist order, deduped. Emitted in the managed section as
    // fallback-archive=<basename> so OpenMW resolves meshes/textures the mod's
    // plugins reference. allManagedArchives = every BSA we'll emit, so Pass 2 can
    // drop duplicate pre-existing fallback-archive= lines.
    QStringList   archiveOrdered;
    QSet<QString> archiveSeen;
    QSet<QString> allManagedArchives;

    for (const ConfigMod &m : mods) {
        if (!m.installed) continue;

        for (const auto &p : m.pluginDirs)
            for (const QString &cf : p.second) {
                if (m.suppressedPlugins.contains(cf)) {
                    allSuppressed.insert(cf);
                    continue;
                }
                if (m.groundcoverFiles.contains(cf))
                    allManagedGroundcover.insert(cf);
                else
                    allManagedContent.insert(cf);
            }

        if (!m.enabled) continue;

        if (!m.pluginDirs.isEmpty()) {
            for (const auto &p : m.pluginDirs) {
                dataLines << "data=\"" + p.first + "\"";
                for (const QString &cf : pluginOrderWithinMod(p.second)) {
                    if (m.suppressedPlugins.contains(cf))
                        continue;    // excluded from everything
                    if (m.groundcoverFiles.contains(cf)) {
                        if (!groundcoverSeen.contains(cf)) {
                            groundcoverSeen.insert(cf);
                            groundcoverOrdered << cf;
                        }
                    } else {
                        if (!availableSeen.contains(cf)) {
                            availableSeen.insert(cf);
                            availableContentOrdered << cf;
                        }
                    }
                }
            }
        } else {
            // Pure resource mod (menu replacer, retexture, sound pack): no
            // plugins, but still needs a data= entry for its assets.
            for (const QString &r : m.resourceRoots)
                dataLines << "data=\"" + r + "\"";
        }

        // Record BSAs for every installed mod (so Pass 2 drops stale entries
        // even from disabled ones), but only emit for enabled - like content=.
        for (const QString &b : m.bsaFiles) {
            allManagedArchives.insert(b);
            if (!m.enabled) continue;
            if (archiveSeen.contains(b)) continue;
            archiveSeen.insert(b);
            archiveOrdered << b;
        }
    }

    // Pass 2: parse existing cfg.
    //   preamble          non-data=/content= lines outside managed section
    //                     (settings, fallback=, etc.)
    //   allPrevContent    all content= filenames from the whole file in
    //                     encounter order (managed + launcher writes like
    //                     Morrowind.esm, Tribunal.esm)
    //   externalDataLines data= lines outside the managed section (base-game etc.)
    // Managed-section data= lines are dropped and rebuilt from the modlist above.
    QStringList   preamble;
    QStringList   allPrevContent;
    QSet<QString> prevContentSeen;
    QStringList   externalDataLines;

    if (!existingCfg.isEmpty()) {
        const QStringList lines = existingCfg.split('\n');
        bool inManaged = false;
        for (int i = 0; i < lines.size(); ++i) {
            QString line = lines.at(i);
            if (line.endsWith('\r')) line.chop(1);   // tolerate CRLF

            // split() on the trailing "\n" leaves an empty tail; skip it so the
            // preamble gets no phantom blank line at the end.
            if (i == lines.size() - 1 && line.isEmpty()) continue;

            if (line == BEGIN) { inManaged = true;  continue; }
            if (line == END)   { inManaged = false; continue; }

            if (line.startsWith(QStringLiteral("content="))) {
                QString fname = line.mid(8);
                if (!prevContentSeen.contains(fname)) {
                    allPrevContent << fname;
                    prevContentSeen.insert(fname);
                }
            } else if (line.startsWith(QStringLiteral("groundcover="))) {
                // Managed groundcover= is rebuilt; keep external ones in the
                // preamble, but only if not also a managed entry. The Launcher
                // sometimes promotes managed groundcover= into the preamble;
                // emitting twice loads the plugin twice and shifts content-file
                // indices under the savegame (grass renders as marker_error).
                if (!inManaged) {
                    const QString cf = line.mid(12);
                    if (!allManagedGroundcover.contains(cf)
                     && !allManagedContent.contains(cf))
                        preamble << line;
                }
            } else if (!inManaged && line.startsWith(QStringLiteral("data="))) {
                externalDataLines << line;
            } else if (!inManaged
                    && line.startsWith(QStringLiteral("fallback-archive="))) {
                // Drop preamble fallback-archive= whose basename Pass 4 re-emits,
                // else an installed mod's BSA that used to sit in the preamble
                // (user-added before we managed it, or migrated) duplicates.
                // Vanilla BSAs are never in allManagedArchives so they survive.
                const QString name = line.mid(QStringLiteral("fallback-archive=").size());
                if (!allManagedArchives.contains(name))
                    preamble << line;
            } else if (!inManaged) {
                preamble << line;
            }
            // Inside managed section: data= and fallback-archive= discarded,
            // rebuilt above.
        }
    }

    // Pass 3: merge content order.
    //   A. Prior content not in allManagedContent = base-game/external; keep at
    //      its prior position.
    //   B. Managed content from an enabled+installed mod: loadOrder order, then
    //      modlist order for anything loadOrder hasn't seen.
    //   C. Managed content from a now-disabled mod: dropped.
    QStringList   contentOrder;
    QSet<QString> seen;

    // Phase A: external plugins in prior encounter order.
    for (const QString &cf : allPrevContent) {
        if (allManagedContent.contains(cf))      continue;
        if (allManagedGroundcover.contains(cf))   continue;
        if (allSuppressed.contains(cf))           continue;
        if (seen.contains(cf))                    continue;
        contentOrder << cf;
        seen.insert(cf);
    }

    // Phase B: managed plugins in loadOrder order.
    QSet<QString> availableSet(availableContentOrdered.begin(),
                                availableContentOrdered.end());
    for (const QString &cf : loadOrder) {
        if (!availableSet.contains(cf)) continue;
        if (seen.contains(cf))           continue;
        contentOrder << cf;
        seen.insert(cf);
    }
    // Phase B fallback: anything loadOrder missed (shouldn't happen once
    // reconcileLoadOrder ran) in modlist order.
    for (const QString &cf : availableContentOrdered) {
        if (seen.contains(cf)) continue;
        contentOrder << cf;
        seen.insert(cf);
    }

    // Pass 4: assemble output.
    //   preamble           settings, fallbacks
    //   externalDataLines  base-game data= paths (outside managed section so the
    //                      launcher still sees them where expected)
    //   BEGIN
    //   dataLines          managed mod data= paths
    //   content=           base-game + managed, Pass 3 order
    //   END
    QString out;
    for (const QString &line : preamble)          out += line + "\n";
    for (const QString &line : externalDataLines) out += line + "\n";
    out += BEGIN + "\n";
    for (const QString &dl  : dataLines)          out += dl   + "\n";
    // fallback-archive= before content= so the archive is registered before
    // OpenMW resolves plugin references. Mod BSAs come after vanilla ones in the
    // preamble, so loose mod files in data= dirs still win over BSA-packed copies
    // (OpenMW loose-file priority).
    for (const QString &b   : archiveOrdered)     out += "fallback-archive=" + b + "\n";
    for (const QString &cf  : contentOrder)       out += "content=" + cf + "\n";
    for (const QString &cf  : groundcoverOrdered) out += "groundcover=" + cf + "\n";
    out += END + "\n";
    return out;
}

QString renderLauncherCfg(const QString     &in,
                          const QStringList &dataPaths,
                          const QStringList &contentFiles)
{
    // Fresh install / launcher never run - leave it; the openmw.cfg path handles it.
    if (in.isEmpty()) return {};

    // launcher.cfg is QSettings INI, but QSettings drops comments, reorders keys
    // and mangles quoting (corrupted real 9k-line files). So rewrite line-based,
    // touching only the current profile's data=/content= block.
    const bool crlf = in.contains(QStringLiteral("\r\n"));
    const QString nl = crlf ? QStringLiteral("\r\n") : QStringLiteral("\n");

    QStringList lines = in.split('\n');
    // Normalise CR now; we avoid re-emitting the final-"\n" blank later.
    for (QString &l : lines) if (l.endsWith('\r')) l.chop(1);

    // Locate [Profiles] section range.
    int profStart = -1;
    int profEnd   = lines.size();
    for (int i = 0; i < lines.size(); ++i) {
        const QString &l = lines.at(i);
        if (!l.startsWith('[') || !l.endsWith(']')) continue;
        if (l == QStringLiteral("[Profiles]")) {
            profStart = i;
        } else if (profStart >= 0) {
            profEnd = i;
            break;
        }
    }
    if (profStart < 0) return {};       // no [Profiles], nothing to do

    // Find currentprofile= inside the section.
    QString ts;
    for (int i = profStart + 1; i < profEnd; ++i) {
        static const QString kKey = QStringLiteral("currentprofile=");
        if (lines.at(i).startsWith(kKey)) {
            ts = lines.at(i).mid(kKey.size());
            break;
        }
    }
    if (ts.isEmpty()) return {};        // no active profile - no-op

    const QString prefix    = ts + QStringLiteral("/");
    const QString dataKey   = prefix + QStringLiteral("data=");
    const QString contentKey = prefix + QStringLiteral("content=");

    // Walk [Profiles], drop existing data=/content= for the current profile and
    // record where the rebuilt block lands. Insertion point = right after the
    // last surviving <ts>/... line, so the block stays contiguous with the
    // profile's other keys and doesn't slice into a neighbouring profile.
    QStringList kept;
    int insertAfter          = -1;
    int currentProfileLineIx = -1;
    for (int i = profStart + 1; i < profEnd; ++i) {
        const QString &l = lines.at(i);
        if (l.startsWith(dataKey) || l.startsWith(contentKey)) continue;
        kept << l;
        const int ix = kept.size() - 1;
        if (l.startsWith(QStringLiteral("currentprofile=")))
            currentProfileLineIx = ix;
        if (l.startsWith(prefix)) insertAfter = ix;
    }
    // No other <ts>/ line survived: park the block right after currentprofile=.
    // (If currentprofile= isn't there we already returned above.)
    if (insertAfter < 0) insertAfter = currentProfileLineIx;

    QStringList block;
    block.reserve(dataPaths.size() + contentFiles.size());
    // launcher.cfg stores paths UNQUOTED even with spaces (verified against a
    // live file). Quoting makes the launcher treat the quotes as literal path
    // characters.
    for (const QString &p : dataPaths)    block << prefix + QStringLiteral("data=")    + p;
    for (const QString &c : contentFiles) block << prefix + QStringLiteral("content=") + c;

    QStringList newProfiles;
    newProfiles.reserve(kept.size() + block.size());
    for (int i = 0; i < kept.size(); ++i) {
        newProfiles << kept.at(i);
        if (i == insertAfter)
            for (const QString &b : block) newProfiles << b;
    }

    QStringList outLines;
    outLines.reserve(lines.size() + block.size());
    for (int i = 0; i <= profStart; ++i) outLines << lines.at(i);
    outLines += newProfiles;
    for (int i = profEnd; i < lines.size(); ++i) outLines << lines.at(i);

    return outLines.join(nl);
}

QStringList readLauncherCfgContentOrder(const QString &in)
{
    if (in.isEmpty()) return {};

    // Same parsing shape as renderLauncherCfg: locate [Profiles], find
    // currentprofile=, enumerate <ts>/content= lines in encounter order.
    // data= and fallback-archive= ignored here.
    QStringList lines = in.split('\n');
    for (QString &l : lines) if (l.endsWith('\r')) l.chop(1);

    int profStart = -1;
    int profEnd   = lines.size();
    for (int i = 0; i < lines.size(); ++i) {
        const QString &l = lines.at(i);
        if (!l.startsWith('[') || !l.endsWith(']')) continue;
        if (l == QStringLiteral("[Profiles]")) {
            profStart = i;
        } else if (profStart >= 0) {
            profEnd = i;
            break;
        }
    }
    if (profStart < 0) return {};

    QString ts;
    for (int i = profStart + 1; i < profEnd; ++i) {
        static const QString kKey = QStringLiteral("currentprofile=");
        if (lines.at(i).startsWith(kKey)) {
            ts = lines.at(i).mid(kKey.size());
            break;
        }
    }
    if (ts.isEmpty()) return {};

    const QString contentKey = ts + QStringLiteral("/content=");
    QStringList out;
    for (int i = profStart + 1; i < profEnd; ++i) {
        const QString &l = lines.at(i);
        if (l.startsWith(contentKey))
            out << l.mid(contentKey.size());
    }
    return out;
}

QStringList readLauncherCfgDataPaths(const QString &in)
{
    if (in.isEmpty()) return {};

    // Same parsing shape as readLauncherCfgContentOrder: locate [Profiles],
    // find currentprofile=, enumerate <ts>/data= lines in encounter order.
    // Returned unquoted (the launcher writes them unquoted; quoting mangles
    // paths with spaces).
    QStringList lines = in.split('\n');
    for (QString &l : lines) if (l.endsWith('\r')) l.chop(1);

    int profStart = -1;
    int profEnd   = lines.size();
    for (int i = 0; i < lines.size(); ++i) {
        const QString &l = lines.at(i);
        if (!l.startsWith('[') || !l.endsWith(']')) continue;
        if (l == QStringLiteral("[Profiles]")) {
            profStart = i;
        } else if (profStart >= 0) {
            profEnd = i;
            break;
        }
    }
    if (profStart < 0) return {};

    QString ts;
    for (int i = profStart + 1; i < profEnd; ++i) {
        static const QString kKey = QStringLiteral("currentprofile=");
        if (lines.at(i).startsWith(kKey)) {
            ts = lines.at(i).mid(kKey.size());
            break;
        }
    }
    if (ts.isEmpty()) return {};

    const QString dataKey = ts + QStringLiteral("/data=");
    QStringList out;
    for (int i = profStart + 1; i < profEnd; ++i) {
        const QString &l = lines.at(i);
        if (!l.startsWith(dataKey)) continue;
        QString p = l.mid(dataKey.size());
        // Tolerate quotes in case a future Launcher starts wrapping paths.
        if (p.size() >= 2 && p.startsWith('"') && p.endsWith('"'))
            p = p.mid(1, p.size() - 2);
        out << p;
    }
    return out;
}

// -- Importer parser ----------------------------------------------------
//
// openmw.cfg is one entry per line, key=value, with `# ` comments and the
// launcher's habit of quoting data= paths with spaces. Import keeps only three
// keys; everything else (fallback=, script-blacklist=, replace=, ...) dropped.

ImportEntries parseConfigEntries(const QString &cfgText)
{
    ImportEntries out;
    if (cfgText.isEmpty()) return out;

    auto stripQuotes = [](QString v) {
        v = v.trimmed();
        if (v.size() >= 2 && v.startsWith('"') && v.endsWith('"'))
            v = v.mid(1, v.size() - 2);
        return v;
    };

    const QStringList lines = cfgText.split('\n');
    for (const QString &raw : lines) {
        QString line = raw;
        // Tolerate Windows endings + trailing whitespace from hand-edits.
        if (line.endsWith('\r')) line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;

        // Match on the trimmed form but keep the value verbatim (only stripping
        // quotes) so trailing whitespace inside a path survives.
        if (trimmed.startsWith(QStringLiteral("data="))) {
            out.dataPaths << stripQuotes(trimmed.mid(5));
        } else if (trimmed.startsWith(QStringLiteral("content="))) {
            out.contentFiles << stripQuotes(trimmed.mid(8));
        } else if (trimmed.startsWith(QStringLiteral("groundcover="))) {
            out.groundcoverFiles << stripQuotes(trimmed.mid(12));
        }
    }
    return out;
}

QStringList externalDataPaths(const QString &cfgText)
{
    QStringList out;
    bool inManaged = false;
    for (QString line : cfgText.split(QLatin1Char('\n'))) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        if (line == kManagedBegin)        { inManaged = true;  continue; }
        if (line.startsWith(kManagedEnd)) { inManaged = false; continue; }
        if (inManaged) continue;
        if (!line.startsWith(QStringLiteral("data="))) continue;
        QString path = line.mid(5);
        if (path.size() >= 2 && path.startsWith(QLatin1Char('"'))
                             && path.endsWith(QLatin1Char('"')))
            path = path.mid(1, path.size() - 2);
        out.append(path);
    }
    return out;
}

bool looksLikeVanillaDataFolder(const QString &dirPath)
{
    if (dirPath.isEmpty()) return false;
    QDir d(dirPath);
    if (!d.exists()) return false;
    // Conservative: require Morrowind.esm AND at least one official expansion.
    // A modlist might carry Morrowind.esm in some standalone folder (ESM
    // replacer) but won't ship Tribunal/Bloodmoon masters alongside it; without
    // the AND we'd risk skipping a mod the user actually wants.
    const bool hasMorrowind = QFileInfo(d.filePath("Morrowind.esm")).exists();
    if (!hasMorrowind) return false;
    const bool hasExpansion = QFileInfo(d.filePath("Tribunal.esm")).exists()
                           || QFileInfo(d.filePath("Bloodmoon.esm")).exists();
    return hasExpansion;
}

} // namespace openmw
