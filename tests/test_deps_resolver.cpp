// tests/test_deps_resolver.cpp
//
// Round-trips the three pure decision helpers extracted from MainWindow:
//   · deps::resolveDependencies    - drives HasMissing / MissingLabels
//                                     / HasInListDep (yellow ! + ↳ indent)
//   · deps::autoLinkSameModpage    - decides DependsOn mutations at install
//   · deps::parseDescriptionDeps   - regex over Nexus mod descriptions
//
// Anchored against the Interface-Reimagined-for-OpenMW patch workflow the
// user manually reproduced in the last session: without these assertions,
// a silent regression in same-URL self-skip or MAIN/UPDATE semantics
// would just quietly stop the ↳ indent from appearing and nobody would
// notice for weeks.
//
// Build + run:
//   ./build/tests/test_deps_resolver

#include "deps_resolver.h"

#include <QCoreApplication>

#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok, const QString &detail = {})
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name << "\n";
        if (!detail.isEmpty())
            std::cout << "    " << detail.toStdString() << "\n";
        ++s_failed;
    }
}

using deps::ModEntry;

// -- Fixture helpers ---

static ModEntry mk(int idx, const QString &name, const QString &url,
                    bool enabled = true, bool installed = true,
                    QStringList deps = {})
{
    ModEntry m;
    m.idx         = idx;
    m.nexusUrl    = url;
    m.displayName = name;
    m.enabled     = enabled;
    m.installed   = installed;
    m.dependsOn   = deps;
    return m;
}

static const QString URL_BASE  =
    "https://www.nexusmods.com/morrowind/mods/54985";  // Interface Reimagined
static const QString URL_OTHER =
    "https://www.nexusmods.com/morrowind/mods/12345";

// -- resolveDependencies tests ---

static void testResolveEmptyDepsIsSilent()
{
    std::cout << "testResolveEmptyDepsIsSilent\n";
    const QList<ModEntry> all = { mk(0, "Mod A", URL_BASE) };
    auto r = deps::resolveDependencies(all[0], all);
    check("no DependsOn → hasMissing=false",  !r.hasMissing);
    check("no DependsOn → hasInListDep=false", !r.hasInListDep);
    check("missingLabels empty",              r.missingLabels.isEmpty());
}

static void testResolveDisabledTargetNeverFlagged()
{
    std::cout << "testResolveDisabledTargetNeverFlagged\n";
    // Target is disabled; its DependsOn points at a URL NOT in the list.
    // Missing-dep icon must stay off because disabled mods aren't a
    // crash risk until the user turns them on.
    const QList<ModEntry> all = {
        mk(0, "Patch", URL_BASE, /*enabled=*/false, /*installed=*/true,
           { URL_OTHER }),
    };
    auto r = deps::resolveDependencies(all[0], all);
    check("disabled target → hasMissing=false", !r.hasMissing);
    check("disabled target → missingLabels empty",
          r.missingLabels.isEmpty());
}

static void testResolveDepNotInList()
{
    std::cout << "testResolveDepNotInList\n";
    const QList<ModEntry> all = {
        mk(0, "Patch", URL_BASE, true, true, { URL_OTHER }),
    };
    auto r = deps::resolveDependencies(all[0], all);
    check("missing URL flagged", r.hasMissing);
    check("label says 'not in modlist'",
          r.missingLabels.size() == 1
          && r.missingLabels.first().endsWith(" - not in modlist"));
}

static void testResolveDepSatisfied()
{
    std::cout << "testResolveDepSatisfied\n";
    const QList<ModEntry> all = {
        mk(0, "Base",  URL_OTHER, true, true),
        mk(1, "Patch", URL_BASE,  true, true, { URL_OTHER }),
    };
    auto r = deps::resolveDependencies(all[1], all);
    check("installed+enabled sibling satisfies dep", !r.hasMissing);
    check("hasInListDep true (parent present)",       r.hasInListDep);
}

static void testResolveDepDisabled()
{
    std::cout << "testResolveDepDisabled\n";
    const QList<ModEntry> all = {
        mk(0, "Base",  URL_OTHER, /*enabled=*/false, true),
        mk(1, "Patch", URL_BASE,  true, true, { URL_OTHER }),
    };
    auto r = deps::resolveDependencies(all[1], all);
    check("disabled sibling → hasMissing", r.hasMissing);
    check("label mentions 'disabled'",
          r.missingLabels.size() == 1
          && r.missingLabels.first() == "Base - disabled");
    check("hasInListDep still true",       r.hasInListDep);
}

