#ifndef DEPS_RESOLVER_H
#define DEPS_RESOLVER_H

// Pure decision logic extracted from MainWindow. Functions take POD snapshots
// and return POD results so the unit tests need no QTemporaryDir / Qt Widgets:
//
//   resolveDependencies   - per-mod scan: satisfied / missing / in-list-dep.
//   autoLinkSameModpage   - at install time, decides DependsOn mutations
//                           when two files from the same modpage land
//                           side by side.
//   parseDescriptionDeps  - regex over a Nexus description; splits
//                           same-game hits into installed vs missing.

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace deps {

// Snapshot of one modlist row, built from m_modList on the UI thread and
// passed by value.
struct ModEntry {
    int          idx         = -1;     // row position; matters for "earliest sibling wins"
    QString      nexusUrl;              // ModRole::NexusUrl (may be empty)
    QString      displayName;           // CustomName else text()
    bool         enabled     = false;
    bool         installed   = false;
    bool         isUtility   = false;  // ModRole::IsUtility (framework / library)
    QStringList  dependsOn;
};

// What the UI should paint for a single mod after resolving its DependsOn
// against the full snapshot.  hasMissing drives the yellow ! icon,
// missingLabels feeds its tooltip, hasInListDep drives the ↳ indent.
struct DepScanResult {
    bool         hasMissing   = false;
    QStringList  missingLabels;
    bool         hasInListDep = false;
};

// One mutation to apply to the modlist. The caller dedups via
// QStringList::contains (kept out of the pure logic for test clarity).
struct AutoLinkAction {
    int     targetIdx  = -1;
    QString urlToAppend;
};

// Result of regex-scanning a mod's description for dependency URLs.
struct DescriptionDeps {
    QStringList presentUrls;      // hits that resolved to an already-installed mod
    QList<int>  missingModIds;    // hits that didn't; caller may prompt the user
};

// Resolve `target`'s DependsOn against the snapshot. HasInListDep ignores
// enabled state (the indent always signals "belongs with parent");
// HasMissing only fires for enabled targets. Self-URL candidates are
// skipped so a patch with DependsOn==NexusUrl doesn't satisfy itself.
// Any installed+enabled candidate satisfies a multi-candidate URL.
DepScanResult resolveDependencies(const ModEntry &target,
                                  const QList<ModEntry> &allMods);

// Decide same-modpage DependsOn mutations given a newly-installing mod
// and the current list. `categoryHint` is the Nexus category_name from
// files.json. Empty newMod.nexusUrl or no siblings yields no actions.
// MAIN/UPDATE: newMod is the base; emit one mutation per sibling.
// Anything else: newMod becomes a dependent of the shared URL.
QList<AutoLinkAction>
autoLinkSameModpage(const ModEntry &newMod,
                    const QList<ModEntry> &allMods,
                    const QString &categoryHint);

// Extract Nexus mod URLs for `game` from a description blob, drop the
// self-reference and duplicates, and classify each remaining hit as
// either already-installed (output goes into `presentUrls`, using the
// caller's id-to-url map) or missing (`missingModIds`).
//
// Semantics match MainWindow::checkModDependencies's inline scan.
DescriptionDeps
parseDescriptionDeps(const QString &description,
                     const QString &game,
                     int selfModId,
                     const QMap<int, QString> &installedIdToUrl);

// Selection-driven row highlight roles.  Painted by the delegate as a
// tinted row band + edge stripes; drives the "this is a dep of the
// selected mod" / "this uses the selected mod" visual surfacing.
enum class Highlight : int {
    None = 0,  // not related to the selected mod
    Dep  = 1,  // item is listed in the selected mod's DependsOn  (green)
    User = 2,  // item's DependsOn contains the selected mod's URL (blue)
};

// One Highlight per row, parallel to allMods. The selected row is always
// None. Dep needs the candidate's NexusUrl present in the selected row's
// DependsOn; User is the reverse. IsUtility flips User->Dep so the
// brighter colour marks "who depends on this library". Dep wins ties.
QList<Highlight>
computeSelectionHighlights(const QList<ModEntry> &allMods, int selectedIdx);

} // namespace deps

#endif // DEPS_RESOLVER_H
