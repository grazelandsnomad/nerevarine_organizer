#include "deps_resolver.h"

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

} // namespace deps
