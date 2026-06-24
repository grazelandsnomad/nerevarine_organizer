#include "nexusclient.h"
#include "deps_resolver.h"

#include <QCoreApplication>
#include <QByteArray>

#include <algorithm>
#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok, const QString &got = QString())
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name;
        if (!got.isNull()) std::cout << "  - got: \"" << got.toStdString() << "\"";
        std::cout << "\n";
        ++s_failed;
    }
}

static void testModInfo()
{
    using K = NexusClient::NexusError::Kind;
    std::cout << "\nparseModInfo:\n";

    // /v1/games/{game}/mods/{modId}.json shape
    {
        QByteArray json = R"({
            "name": "Tamriel Rebuilt",
            "description": "A huge landmass mod. See [url=https://www.nexusmods.com/morrowind/mods/42]Patch[/url].",
            "updated_timestamp": 1700000000
        })";
        auto info = NexusClient::parseModInfo(json);
        check("parsed OK",             info.has_value());
        check("name extracted",        info && info->name == "Tamriel Rebuilt",
              info ? info->name : info.error().toString());
        check("description extracted", info && info->description.contains("huge landmass"));
        check("timestamp extracted",   info && info->updatedTimestamp == 1700000000);
    }

    // Nexus sometimes pads the name with whitespace
    {
        QByteArray json = R"({"name": "  Morrowind Rebirth  "})";
        auto info = NexusClient::parseModInfo(json);
        check("name trimmed", info && info->name == "Morrowind Rebirth",
              info ? info->name : info.error().toString());
    }

    // Missing optional fields default, they don't error.
    {
        auto info = NexusClient::parseModInfo("{}");
        check("empty object → parse succeeds", info.has_value());
        check("missing name → empty",          info && info->name.isEmpty());
        check("missing desc → empty",          info && info->description.isEmpty());
        check("missing timestamp → 0",         info && info->updatedTimestamp == 0);
    }

    // Garbage gives a structured error, not silent defaults.
    {
        auto info = NexusClient::parseModInfo("not json");
        check("malformed → InvalidJson",
              !info && info.error().kind == K::InvalidJson,
              info ? info->name : info.error().toString());
    }

    // Array where we wanted an object.
    {
        auto info = NexusClient::parseModInfo("[1,2,3]");
        check("array top-level → WrongShape",
              !info && info.error().kind == K::WrongShape,
              info ? info->name : info.error().toString());
    }
}

