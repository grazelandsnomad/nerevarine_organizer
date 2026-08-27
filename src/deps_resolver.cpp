#include "deps_resolver.h"

#include "nxmurl.h"

#include <QHash>
#include <QSet>

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringView>

namespace deps {

QList<Highlight>
computeSelectionHighlights(const QList<ModEntry> &allMods, int selectedIdx)
{
    QList<Highlight> out(allMods.size(), Highlight::None);
    if (selectedIdx < 0 || selectedIdx >= allMods.size()) return out;

    const ModEntry &sel = allMods[selectedIdx];
    const QString &selUrl = sel.nexusUrl;
    const QSet<QString> depSet(sel.dependsOn.begin(), sel.dependsOn.end());

    // For a utility, flip User -> Dep so its users get the prominent green
    // ("who uses this library" is the useful direction). Content mods keep
    // the default 1=green-dep / 2=blue-user.
    const Highlight userRole = sel.isUtility ? Highlight::Dep : Highlight::User;

    for (int i = 0; i < allMods.size(); ++i) {
        if (i == selectedIdx) continue;
        const ModEntry &cand = allMods[i];

        // Dep: row is in the selected mod's DependsOn.
        if (!cand.nexusUrl.isEmpty() && depSet.contains(cand.nexusUrl)) {
            out[i] = Highlight::Dep;
            continue;
        }
        // User: row's DependsOn names the selected mod.
        if (!selUrl.isEmpty() && cand.dependsOn.contains(selUrl))
            out[i] = userRole;
    }
    return out;
}

DepScanResult resolveDependencies(const ModEntry &target,
                                  const QList<ModEntry> &allMods)
{
    DepScanResult out;

    // URL -> list of idx, so several same-URL rows are tolerated (an
    // auto-linked patch shares its base's modpage).
    QHash<QString, QList<int>> byUrl;
    for (const ModEntry &m : allMods)
        if (!m.nexusUrl.isEmpty())
            byUrl[m.nexusUrl].append(m.idx);

    // hasInListDep: any DependsOn URL resolves to a row other than target.
    for (const QString &url : target.dependsOn) {
        const auto idxs = byUrl.value(url);
        for (int i : idxs) {
            if (i != target.idx) { out.hasInListDep = true; break; }
        }
        if (out.hasInListDep) break;
    }

    // Missing-dep check only runs for enabled targets.
    if (!target.enabled || target.dependsOn.isEmpty())
        return out;

    for (const QString &url : target.dependsOn) {
        const auto idxs = byUrl.value(url);

        const ModEntry *firstNonSelf = nullptr;
        bool anySatisfied = false;
        for (int i : idxs) {
            if (i == target.idx) continue;
            // Linear probe; allMods is tiny and this runs once per row per scan.
            const ModEntry *cand = nullptr;
            for (const ModEntry &m : allMods) {
                if (m.idx == i) { cand = &m; break; }
            }
            if (!cand) continue;
            if (!firstNonSelf) firstNonSelf = cand;
            if (cand->installed && cand->enabled) {
                anySatisfied = true;
                break;
            }
        }
        if (anySatisfied) continue;

        if (!firstNonSelf) {
            out.missingLabels << url + QStringLiteral(" - not in modlist");
            continue;
        }
        if (!firstNonSelf->installed)
            out.missingLabels << firstNonSelf->displayName
                                  + QStringLiteral(" - not installed");
        else
            out.missingLabels << firstNonSelf->displayName
                                  + QStringLiteral(" - disabled");
    }
    out.hasMissing = !out.missingLabels.isEmpty();
    return out;
}

QList<AutoLinkAction>
autoLinkSameModpage(const ModEntry &newMod,
                    const QList<ModEntry> &allMods,
                    const QString &categoryHint)
{
    QList<AutoLinkAction> actions;
    if (newMod.nexusUrl.isEmpty()) return actions;

    // Same-URL siblings by idx, skipping the new entry.
    QList<int> siblingIdxs;
    for (const ModEntry &m : allMods) {
        if (m.idx == newMod.idx) continue;
        if (m.nexusUrl == newMod.nexusUrl)
            siblingIdxs.append(m.idx);
    }
    if (siblingIdxs.isEmpty()) return actions;

    const QString cat = categoryHint.toUpper();
    const bool newIsBase = (cat == QStringLiteral("MAIN")
                          || cat == QStringLiteral("UPDATE"));

    if (newIsBase) {
        // Existing siblings become dependents of the new base.
        for (int i : siblingIdxs)
            actions.append({i, newMod.nexusUrl});
    } else {
        // New arrival depends on the existing sibling(s).
        actions.append({newMod.idx, newMod.nexusUrl});
    }
    return actions;
}

DescriptionDeps
parseDescriptionDeps(const QString &description,
                     const QString &game,
                     int selfModId,
                     const QMap<int, QString> &installedIdToUrl)
{
    DescriptionDeps out;

    const QRegularExpression re(
        QString(R"(https?://(?:www\.)?nexusmods\.com/%1/mods/(\d+))")
            .arg(QRegularExpression::escape(game)),
        QRegularExpression::CaseInsensitiveOption);

    QSet<int> seen;
    auto it = re.globalMatch(description);
    while (it.hasNext()) {
        const int id = it.next().captured(1).toInt();
        if (id <= 0 || id == selfModId) continue;
        if (seen.contains(id))         continue;
        seen.insert(id);

        auto hit = installedIdToUrl.constFind(id);
        if (hit != installedIdToUrl.constEnd())
            out.presentUrls << hit.value();
        else
            out.missingModIds.append(id);
    }
    return out;
}


// -- The dependency web, as a graph -----------------------------------

// A dependency URL reduced to what actually identifies the mod. Raw string
// equality would treat ".../mods/146873" and ".../mods/146873?tab=files" as
// different mods; the existing lookups do exactly that and miss the match.
// Unparseable URLs fall back to their trimmed selves so nothing is lost.
QString urlKey(const QString &url)
{
    const auto ref = parseNexusModUrl(url);
    if (ref) return ref->game + QLatin1Char('/') + QString::number(ref->modId);
    return url.trimmed();
}

Graph buildGraph(const QList<ModEntry> &allMods)
{
    Graph g;

    // Every row becomes a node, so the canvas can offer any mod as an endpoint
    // even when it has no edges yet.
    QHash<int, int> rowToNode;          // row idx -> node index
    QHash<QString, QList<int>> byKey;   // url key -> node indices (MULTI)
    for (const ModEntry &m : allMods) {
        GraphNode n;
        n.idx         = m.idx;
        n.label       = m.displayName;
        n.installed   = m.installed;
        n.enabled     = m.enabled;
        n.isUtility   = m.isUtility;
        n.canBeTarget = !m.nexusUrl.isEmpty();
        n.section     = m.section;
        rowToNode.insert(m.idx, int(g.nodes.size()));
        if (!m.nexusUrl.isEmpty())
            byKey[urlKey(m.nexusUrl)].append(int(g.nodes.size()));
        g.nodes.append(n);
    }

    // Ghosts are created on demand and shared, so two mods needing the same
    // absent dependency point at one node rather than two.
    QHash<QString, int> ghostByKey;

    for (const ModEntry &m : allMods) {
        const int from = rowToNode.value(m.idx, -1);
        if (from < 0) continue;

        for (const QString &dep : m.dependsOn) {
            if (dep.trimmed().isEmpty()) continue;
            const QString key = urlKey(dep);

            const QList<int> targets = byKey.value(key);
            if (!targets.isEmpty()) {
                // Every row sharing the modpage, not just the first: a mod
                // page's MAIN and PATCH rows are both legitimately the
                // dependency.
                for (int to : targets) {
                    if (to == from) continue;      // no self-edge
                    g.edges.append({from, to});
                }
                continue;
            }

            // Nothing in the list matches. Keep the edge and draw the absence.
            auto it = ghostByKey.constFind(key);
            int to;
            if (it != ghostByKey.constEnd()) {
                to = *it;
            } else {
                GraphNode gh;
                gh.label       = dep;
                gh.ghost       = true;
                gh.ghostUrl    = dep;
                gh.canBeTarget = true;   // it IS a target, just not installed
                to = int(g.nodes.size());
                g.nodes.append(gh);
                ghostByKey.insert(key, to);
            }
            g.edges.append({from, to});
        }
    }

    return g;
}

bool wouldCycle(const Graph &g, int from, int to)
{
    if (from < 0 || to < 0 || from >= g.nodes.size() || to >= g.nodes.size())
        return false;
    if (from == to) return true;    // a mod cannot need itself

    // Reachable-from-`to`: if `from` is already downstream of `to`, adding
    // from -> to closes a loop. Iterative, so an existing cycle in the data
    // cannot blow the stack.
    QHash<int, QList<int>> out;
    for (const GraphEdge &e : g.edges) out[e.from].append(e.to);

    QList<int> stack{to};
    QSet<int> seen;
    while (!stack.isEmpty()) {
        const int n = stack.takeLast();
        if (n == from) return true;
        if (seen.contains(n)) continue;
        seen.insert(n);
        for (int nxt : out.value(n)) stack.append(nxt);
    }
    return false;
}

QList<int> layerOf(const Graph &g)
{
    QHash<int, QList<int>> out;
    for (const GraphEdge &e : g.edges) out[e.from].append(e.to);

    QList<int> depth(g.nodes.size(), 0);

    // Longest path to a leaf, computed iteratively with an explicit colour
    // map. A cycle is broken by treating an in-progress node as depth 0 rather
    // than recursing into it - the layout still draws, which matters more than
    // a "correct" depth for data that is already contradictory.
    enum Colour : quint8 { White = 0, Grey, Black };
    QList<quint8> colour(g.nodes.size(), White);

    for (int root = 0; root < g.nodes.size(); ++root) {
        if (colour[root] != White) continue;
        QList<int> stack{root};
        while (!stack.isEmpty()) {
            const int n = stack.last();
            if (colour[n] == White) {
                colour[n] = Grey;
                for (int m : out.value(n))
                    if (colour[m] == White) stack.append(m);
                continue;
            }
            // Every child settled (or is part of a cycle): fold them in.
            stack.removeLast();
            if (colour[n] == Black) continue;
            int d = 0;
            for (int m : out.value(n))
                if (colour[m] == Black) d = qMax(d, 1 + depth[m]);
            depth[n]  = d;
            colour[n] = Black;
        }
    }
    return depth;
}

QList<Breakage> findNewlyBroken(const QList<ModEntry> &before,
                                const QList<ModEntry> &after,
                                bool includeDisabled)
{
    QList<Breakage> out;
    if (before.size() != after.size()) return out;

    for (int i = 0; i < before.size(); ++i) {
        const ModEntry &b = before[i];
        if (b.dependsOn.isEmpty()) continue;
        if (!includeDisabled && !b.enabled) continue;

        // resolveDependencies only reports for an enabled target, so evaluate
        // the candidate as if it were on. Whether the DEPENDENT is ticked is
        // the caller's filter (includeDisabled), not a reason to go blind to
        // what its dependency list says.
        ModEntry probeBefore = b;
        ModEntry probeAfter  = after[i];
        probeBefore.enabled = true;
        probeAfter.enabled  = true;

        const QStringList wasMissing =
            resolveDependencies(probeBefore, before).missingLabels;
        const QStringList nowMissing =
            resolveDependencies(probeAfter, after).missingLabels;
        if (nowMissing.size() <= wasMissing.size()) continue;

        // Report only what this change broke. A mod already missing dep A and
        // now missing A and B is newly broken, but the dialog should say B.
        QStringList added;
        for (const QString &m : nowMissing)
            if (!wasMissing.contains(m)) added << m;
        if (added.isEmpty()) continue;

        Breakage br;
        br.idx         = b.idx;
        br.displayName = b.displayName;
        br.enabled     = b.enabled;
        br.nowMissing  = added;
        out.append(br);
    }
    return out;
}

QList<Breakage> expandBreakageClosure(const QList<ModEntry> &before,
                                      QList<ModEntry> after,
                                      bool includeDisabled,
                                      const std::function<void(ModEntry &)> &disable)
{
    // Enough rounds for any real chain; a cycle in hand-entered data would
    // otherwise never settle. layerOf tolerates the same shape.
    constexpr int kMaxRounds = 10;

    QList<Breakage> all;
    QSet<int> seen;
    for (int round = 0; round < kMaxRounds; ++round) {
        const QList<Breakage> found = findNewlyBroken(before, after, includeDisabled);
        bool grew = false;
        for (const Breakage &br : found) {
            if (seen.contains(br.idx)) continue;
            seen.insert(br.idx);
            all.append(br);
            grew = true;
            if (disable) {
                for (ModEntry &m : after)
                    if (m.idx == br.idx) { disable(m); break; }
            }
        }
        if (!grew || !disable) break;
    }
    return all;
}

QList<int> findDeclaredDependents(const QList<ModEntry> &allMods,
                                  const QList<int> &targetIdxs)
{
    QList<int> out;
    if (targetIdxs.isEmpty()) return out;

    QSet<QString> targetKeys;
    QSet<int> targetSet;
    for (int idx : targetIdxs) {
        targetSet.insert(idx);
        for (const ModEntry &m : allMods)
            if (m.idx == idx && !m.nexusUrl.isEmpty())
                targetKeys.insert(urlKey(m.nexusUrl));
    }
    if (targetKeys.isEmpty()) return out;

    for (const ModEntry &m : allMods) {
        if (targetSet.contains(m.idx)) continue;   // never itself
        for (const QString &u : m.dependsOn) {
            if (targetKeys.contains(urlKey(u))) { out.append(m.idx); break; }
        }
    }
    return out;
}

} // namespace deps
