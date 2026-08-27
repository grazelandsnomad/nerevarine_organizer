#ifndef DEPS_RESOLVER_H
#define DEPS_RESOLVER_H

// Pure decision logic pulled out of MainWindow. POD in, POD out, so tests
// need no QTemporaryDir / Qt Widgets.
//
//   resolveDependencies   - per-mod scan: satisfied / missing / in-list-dep.
//   autoLinkSameModpage   - install-time DependsOn mutations when two files
//                           from one modpage land side by side.
//   parseDescriptionDeps  - regex a Nexus description; split same-game hits
//                           into installed vs missing.
//   buildGraph            - the whole DependsOn web as nodes + edges, for the
//                           dependency canvas.

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>

namespace deps {

// One modlist row, snapshotted from m_modList on the UI thread, passed by value.
struct ModEntry {
    int          idx         = -1;     // row position; "earliest sibling wins" needs it
    QString      nexusUrl;              // ModRole::NexusUrl (may be empty)
    QString      displayName;           // CustomName else text()
    bool         enabled     = false;
    bool         installed   = false;
    bool         isUtility   = false;  // ModRole::IsUtility (framework / library)
    QStringList  dependsOn;
    // Name of the separator this row sits under, "" for rows above the first
    // one. Only the canvas uses it, to scope what it draws to one section.
    QString      section;
};

// What to paint for one mod after resolving its DependsOn against the
// snapshot. hasMissing -> yellow ! icon, missingLabels -> its tooltip,
// hasInListDep -> the ↳ indent.
struct DepScanResult {
    bool         hasMissing   = false;
    QStringList  missingLabels;
    bool         hasInListDep = false;
};

// One mutation to apply to the modlist. Caller dedups via
// QStringList::contains (kept out of here so the logic stays test-friendly).
struct AutoLinkAction {
    int     targetIdx  = -1;
    QString urlToAppend;
};

// Result of regex-scanning a mod's description for dependency URLs.
struct DescriptionDeps {
    QStringList presentUrls;      // hits resolving to an installed mod
    QList<int>  missingModIds;    // hits that didn't; caller may prompt
};

// Resolve `target`'s DependsOn against the snapshot. hasInListDep ignores
// enabled state (the indent always means "belongs with parent"); hasMissing
// only fires for enabled targets. Skip self-URL candidates so a patch with
// DependsOn==NexusUrl can't satisfy itself. Any installed+enabled candidate
// satisfies a multi-candidate URL.
DepScanResult resolveDependencies(const ModEntry &target,
                                  const QList<ModEntry> &allMods);

// -- The dependency web, as a graph -----------------------------------
//
// Everything above answers a question about ONE mod. This answers what the
// whole set looks like, which is what a canvas needs.
//
// Two properties of the underlying data shape this, and both are easy to get
// wrong:
//
//   * A dependency is a Nexus mod-page URL, and that URL is NOT unique per
//     row. The MAIN, UPDATE and PATCH files of one mod page all carry the same
//     nexusUrl, so one dependency can legitimately resolve to SEVERAL nodes.
//     Resolving with a first-match lookup silently drops the rest.
//
//   * A row with no nexusUrl can never be the TARGET of a dependency - the
//     picker only offers rows that have one - though it can still declare
//     dependencies of its own. An editable canvas has to show that asymmetry
//     or the user will keep trying to draw an impossible arrow.

struct GraphNode {
    int     idx = -1;          // row index, or -1 for a ghost
    QString label;
    bool    installed   = false;
    bool    enabled     = false;
    bool    isUtility   = false;
    // False when the row has no nexusUrl, so nothing can ever depend on it.
    bool    canBeTarget = false;
    // A dependency URL that matches no row at all. The edge is kept and the
    // missing mod drawn, rather than the edge vanishing and the graph quietly
    // disagreeing with the modlist.
    bool    ghost = false;
    QString ghostUrl;
    QString modPath;           // layout identity; unique, unlike nexusUrl
    QString section;           // separator this row sits under; "" for ghosts
};

// from depends on to. Indices into Graph::nodes, not row indices.
struct GraphEdge { int from = -1; int to = -1; };

struct Graph {
    QList<GraphNode> nodes;
    QList<GraphEdge> edges;
};

// Build the graph from a modlist snapshot. Every mod row becomes a node so the
// canvas can offer any of them as an endpoint; edges come only from declared
// DependsOn. Self-edges are dropped, the way resolveDependencies already skips
// self-URL candidates.
Graph buildGraph(const QList<ModEntry> &allMods);

// Would adding from -> to create a cycle? An editable canvas must refuse one:
// "A needs B needs A" cannot be satisfied, and there is no sensible layer to
// draw it in either. True also for a self-edge.
bool wouldCycle(const Graph &g, int from, int to);

// Depth of each node: 0 for one that depends on nothing, otherwise one more
// than its deepest dependency. Index-parallel with Graph::nodes.
//
// Cycle-tolerant by construction. The user can hand-build a cycle that
// wouldCycle did not see (two edges added in separate sessions, or data edited
// by hand), and a naive longest-path walk would recurse until the stack died.
QList<int> layerOf(const Graph &g);

// Same-modpage DependsOn mutations for a newly-installing mod against the
// current list. `categoryHint` is the Nexus category_name from files.json.
// No actions if newMod.nexusUrl is empty or there are no siblings.
// MAIN/UPDATE: newMod is the base, one mutation per sibling. Otherwise
// newMod becomes a dependent of the shared URL.
QList<AutoLinkAction>
autoLinkSameModpage(const ModEntry &newMod,
                    const QList<ModEntry> &allMods,
                    const QString &categoryHint);

// Pull Nexus mod URLs for `game` out of a description, drop the self-ref
// and dupes, then split each hit into installed (`presentUrls`, via the
// caller's id-to-url map) or missing (`missingModIds`). Same behavior as
// MainWindow::checkModDependencies's inline scan.
DescriptionDeps
parseDescriptionDeps(const QString &description,
                     const QString &game,
                     int selfModId,
                     const QMap<int, QString> &installedIdToUrl);

// Selection-driven row highlight roles. The delegate paints them as a
// tinted row band + edge stripes to surface "dep of the selected mod" /
// "uses the selected mod".
enum class Highlight : int {
    None = 0,  // unrelated to the selected mod
    Dep  = 1,  // in the selected mod's DependsOn  (green)
    User = 2,  // its DependsOn contains the selected mod's URL (blue)
};

// One Highlight per row, parallel to allMods. Selected row is always None.
// Dep needs the candidate's NexusUrl in the selected row's DependsOn; User
// is the reverse. IsUtility flips User->Dep so the brighter colour marks
// "who depends on this library". Dep wins ties.
QList<Highlight>
computeSelectionHighlights(const QList<ModEntry> &allMods, int selectedIdx);

// -- Who breaks if I do this? -----------------------------------------
//
// The reverse of resolveDependencies. Asked before disabling, uninstalling or
// removing a mod, so the user finds out from the manager rather than from
// OpenMW.
//
// Phrased as a diff between two whole modlists rather than as "who names this
// URL", and that is the point. One Nexus URL maps to SEVERAL rows (the MAIN,
// UPDATE and PATCH files of one mod page all carry it), so disabling the PATCH
// row of a library breaks nothing while its MAIN row still stands. A URL scan
// names every dependent anyway, and a warning that cries wolf on a 26-dependent
// library is worse than no warning.

// A row that the proposed change would newly leave with an unmet dependency.
struct Breakage {
    int         idx = -1;      // row index, as in `before`
    QString     displayName;
    bool        enabled = false;
    QStringList nowMissing;    // resolveDependencies' own labels
};

// Rows satisfied in `before` and unsatisfied in `after`. Both lists must be
// index-parallel - same rows, same order; the caller builds `after` by copying
// `before` and applying the pending action to it. Mismatched sizes give {}.
//
// A row already broken BEFORE the change is not reported: the user is being
// asked about the change, not about the state of their list.
//
// Matching is resolveDependencies', i.e. raw URL equality, deliberately: this
// has to agree with the yellow ! icon and the launch warning the user is
// already looking at. buildGraph's normalisation is not used here for that
// reason.
//
// includeDisabled reports a dependent that is itself currently disabled.
// "Will break" normally means enabled-only, so disable/uninstall pass false;
// remove passes true, because a row being taken out of the list matters to a
// dependent whether or not it happens to be ticked right now.
QList<Breakage> findNewlyBroken(const QList<ModEntry> &before,
                                const QList<ModEntry> &after,
                                bool includeDisabled = false);

// findNewlyBroken to a fixed point: if disabling A breaks B, and the user
// takes the offer to disable B too, that can break C. `disable` applies the
// same action to each newly-found row. Round-capped, since a hand-built cycle
// in the data would otherwise spin - see layerOf for the same tolerance.
//
// Showing the first level and then acting on the closure would be worse than
// not offering the cascade at all, so the caller shows what this returns.
QList<Breakage> expandBreakageClosure(const QList<ModEntry> &before,
                                      QList<ModEntry> after,
                                      bool includeDisabled,
                                      const std::function<void(ModEntry &)> &disable);

// Rows whose declared DependsOn names any of `targetIdxs`, whether or not the
// dependency is currently satisfied. The "Required by" question, which is not
// the same as "who would break" - a dependent that is already broken still
// declared the dependency and still belongs in that list.
//
// Normalised through urlKey(), unlike findNewlyBroken: this one only ever
// shows names, so a wider net costs nothing.
QList<int> findDeclaredDependents(const QList<ModEntry> &allMods,
                                  const QList<int> &targetIdxs);

// A dependency URL reduced to what identifies the mod ("morrowind/49042"), so
// ".../mods/49042" and ".../mods/49042?tab=files" compare equal. Unparseable
// URLs fall back to their trimmed selves.
QString urlKey(const QString &url);

} // namespace deps

#endif // DEPS_RESOLVER_H
