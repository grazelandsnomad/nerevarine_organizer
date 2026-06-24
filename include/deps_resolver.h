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

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

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

} // namespace deps

#endif // DEPS_RESOLVER_H