static void testFilesList()
{
    using K = NexusClient::NexusError::Kind;
    std::cout << "\nparseFilesList:\n";

    QByteArray json = R"({
        "files": [
            {
                "file_id": 1001,
                "name": "Main File",
                "version": "2.3",
                "category_name": "MAIN",
                "md5": "ABCDEF0123456789",
                "size_in_bytes": 1048576,
                "size_kb": 1024
            },
            {
                "file_id": 1002,
                "name": "Optional Patch",
                "version": "1.0",
                "category_name": "PATCH",
                "md5": "",
                "size_in_bytes": 2048,
                "size_kb": 2
            }
        ]
    })";

    auto files = NexusClient::parseFilesList(json);
    check("parsed OK",                  files.has_value(),
          files ? QString() : files.error().toString());
    if (files) {
        const auto &list = *files;
        check("two files parsed",           list.size() == 2);
        check("first fileId",               list[0].fileId == 1001);
        check("first category",             list[0].category == "MAIN");
        check("md5 lowercased",             list[0].md5 == "abcdef0123456789", list[0].md5);
        check("size_in_bytes as qint64",    list[0].sizeBytes == 1048576);
        check("size_kb as double",          list[0].sizeKb == 1024.0);
        check("second name",                list[1].name == "Optional Patch");
        check("empty md5 stays empty",      list[1].md5.isEmpty());
    }

    // Drop superseded uploads via the "file_updates" chain. Mod 58624:
    // author re-uploaded v11 seconds apart -> two same-name "MAIN" v11
    // files plus archived history. Only the still-present head of each
    // lineage survives, in original order; files in no chain stay.
    QByteArray superseded = R"({
        "files": [
            { "file_id": 7001, "name": "Mine", "version": "9",  "category_name": "ARCHIVED", "size_kb": 116 },
            { "file_id": 7002, "name": "Mine", "version": "10", "category_name": "ARCHIVED", "size_kb": 116 },
            { "file_id": 8001, "name": "Mine", "version": "11", "category_name": "MAIN",     "size_kb": 116 },
            { "file_id": 8002, "name": "Mine", "version": "11", "category_name": "MAIN",     "size_kb": 116 },
            { "file_id": 9001, "name": "Optional Add-on", "version": "1", "category_name": "OPTIONAL", "size_kb": 2 }
        ],
        "file_updates": [
            { "old_file_id": 7001, "new_file_id": 7002 },
            { "old_file_id": 7002, "new_file_id": 8001 },
            { "old_file_id": 8001, "new_file_id": 8002 }
        ]
    })";
    auto cur = NexusClient::parseFilesList(superseded);
    check("superseded parse OK", cur.has_value(),
          cur ? QString() : cur.error().toString());
    if (cur) {
        const auto &l = *cur;
        check("superseded chain collapses to heads", l.size() == 2,
              QString("size=%1").arg(l.size()));
        check("current MAIN survives",        l.size() > 0 && l[0].fileId == 8002);
        check("survivor keeps MAIN category",  l.size() > 0 && l[0].category == "MAIN");
        check("independent optional survives", l.size() > 1 && l[1].fileId == 9001);
        check("superseded twin dropped",
              std::none_of(l.begin(), l.end(),
                           [](const auto &f) { return f.fileId == 8001; }));
        check("archived ancestors dropped",
              std::none_of(l.begin(), l.end(),
                           [](const auto &f) { return f.fileId == 7001 || f.fileId == 7002; }));
    }

    // Empty "files" array -> empty list, not error (zero-file mod is real).
    auto empty = NexusClient::parseFilesList(R"({"files": []})");
    check("empty array → success",    empty.has_value());
    check("empty array → empty list", empty && empty->isEmpty());

    // Missing "files" key -> MissingField. Used to collapse to an empty
    // list and hide error envelopes.
    auto missing = NexusClient::parseFilesList("{}");
    check("missing key → MissingField(files)",
          !missing && missing.error().kind == K::MissingField
                   && missing.error().detail == QStringLiteral("files"),
          missing ? QString() : missing.error().toString());

    auto garbage = NexusClient::parseFilesList("not json");
    check("malformed → InvalidJson",
          !garbage && garbage.error().kind == K::InvalidJson,
          garbage ? QString() : garbage.error().toString());

    auto wrong = NexusClient::parseFilesList("[]");
    check("array top-level → WrongShape",
          !wrong && wrong.error().kind == K::WrongShape,
          wrong ? QString() : wrong.error().toString());
}

static void testDownloadUri()
{
    using K = NexusClient::NexusError::Kind;
    std::cout << "\nparseDownloadUri:\n";

    // Array of CDN candidates; first is used.
    QByteArray json = R"([
        {"name": "Nexus CDN", "short_name": "Nexus CDN",
         "URI": "https://supporter-files.nexus-cdn.com/100/42/MyMod-42-2-3.zip"},
        {"name": "Paris", "short_name": "Paris",
         "URI": "https://fr.nexus-cdn.com/100/42/MyMod-42-2-3.zip"}
    ])";
    {
        const auto r = NexusClient::parseDownloadUri(json);
        check("first URI picked - has value", r.has_value(),
              r ? r.value() : r.error().toString());
        if (r)
            check("first URI picked - exact match",
                  r.value() == "https://supporter-files.nexus-cdn.com/100/42/MyMod-42-2-3.zip",
                  r.value());
    }

    // Each failure mode reports a distinct NexusError; an empty-string
    // sentinel would lose that.
    {
        const auto r = NexusClient::parseDownloadUri("[]");
        check("empty array → EmptyPayload",
              !r && r.error().kind == K::EmptyPayload,
              r ? r.value() : r.error().toString());
    }
    {
        const auto r = NexusClient::parseDownloadUri(R"({"URI":"x"})");
        check("object → WrongShape",
              !r && r.error().kind == K::WrongShape,
              r ? r.value() : r.error().toString());
    }
    {
        const auto r = NexusClient::parseDownloadUri("not json");
        check("garbage → InvalidJson",
              !r && r.error().kind == K::InvalidJson,
              r ? r.value() : r.error().toString());
    }
    {
        // Entry with no URI, or a blank URI.
        const auto r1 = NexusClient::parseDownloadUri(R"([{"name":"x"}])");
        check("no URI field → MissingField(URI)",
              !r1 && r1.error().kind == K::MissingField
                  && r1.error().detail == QStringLiteral("URI"),
              r1 ? r1.value() : r1.error().toString());
        const auto r2 = NexusClient::parseDownloadUri(R"([{"URI":""}])");
        check("blank URI → MissingField(URI)",
              !r2 && r2.error().kind == K::MissingField
                  && r2.error().detail == QStringLiteral("URI"),
              r2 ? r2.value() : r2.error().toString());
    }

    // toString() gives stable ASCII tokens for logs.
    {
        using NexusError = NexusClient::NexusError;
        check("toString InvalidJson",
              NexusError{K::InvalidJson, {}}.toString() == QStringLiteral("InvalidJson"));
        check("toString WrongShape",
              NexusError{K::WrongShape, {}}.toString() == QStringLiteral("WrongShape"));
        check("toString MissingField wraps detail",
              NexusError{K::MissingField, QStringLiteral("URI")}.toString()
                  == QStringLiteral("MissingField(URI)"));
        check("toString EmptyPayload",
              NexusError{K::EmptyPayload, {}}.toString() == QStringLiteral("EmptyPayload"));
    }
}