static void testResolveDepNotInstalled()
{
    std::cout << "testResolveDepNotInstalled\n";
    const QList<ModEntry> all = {
        mk(0, "Base",  URL_OTHER, true, /*installed=*/false),
        mk(1, "Patch", URL_BASE,  true, true, { URL_OTHER }),
    };
    auto r = deps::resolveDependencies(all[1], all);
    check("not-installed sibling → hasMissing", r.hasMissing);
    check("label mentions 'not installed'",
          r.missingLabels.size() == 1
          && r.missingLabels.first() == "Base - not installed");
}

static void testResolveMultipleCandidatesAnySatisfies()
{
    std::cout << "testResolveMultipleCandidatesAnySatisfies\n";
    // Two rows share URL_OTHER - one disabled, one installed+enabled.
    // The enabled one satisfies the dep and the dep must NOT appear in
    // missingLabels.
    const QList<ModEntry> all = {
        mk(0, "Base v1 (old)", URL_OTHER, /*enabled=*/false, true),
        mk(1, "Base v1 (new)", URL_OTHER, true, true),
        mk(2, "Patch",         URL_BASE,  true, true, { URL_OTHER }),
    };
    auto r = deps::resolveDependencies(all[2], all);
    check("one satisfied sibling is enough", !r.hasMissing);
}

static void testResolveSelfUrlSkipped()
{
    std::cout << "testResolveSelfUrlSkipped\n";
    // Auto-linked patch case: the patch's own NexusUrl IS the URL in its
    // DependsOn (they share a modpage).  The resolver must skip self -
    // otherwise it'd falsely report the dep satisfied even when the base
    // isn't in the list.
    const QList<ModEntry> soloPatch = {
        mk(0, "Patch", URL_BASE, true, true, { URL_BASE }),
    };
    auto r1 = deps::resolveDependencies(soloPatch[0], soloPatch);
    check("self-URL alone → missing (not self-satisfied)", r1.hasMissing);
    check("self-URL alone → hasInListDep=false",           !r1.hasInListDep);

    // Once a real sibling exists with the same URL, the dep resolves.
    const QList<ModEntry> withBase = {
        mk(0, "Base",  URL_BASE, true, true),
        mk(1, "Patch", URL_BASE, true, true, { URL_BASE }),
    };
    auto r2 = deps::resolveDependencies(withBase[1], withBase);
    check("self-URL + real sibling → satisfied", !r2.hasMissing);
    check("self-URL + real sibling → hasInListDep=true", r2.hasInListDep);
}

// -- autoLinkSameModpage tests ---

static void testAutoLinkNoSiblings()
{
    std::cout << "testAutoLinkNoSiblings\n";
    const QList<ModEntry> all = { mk(0, "Solo", URL_BASE) };
    auto acts = deps::autoLinkSameModpage(all[0], all, "PATCH");
    check("no same-URL siblings → empty actions", acts.isEmpty());
}

static void testAutoLinkMainAdoptsSibling()
{
    std::cout << "testAutoLinkMainAdoptsSibling\n";
    // Scenario: user installed the PATCH first (row 0), now installs
    // MAIN (row 1).  The MAIN install should mutate the existing sibling
    // so row 0 (the patch) gains URL_BASE in its DependsOn.
    const QList<ModEntry> all = {
        mk(0, "Patch",         URL_BASE),
        mk(1, "Base (new)",    URL_BASE),
    };
    auto acts = deps::autoLinkSameModpage(all[1], all, "MAIN");
    check("exactly one action", acts.size() == 1);
    check("action targets the pre-existing sibling", acts.first().targetIdx == 0);
    check("action appends the shared URL",
          acts.first().urlToAppend == URL_BASE);
}

static void testAutoLinkUpdateTreatedAsBase()
{
    std::cout << "testAutoLinkUpdateTreatedAsBase\n";
    const QList<ModEntry> all = {
        mk(0, "Patch",     URL_BASE),
        mk(1, "Base 1.1",  URL_BASE),
    };
    auto acts = deps::autoLinkSameModpage(all[1], all, "UPDATE");
    check("UPDATE behaves like MAIN", acts.size() == 1
                                     && acts.first().targetIdx == 0);
}

static void testAutoLinkPatchInstallDependsOnBase()
{
    std::cout << "testAutoLinkPatchInstallDependsOnBase\n";
    const QList<ModEntry> all = {
        mk(0, "Base",  URL_BASE),
        mk(1, "Patch", URL_BASE),  // just installed
    };
    auto acts = deps::autoLinkSameModpage(all[1], all, "PATCH");
    check("exactly one action", acts.size() == 1);
    check("action targets the new (dependent) entry",
          acts.first().targetIdx == 1);
    check("URL appended is the shared page URL",
          acts.first().urlToAppend == URL_BASE);
}

