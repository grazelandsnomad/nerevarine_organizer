#include "openmwconfigwriter.h"

#include "master_satisfaction.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStringView>

#include <algorithm>

namespace openmw {

SyncPrepareResult prepareForSync(const SyncPrepareInputs &in)
{
    static const QString kBegin = "# --- Nerevarine Organizer BEGIN ---";
    static const QString kEnd   = "# --- Nerevarine Organizer END ---";
    static const QStringList contentExts{".esp", ".esm", ".omwaddon", ".omwscripts"};

    SyncPrepareResult out;
    out.mods = in.mods;     // copy; we mutate suppressedPlugins / groundcoverFiles below

    QString existing = in.existingCfg;
    const QString modsRootForRescue = in.modsRoot.isEmpty()
        ? QString() : QDir::cleanPath(in.modsRoot);

    // -- Orphan-managed rescue ---
    //
    // When the modlist is replaced (Import) or a mod is removed without
    // deleting its folder from disk, openmw.cfg can still carry data=
    // lines inside the managed section that no current managed mod
    // claims. The orphan scrub below would drop those lines and the
    // content= scrub would drop the corresponding plugins, wiping the
    // OpenMW Launcher's Content List of mods whose files are still
    // physically present.
    //
    // Rescue any orphan-managed path whose folder still has plugins on
    // disk by re-emitting it OUTSIDE the managed section.
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

    // -- Launcher-only externals augmentation ---
    //
    // OpenMW Launcher writes the user's selected data= paths and content=
    // entries into BOTH openmw.cfg and launcher.cfg, but only when the user
    // actually opens the launcher and clicks Save/Play.  A user who set up
    // their game with `openmw-launcher` and then never re-opened it can
    // legitimately end up with vanilla "data=<Morrowind Data Files>" plus
    // "content=Morrowind.esm" present in launcher.cfg but absent from the
    // local openmw.cfg.  Treat that as legitimate external state and
    // synthesise it as if it had been written outside the BEGIN/END markers.
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

        // Filenames any managed mod knows about (regardless of enabled
        // state).  Disabled-mod plugins still belong to Nerevarine and
        // must NOT be carried back as fake externals - that would make
        // disabling a mod inside Nerevarine fail to remove its content=
        // line from the launcher's Selected list.
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

    // -- Orphan-plugin scrub: build providedPlugins set ---
    //
    // Drop `content=` lines that point at plugins no data= directory
    // provides.  OpenMW aborts at launch with:
    //     Fatal error: Failed loading X.esp: the content file does not exist
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

    // -- Master satisfaction ---
    //
    // content= plugins: OpenMW aborts at launch with "File X asks for
    // parent file Y, but it is not available or has been loaded in the
    // wrong order".  groundcover= plugins: same crash, no graceful
    // fallback.  Detection lives in openmw::findUnsatisfiedMasters.
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

    // -- Scrub: build scrubbedExisting + droppedOrphans count ---
    //
    // groundcover= lines for plugins not in providedPlugins drop too;
    // groundcover plugins must not appear in the load order.
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

    // -- Effective load order ---
    //
    // Filter out plugins not in providedPlugins, plugins in the
    // groundcover= section (loaded via the separate mechanism), and
    // suppressed plugins (missing masters).  Keeping any of these in
    // the load order means the next absorb cycle resurrects them and
    // the user keeps hitting the same crash.
    out.effectiveLoadOrder.reserve(in.loadOrder.size());
    for (const QString &cf : in.loadOrder)
        if (providedPlugins.contains(cf)
         && !groundcoverPlugins.contains(cf)
         && !suppressedFromLoadOrder.contains(cf))
            out.effectiveLoadOrder << cf;

    return out;
}

namespace {

// .esm first, then other extensions; within each group, case-insensitive
// alphabetical.  Deterministic so reruns produce identical cfg bytes.
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
    static const QString BEGIN = "# --- Nerevarine Organizer BEGIN ---";
    static const QString END   = "# --- Nerevarine Organizer END ---";

    // --- Pass 1: build lists from the mod list ---
    //
    // For each mod we emit EVERY subdirectory that contains a plugin as a
    // data= path, and collect its filenames in MOD LIST ORDER.  OpenMW loads
    // content= entries in the order they appear, so reordering mods in the
    // organizer must reorder their content= lines too.
    //
    // availableContentOrdered  content files from ENABLED+INSTALLED mods,
    //                          in modlist order, deduplicated.
    // allManagedContent        every content file seen in ANY installed mod
    //                          (enabled or not) - used in Pass 3 to tell
    //                          "ours" from base-game / external plugins.
    QStringList   dataLines;
    QStringList   availableContentOrdered;
    QSet<QString> availableSeen;
    QSet<QString> allManagedContent;
    QStringList   groundcoverOrdered;
    QSet<QString> groundcoverSeen;
    QSet<QString> allManagedGroundcover;
    QSet<QString> allSuppressed;
    // Mod BSAs in modlist order, deduped.  Emitted inside the managed
    // section as `fallback-archive=<basename>` so OpenMW can resolve the
    // archive's contents when looking up meshes/textures referenced by
    // the mod's plugins.  allManagedArchives carries every BSA name we'll
    // emit so Pass 2 can drop pre-existing `fallback-archive=` lines that
    // duplicate them (a renamed/uninstalled mod no longer registers).
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
            // Pure resource mod (main menu replacer, retexture, sound pack):
            // no plugins, but OpenMW still needs a data= entry pointing at the
            // folder that holds textures/, meshes/, splash/ and so on.
            for (const QString &r : m.resourceRoots)
                dataLines << "data=\"" + r + "\"";
        }