static void run_nexus_client()
{
    std::cout << "=== nexus_client tests ===\n";

    testModInfo();
    testFilesList();
    testDownloadUri();
}

using deps::ModEntry;


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
    // Disabled target, DependsOn points at a URL not in the list.
    // Missing-dep icon stays off: disabled mods can't crash until enabled.
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
    // Two rows share URL_OTHER, one disabled, one enabled. The enabled
    // one satisfies the dep, so it must not show in missingLabels.
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
    // Auto-linked patch: the patch's own NexusUrl is the URL in its
    // DependsOn (shared modpage). Resolver must skip self, else it reports
    // the dep satisfied even when the base isn't in the list.
    const QList<ModEntry> soloPatch = {
        mk(0, "Patch", URL_BASE, true, true, { URL_BASE }),
    };
    auto r1 = deps::resolveDependencies(soloPatch[0], soloPatch);
    check("self-URL alone → missing (not self-satisfied)", r1.hasMissing);
    check("self-URL alone → hasInListDep=false",           !r1.hasInListDep);

    // A real sibling with the same URL resolves the dep.
    const QList<ModEntry> withBase = {
        mk(0, "Base",  URL_BASE, true, true),
        mk(1, "Patch", URL_BASE, true, true, { URL_BASE }),
    };
    auto r2 = deps::resolveDependencies(withBase[1], withBase);
    check("self-URL + real sibling → satisfied", !r2.hasMissing);
    check("self-URL + real sibling → hasInListDep=true", r2.hasInListDep);
}

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
    // User installed the PATCH first (row 0), now installs MAIN (row 1).
    // The MAIN install mutates the existing sibling so row 0 (the patch)
    // gains URL_BASE in its DependsOn.
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
    // OPTIONAL, MISCELLANEOUS, OLD and anything unrecognised fall through
    // to the dependent branch.
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
    // Two URL-less local mods must not auto-link just because both URLs
    // are empty.
    const QList<ModEntry> all = {
        mk(0, "Local A", ""),
        mk(1, "Local B", ""),
    };
    auto acts = deps::autoLinkSameModpage(all[1], all, "PATCH");
    check("empty NexusUrl → no actions", acts.isEmpty());
}

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
    // 222 not installed.

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
    // All three missing; appearance order must be 333, 111, 222.
    check("first-seen order preserved",
          r.missingModIds == QList<int>({333, 111, 222}));
}

using deps::Highlight;

// Selecting a content mod whose DependsOn lists a library URL paints the
// library green. Original bug was data (Stargazer.DependsOn empty after a
// restore); this guards the wiring against silently dropping the Dep match.
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

// Selecting a library tints its consumers. Skill Framework is a utility,
// so consumers go green (Dep) instead of the default blue (User).
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

// Non-utility selection: consumers get blue (User), the default for
// content mods.
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

// Empty NexusUrl on the selected row: no Dep matches (nothing in an empty
// DependsOn) and no User matches (no URL to reference). All-None, no crash.
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

// Empty-URL candidate rows never satisfy a Dep match: DependsOn entries are
// URLs, so an empty URL matches nothing. Guards against someone changing the
// match to "contains name" or similar.
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

// selectedIdx out of range (nothing selected, or stale after a row was
// removed): all-None, no crash.
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

// Output length must equal input length: MainWindow indexes m_modList by
// row, so an off-by-one would mis-highlight or skip a row.
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

static void run_deps_resolver()
{
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
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    run_nexus_client();
    run_deps_resolver();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