static void testAutoLinkUnknownCategoryIsDependent()
{
    std::cout << "testAutoLinkUnknownCategoryIsDependent\n";
    const QList<ModEntry> all = {
        mk(0, "Base",     URL_BASE),
        mk(1, "Whatever", URL_BASE),
    };
    // OPTIONAL, MISCELLANEOUS, OLD and unrecognised strings all fall
    // through to the dependent branch.
    for (const QString &cat : {"OPTIONAL", "MISCELLANEOUS", "OLD", "", "xyzzy"}) {
        auto acts = deps::autoLinkSameModpage(all[1], all, cat);
        check(("category '" + cat.toStdString() + "' → dependent").c_str(),
              acts.size() == 1 && acts.first().targetIdx == 1);
    }
}

static void testAutoLinkMainWithManySiblings()
{
    std::cout << "testAutoLinkMainWithManySiblings\n";
    const QList<ModEntry> all = {
        mk(0, "Patch A",    URL_BASE),
        mk(1, "Optional B", URL_BASE),
        mk(2, "Patch C",    URL_BASE),
        mk(3, "Base (new)", URL_BASE),
    };
    auto acts = deps::autoLinkSameModpage(all[3], all, "MAIN");
    check("three actions (one per existing sibling)", acts.size() == 3);
    QList<int> targets;
    for (const auto &a : acts) targets << a.targetIdx;
    std::sort(targets.begin(), targets.end());
    check("actions target rows 0, 1, 2",
          targets == QList<int>({0, 1, 2}));
}

static void testAutoLinkIgnoresOtherUrls()
{
    std::cout << "testAutoLinkIgnoresOtherUrls\n";
    const QList<ModEntry> all = {
        mk(0, "Other mod", URL_OTHER),
        mk(1, "Base",      URL_BASE),
        mk(2, "Patch",     URL_BASE),  // just installed
    };
    auto acts = deps::autoLinkSameModpage(all[2], all, "PATCH");
    check("unrelated URL rows don't receive actions",
          acts.size() == 1 && acts.first().targetIdx == 2);
}

static void testAutoLinkEmptyUrlNoop()
{
    std::cout << "testAutoLinkEmptyUrlNoop\n";
    // Mods added via "Add Mod Folder…" without a Nexus URL should not
    // trigger spurious auto-link actions just because two of them have
    // empty URLs.
    const QList<ModEntry> all = {
        mk(0, "Local A", ""),
        mk(1, "Local B", ""),
    };
    auto acts = deps::autoLinkSameModpage(all[1], all, "PATCH");
    check("empty NexusUrl → no actions", acts.isEmpty());
}

// -- parseDescriptionDeps tests ---

static void testParseEmptyDescription()
{
    std::cout << "testParseEmptyDescription\n";
    auto r = deps::parseDescriptionDeps("", "morrowind", 42, {});
    check("empty desc → no hits",
          r.presentUrls.isEmpty() && r.missingModIds.isEmpty());
}

static void testParseClassifiesHits()
{
    std::cout << "testParseClassifiesHits\n";
    const QString desc =
        "Requires https://www.nexusmods.com/morrowind/mods/111 and "
        "https://www.nexusmods.com/morrowind/mods/222";
    QMap<int, QString> idToUrl;
    idToUrl.insert(111, "https://www.nexusmods.com/morrowind/mods/111");
    // 222 is NOT installed.

    auto r = deps::parseDescriptionDeps(desc, "morrowind", /*self=*/999, idToUrl);
    check("111 bucketed as present",
          r.presentUrls.size() == 1
          && r.presentUrls.first().endsWith("/mods/111"));
    check("222 bucketed as missing",
          r.missingModIds.size() == 1 && r.missingModIds.first() == 222);
}

static void testParseSelfReferenceExcluded()
{
    std::cout << "testParseSelfReferenceExcluded\n";
    const QString desc =
        "See https://www.nexusmods.com/morrowind/mods/77 (the mod you're "
        "viewing) for background.";
    auto r = deps::parseDescriptionDeps(desc, "morrowind", /*self=*/77, {});
    check("self URL not reported",
          r.presentUrls.isEmpty() && r.missingModIds.isEmpty());
}

static void testParseDuplicatesDeduped()
{
    std::cout << "testParseDuplicatesDeduped\n";
    const QString desc =
        "a https://www.nexusmods.com/morrowind/mods/5 b "
        "https://www.nexusmods.com/morrowind/mods/5 c "
        "https://www.nexusmods.com/morrowind/mods/5";
    auto r = deps::parseDescriptionDeps(desc, "morrowind", 0, {});
    check("repeated URL collapsed to one",
          r.missingModIds.size() == 1 && r.missingModIds.first() == 5);
}

