#include "openmwconfigwriter.h"

#include <QSet>
#include <QStringView>

#include <algorithm>

namespace openmw {

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

} // namespace openmw