        // BSA archives.  Recorded for every installed mod (so Pass 2 can
        // drop stale managed entries even from disabled mods), but only
        // EMITTED for enabled mods - same pattern as content= above.
        for (const QString &b : m.bsaFiles) {
            allManagedArchives.insert(b);
            if (!m.enabled) continue;
            if (archiveSeen.contains(b)) continue;
            archiveSeen.insert(b);
            archiveOrdered << b;
        }
    }

    // --- Pass 2: parse existing cfg ---
    //
    //   preamble          non-data=, non-content= lines outside the managed
    //                     section (settings, fallback=, etc.)
    //   allPrevContent    ALL content= filenames from the ENTIRE file in
    //                     encounter order (captures managed + launcher writes
    //                     like Morrowind.esm, Tribunal.esm …)
    //   externalDataLines data= lines from OUTSIDE the managed section
    //                     (base-game install dir, etc.)
    //
    // data= lines inside the managed section are intentionally dropped - they
    // get rebuilt from the modlist above.
    QStringList   preamble;
    QStringList   allPrevContent;
    QSet<QString> prevContentSeen;
    QStringList   externalDataLines;

    if (!existingCfg.isEmpty()) {
        const QStringList lines = existingCfg.split('\n');
        bool inManaged = false;
        for (int i = 0; i < lines.size(); ++i) {
            QString line = lines.at(i);
            // Tolerate CRLF input by stripping a trailing \r.
            if (line.endsWith('\r')) line.chop(1);

            // QString::split on the final trailing "\n" yields an empty tail;
            // skip that so we don't emit a phantom blank line in the preamble.
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
                // Managed groundcover lines are rebuilt; external ones are
                // kept as-is via the preamble path below, but only if the
                // plugin isn't already being emitted as a managed entry.
                // OpenMW Launcher occasionally rewrites openmw.cfg and
                // promotes managed groundcover= lines into the preamble.
                // Emitting them twice makes OpenMW load the plugin twice,
                // shifting content-file indices out from under the savegame
                // (every grass ref resolves to marker_error meshes).
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
                // Drop any preamble fallback-archive= whose basename is now
                // emitted by Pass 4 - prevents a duplicate entry surviving
                // when an installed mod's BSA used to live in the preamble
                // (manually added by the user before Nerevarine managed it,
                // or migrated from a previous run).  Vanilla BSAs
                // (Morrowind/Tribunal/Bloodmoon) are NEVER in
                // allManagedArchives because no managed mod ships them, so
                // they survive untouched.
                const QString name = line.mid(QStringLiteral("fallback-archive=").size());
                if (!allManagedArchives.contains(name))
                    preamble << line;
            } else if (!inManaged) {
                preamble << line;
            }
            // Inside managed section: data= and fallback-archive= are
            // discarded (rebuilt above).
        }
    }

    // --- Pass 3: merge content order ---
    //
    // Rules:
    //   A. Previously-seen content file NOT in allManagedContent → base-game /
    //      external.  Keep at its prior position.
    //   B. Managed content whose owning mod is enabled+installed → emit in
    //      loadOrder order (authoritative), then fall back to modlist order
    //      for anything loadOrder hasn't seen yet.
    //   C. Managed content whose owning mod is now disabled → dropped.
    QStringList   contentOrder;
    QSet<QString> seen;

    // Phase A: external plugins, in prior encounter order.
    for (const QString &cf : allPrevContent) {
        if (allManagedContent.contains(cf))      continue;
        if (allManagedGroundcover.contains(cf))   continue;
        if (allSuppressed.contains(cf))           continue;
        if (seen.contains(cf))                    continue;
        contentOrder << cf;
        seen.insert(cf);
    }

    // Phase B: managed plugins in loadOrder order (authoritative).
    QSet<QString> availableSet(availableContentOrdered.begin(),
                                availableContentOrdered.end());
    for (const QString &cf : loadOrder) {
        if (!availableSet.contains(cf)) continue;
        if (seen.contains(cf))           continue;
        contentOrder << cf;
        seen.insert(cf);
    }
    // Phase B fallback: anything missing from loadOrder (shouldn't happen if
    // reconcileLoadOrder ran, but defensive) goes in modlist-derived order.
    for (const QString &cf : availableContentOrdered) {
        if (seen.contains(cf)) continue;
        contentOrder << cf;
        seen.insert(cf);
    }

    // --- Pass 4: assemble output ---
    //
    //   preamble              settings, fallbacks …
    //   externalDataLines     base-game data= paths (outside managed section
    //                         so the launcher still sees them where expected)
    //   BEGIN
    //   dataLines             managed mod data= paths
    //   content= …            ALL content entries (base-game + managed) in
    //                         the order determined by Pass 3
    //   END
    QString out;
    for (const QString &line : preamble)          out += line + "\n";
    for (const QString &line : externalDataLines) out += line + "\n";
    out += BEGIN + "\n";
    for (const QString &dl  : dataLines)          out += dl   + "\n";
    // fallback-archive= goes BEFORE content= so the archive is registered
    // by the time OpenMW starts resolving plugin references.  Mod BSAs
    // come AFTER the vanilla ones in the preamble, so loose mod files in
    // data= dirs still win over BSA-packed copies (loose-file priority is
    // OpenMW's documented behaviour and what users expect).
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
    // Fresh install / launcher never run - leave it to whoever created
    // the file next to pick up from openmw.cfg the normal way.
    if (in.isEmpty()) return {};

    // launcher.cfg is Qt QSettings INI.  Writing it via QSettings would
    // lose comments, reorder keys, and mangle quoting - all of which we
    // saw corrupt 9k-line real-world files - so do a line-based rewrite
    // that only touches the current profile's data= / content= block.
    const bool crlf = in.contains(QStringLiteral("\r\n"));
    const QString nl = crlf ? QStringLiteral("\r\n") : QStringLiteral("\n");

    QStringList lines = in.split('\n');
    // Preserve trailing blank from the final "\n" only by not emitting one
    // for it later; for now normalise so comparisons work.
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
    if (profStart < 0) return {};       // no [Profiles] - nothing to do

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

    // Walk the [Profiles] section, dropping existing data=/content= for the
    // current profile and recording where the rebuilt block should land.
    // Insertion point = immediately after the last surviving <ts>/... line,
    // so the rewritten block stays contiguous with the profile's other keys
    // (fallback-archive=, etc.) and doesn't slice into a neighbouring profile.
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
    // No other <ts>/ line survived → park the block right after currentprofile=.
    // (If currentprofile= itself isn't there we already returned above.)
    if (insertAfter < 0) insertAfter = currentProfileLineIx;

    QStringList block;
    block.reserve(dataPaths.size() + contentFiles.size());
    // launcher.cfg convention: paths are stored UNQUOTED even when they
    // contain spaces - verified against a live file.  Quoting them would
    // make the launcher treat the quotes as literal path characters.
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

    // Same parsing shape as renderLauncherCfg - locate [Profiles],
    // find currentprofile=, enumerate <ts>/content= lines in encounter
    // order.  We don't care about data= or fallback-archive= here.
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

    // Same parsing shape as readLauncherCfgContentOrder - locate
    // [Profiles], find currentprofile=, enumerate <ts>/data= lines in
    // encounter order.  Paths are returned unquoted; the launcher writes
    // them unquoted on disk and quoting them would mangle paths with
    // spaces.
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
        // Tolerate quoted launcher.cfg in case a future OpenMW Launcher
        // version starts wrapping paths in double quotes.
        if (p.size() >= 2 && p.startsWith('"') && p.endsWith('"'))
            p = p.mid(1, p.size() - 2);
        out << p;
    }
    return out;
}