static void testParseDifferentGameIgnored()
{
    std::cout << "testParseDifferentGameIgnored\n";
    const QString desc =
        "Looks like https://www.nexusmods.com/skyrim/mods/333 but we're "
        "on Morrowind.";
    auto r = deps::parseDescriptionDeps(desc, "morrowind", 0, {});
    check("skyrim URL ignored",
          r.presentUrls.isEmpty() && r.missingModIds.isEmpty());
}

static void testParseOrderFirstSeen()
{
    std::cout << "testParseOrderFirstSeen\n";
    const QString desc =
        "Hard-requires https://www.nexusmods.com/morrowind/mods/333 "
        "and also https://www.nexusmods.com/morrowind/mods/111 "
        "and also https://www.nexusmods.com/morrowind/mods/222.";
    auto r = deps::parseDescriptionDeps(desc, "morrowind", 0, {});
    // All three missing; order of appearance must be 333, 111, 222.
    check("first-seen order preserved",
          r.missingModIds == QList<int>({333, 111, 222}));
}

// -- computeSelectionHighlights tests ---

using deps::Highlight;

// Stargazer-selects-Skill-Framework regression: selecting a content mod
// whose DependsOn lists a library URL must paint the library green.
// The bug that prompted this test was a data issue (Stargazer.DependsOn
// empty after a restore), but the test anchors the wiring so any future
// change that silently drops the Dep match gets caught.
static void testHighlightSelectedDepGetsGreen()
{
    std::cout << "testHighlightSelectedDepGetsGreen\n";
    const QString URL_STARGAZER = "https://www.nexusmods.com/morrowind/mods/58605";
    const QString URL_SKILL_FW  = "https://www.nexusmods.com/morrowind/mods/57765";
    const QList<ModEntry> all = {
        mk(0, "Skill Framework", URL_SKILL_FW),
        mk(1, "Stargazer",        URL_STARGAZER, true, true, {URL_SKILL_FW}),
        mk(2, "Unrelated Mod",    URL_OTHER),
    };
    const auto hl = deps::computeSelectionHighlights(all, /*selected=*/1);
    check("self row unhighlighted",   hl[1] == Highlight::None);
    check("skill framework = Dep",    hl[0] == Highlight::Dep);
    check("unrelated = None",         hl[2] == Highlight::None);
}

// Reverse direction: selecting a library should tint mods that depend on it.
// Because Skill Framework is flagged as a utility, those users should
// appear in green (Dep) rather than the default blue (User).
static void testHighlightUtilityFlipsUserToDep()
{
    std::cout << "testHighlightUtilityFlipsUserToDep\n";
    const QString URL_SKILL_FW = "https://www.nexusmods.com/morrowind/mods/57765";
    const QString URL_USER1    = "https://www.nexusmods.com/morrowind/mods/58605";
    const QString URL_USER2    = "https://www.nexusmods.com/morrowind/mods/58606";

    ModEntry skill = mk(0, "Skill Framework", URL_SKILL_FW);
    skill.isUtility = true;

    const QList<ModEntry> all = {
        skill,
        mk(1, "Stargazer", URL_USER1, true, true, {URL_SKILL_FW}),
        mk(2, "Other Consumer", URL_USER2, true, true, {URL_SKILL_FW}),
        mk(3, "Unrelated Mod",  URL_OTHER),
    };
    const auto hl = deps::computeSelectionHighlights(all, /*selected=*/0);
    check("users of utility get Dep (green), not User (blue)",
          hl[1] == Highlight::Dep && hl[2] == Highlight::Dep);
    check("unrelated stays None", hl[3] == Highlight::None);
}

// Non-utility selection: mods that depend on the selected get the blue
// (User) highlight - the default direction for content mods.
static void testHighlightNonUtilitySelectionUsesBlue()
{
    std::cout << "testHighlightNonUtilitySelectionUsesBlue\n";
    const QString URL_BASE  = "https://www.nexusmods.com/morrowind/mods/100";
    const QString URL_PATCH = "https://www.nexusmods.com/morrowind/mods/101";

    const QList<ModEntry> all = {
        mk(0, "Base Mod",  URL_BASE),
        mk(1, "Patch Mod", URL_PATCH, true, true, {URL_BASE}),
    };
    const auto hl = deps::computeSelectionHighlights(all, /*selected=*/0);
    check("patch shows up as User when base is selected",
          hl[1] == Highlight::User);
}