// -- Importer parser ----------------------------------------------------
//
// openmw.cfg is one entry per line, key=value, with `# `-prefix comments
// and the launcher's habit of quoting data= paths that contain spaces.
// We only care about three keys for import; everything else (`fallback=`,
// `script-blacklist=`, `replace=`, `resolution=`, etc.) is dropped.

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
        // openmw.cfg is line-based; tolerate Windows endings + trailing
        // whitespace from hand-edits.
        if (line.endsWith('\r')) line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;

        // Use the trimmed form for prefix matches but keep the original
        // value verbatim (apart from quote stripping) so we don't mangle
        // legitimate trailing whitespace inside paths.
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

bool looksLikeVanillaDataFolder(const QString &dirPath)
{
    if (dirPath.isEmpty()) return false;
    QDir d(dirPath);
    if (!d.exists()) return false;
    // Conservative test: the folder must contain Morrowind.esm AND at
    // least one of the official expansions.  A user's modlist may well
    // include Morrowind.esm in some standalone mod folder (an
    // ESM-replacer, e.g.) but it won't ship Tribunal/Bloodmoon master
    // files alongside it.  Without the AND-clause we'd risk skipping a
    // mod the user actually wants in the modlist.
    const bool hasMorrowind = QFileInfo(d.filePath("Morrowind.esm")).exists();
    if (!hasMorrowind) return false;
    const bool hasExpansion = QFileInfo(d.filePath("Tribunal.esm")).exists()
                           || QFileInfo(d.filePath("Bloodmoon.esm")).exists();
    return hasExpansion;
}

} // namespace openmw