// Empty NexusUrl on the selected row → no Dep matches possible (candidates
// can't appear in an empty DependsOn set) and no User matches possible (no
// URL for candidates to reference).  Must produce all-None, no crash.
static void testHighlightEmptySelectedUrlAllNone()
{
    std::cout << "testHighlightEmptySelectedUrlAllNone\n";
    const QList<ModEntry> all = {
        mk(0, "Unlinked Local Mod", QString()),
        mk(1, "Some Mod", "https://www.nexusmods.com/morrowind/mods/42",
           true, true, {"https://www.nexusmods.com/morrowind/mods/999"}),
    };
    const auto hl = deps::computeSelectionHighlights(all, /*selected=*/0);
    check("all entries None for unlinked selection",
          hl[0] == Highlight::None && hl[1] == Highlight::None);
}

// Candidate rows with an empty NexusUrl can never satisfy a Dep match even
// if the selected mod has them listed - because DependsOn entries ARE URLs,
// empty URL = nothing to match against.  Guards against regressions where
// someone changes the match to "contains name" or similar.
static void testHighlightEmptyCandidateUrlCantBeDep()
{
    std::cout << "testHighlightEmptyCandidateUrlCantBeDep\n";
    const QString URL_REAL = "https://www.nexusmods.com/morrowind/mods/10";
    const QList<ModEntry> all = {
        mk(0, "Unlinked",  QString()),
        mk(1, "Selected",  URL_REAL, true, true, {QString()}), // pathological empty dep
    };
    const auto hl = deps::computeSelectionHighlights(all, /*selected=*/1);
    check("empty-URL candidate not matched as Dep",
          hl[0] == Highlight::None);
}

// selectedIdx out of range (nothing selected, or stale index after a
// row was removed) → all-None, no crash.
static void testHighlightOutOfRangeSelectionIsSafe()
{
    std::cout << "testHighlightOutOfRangeSelectionIsSafe\n";
    const QList<ModEntry> all = {
        mk(0, "A", "https://www.nexusmods.com/morrowind/mods/1"),
        mk(1, "B", "https://www.nexusmods.com/morrowind/mods/2"),
    };
    const auto neg   = deps::computeSelectionHighlights(all, -1);
    const auto over  = deps::computeSelectionHighlights(all, 99);
    const auto empty = deps::computeSelectionHighlights({}, 0);
    check("negative idx → all None", neg.size()  == 2 && neg[0]  == Highlight::None && neg[1]  == Highlight::None);
    check("overflow idx → all None", over.size() == 2 && over[0] == Highlight::None && over[1] == Highlight::None);
    check("empty list → empty result", empty.isEmpty());
}

// Output list length must equal input length - the MainWindow wiring
// indexes into m_modList by row, so off-by-one here would silently
// mis-highlight or skip a row.
static void testHighlightOutputLengthMatches()
{
    std::cout << "testHighlightOutputLengthMatches\n";
    QList<ModEntry> all;
    for (int i = 0; i < 17; ++i)
        all.append(mk(i, QString("Mod %1").arg(i),
                      QString("https://www.nexusmods.com/morrowind/mods/%1").arg(i)));
    const auto hl = deps::computeSelectionHighlights(all, 5);
    check("output size == input size", hl.size() == all.size());
}

// -- Entry point ---

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testResolveEmptyDepsIsSilent();
    testResolveDisabledTargetNeverFlagged();
    testResolveDepNotInList();
    testResolveDepSatisfied();
    testResolveDepDisabled();
    testResolveDepNotInstalled();
    testResolveMultipleCandidatesAnySatisfies();
    testResolveSelfUrlSkipped();

    testAutoLinkNoSiblings();
    testAutoLinkMainAdoptsSibling();
    testAutoLinkUpdateTreatedAsBase();
    testAutoLinkPatchInstallDependsOnBase();
    testAutoLinkUnknownCategoryIsDependent();
    testAutoLinkMainWithManySiblings();
    testAutoLinkIgnoresOtherUrls();
    testAutoLinkEmptyUrlNoop();

    testParseEmptyDescription();
    testParseClassifiesHits();
    testParseSelfReferenceExcluded();
    testParseDuplicatesDeduped();
    testParseDifferentGameIgnored();
    testParseOrderFirstSeen();

    testHighlightSelectedDepGetsGreen();
    testHighlightUtilityFlipsUserToDep();
    testHighlightNonUtilitySelectionUsesBlue();
    testHighlightEmptySelectedUrlAllNone();
    testHighlightEmptyCandidateUrlCantBeDep();
    testHighlightOutOfRangeSelectionIsSafe();
    testHighlightOutputLengthMatches();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
