#include "nexusclient.h"
#include "deps_resolver.h"
#include "file_pick.h"

#include <QCoreApplication>
#include <QByteArray>

#include <algorithm>
#include <iostream>

#include "test_harness.h"

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
                "size_kb": 1024,
                "is_primary": true,
                "description": "The whole mod.<br />Install this one."
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
        // The two fields the picker explains files with. Both are optional on
        // a real page, so absence has to read as "nothing to say".
        check("is_primary read",            list[0].isPrimary);
        check("the author's file note read",
              list[0].description.contains("Install this one"), list[0].description);
        check("no is_primary means not primary", !list[1].isPrimary);
        check("no description means empty",  list[1].description.isEmpty());
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

static void testValidateUser()
{
    using K = NexusClient::NexusError::Kind;
    std::cout << "\nparseValidateUser:\n";

    // /v1/users/validate.json shape - premium account.
    {
        QByteArray json = R"({
            "user_id": 1234,
            "key": "abc",
            "name": "  Nerevar  ",
            "email": "nerevar@example.com",
            "is_premium": true,
            "is_supporter": false
        })";
        const auto u = NexusClient::parseValidateUser(json);
        check("premium - parsed OK", u.has_value(),
              u ? QString() : u.error().toString());
        check("premium - user_id",        u && u->userId == 1234);
        check("premium - name trimmed",   u && u->name == "Nerevar",
              u ? u->name : u.error().toString());
        check("premium - email extracted", u && u->email == "nerevar@example.com");
        check("premium - isPremium true",  u && u->isPremium);
    }

    // Free account: is_premium false, fields still parse.
    {
        QByteArray json = R"({"user_id": 5, "name": "Free", "is_premium": false})";
        const auto u = NexusClient::parseValidateUser(json);
        check("free - parsed OK",       u.has_value(),
              u ? QString() : u.error().toString());
        check("free - isPremium false", u && !u->isPremium);
    }

    // A 200 error-envelope without user_id must NOT read as a valid account.
    {
        const auto u = NexusClient::parseValidateUser(
            R"({"message":"Please provide a valid API Key"})");
        check("no user_id → MissingField(user_id)",
              !u && u.error().kind == K::MissingField
                 && u.error().detail == QStringLiteral("user_id"),
              u ? QString() : u.error().toString());
    }
    {
        const auto u = NexusClient::parseValidateUser("{}");
        check("empty object → MissingField(user_id)",
              !u && u.error().kind == K::MissingField,
              u ? QString() : u.error().toString());
    }
    {
        const auto u = NexusClient::parseValidateUser("not json");
        check("garbage → InvalidJson",
              !u && u.error().kind == K::InvalidJson,
              u ? QString() : u.error().toString());
    }
    {
        const auto u = NexusClient::parseValidateUser("[]");
        check("array → WrongShape",
              !u && u.error().kind == K::WrongShape,
              u ? QString() : u.error().toString());
    }
}

static void run_nexus_client()
{
    std::cout << "=== nexus_client tests ===\n";

    testModInfo();
    testFilesList();
    testDownloadUri();
    testValidateUser();
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


// -- buildGraph / wouldCycle / layerOf --------------------------------
//
// The reported shape: KID needs MergeMapper, which needs SKSE and Address
// Library, which itself needs SKSE. Three levels, and the chain the canvas has
// to draw top to bottom.
static const QString URL_SKSE   = QStringLiteral("https://www.nexusmods.com/skyrimspecialedition/mods/30379");
static const QString URL_ADDRLIB= QStringLiteral("https://www.nexusmods.com/skyrimspecialedition/mods/32444");
static const QString URL_MM     = QStringLiteral("https://www.nexusmods.com/skyrimspecialedition/mods/74689");
static const QString URL_KID    = QStringLiteral("https://www.nexusmods.com/skyrimspecialedition/mods/55728");

static int nodeNamed(const deps::Graph &g, const QString &label)
{
    for (int i = 0; i < g.nodes.size(); ++i)
        if (g.nodes[i].label == label) return i;
    return -1;
}
static bool hasEdge(const deps::Graph &g, const QString &from, const QString &to)
{
    const int a = nodeNamed(g, from), b = nodeNamed(g, to);
    if (a < 0 || b < 0) return false;
    for (const auto &e : g.edges) if (e.from == a && e.to == b) return true;
    return false;
}

static void testGraphBuildsTheReportedChain()
{
    std::cout << "\n[the dependency web as a graph]\n";
    const QList<ModEntry> mods = {
        mk(0, "skse64",       URL_SKSE),
        mk(1, "Address Library", URL_ADDRLIB, true, true, {URL_SKSE}),
        mk(2, "MergeMapper",  URL_MM,  true, true, {URL_SKSE, URL_ADDRLIB}),
        mk(3, "KID",          URL_KID, true, true, {URL_MM}),
    };
    const auto g = deps::buildGraph(mods);

    check("every row is a node so any can be an endpoint", g.nodes.size() == 4);
    check("KID needs MergeMapper",         hasEdge(g, "KID", "MergeMapper"));
    check("MergeMapper needs SKSE",        hasEdge(g, "MergeMapper", "skse64"));
    check("MergeMapper needs Address Library",
          hasEdge(g, "MergeMapper", "Address Library"));
    check("Address Library needs SKSE",    hasEdge(g, "Address Library", "skse64"));
    check("and nothing points the wrong way",
          !hasEdge(g, "skse64", "MergeMapper"));

    const auto d = deps::layerOf(g);
    check("SKSE is the deepest layer",  d[nodeNamed(g, "skse64")] == 0);
    check("Address Library sits above it", d[nodeNamed(g, "Address Library")] == 1);
    check("MergeMapper above that",     d[nodeNamed(g, "MergeMapper")] == 2);
    check("KID on top",                 d[nodeNamed(g, "KID")] == 3);
}

static void testOneUrlCanBeSeveralRows()
{
    std::cout << "\n[a dependency URL is not unique to one row]\n";
    // MAIN and PATCH from one mod page share a nexusUrl. Resolving with a
    // first-match lookup drops the second edge silently.
    const QList<ModEntry> mods = {
        mk(0, "OAAB Data",       URL_SKSE),
        mk(1, "OAAB Data patch", URL_SKSE),
        mk(2, "Some mod",        URL_MM, true, true, {URL_SKSE}),
    };
    const auto g = deps::buildGraph(mods);
    check("an edge to EACH row sharing the modpage",
          hasEdge(g, "Some mod", "OAAB Data")
              && hasEdge(g, "Some mod", "OAAB Data patch"),
          QString::number(g.edges.size()));

    // The same page written two ways is the same mod.
    const QList<ModEntry> variants = {
        mk(0, "Core Impact Framework", URL_SKSE),
        mk(1, "Sanguine Symphony", URL_MM, true, true,
           {URL_SKSE + QStringLiteral("?tab=files")}),
    };
    const auto g2 = deps::buildGraph(variants);
    check("a ?tab=files URL resolves to the same mod",
          hasEdge(g2, "Sanguine Symphony", "Core Impact Framework"));
    check("and no ghost was invented for it",
          g2.nodes.size() == 2, QString::number(g2.nodes.size()));
}

static void testGhostsAndImpossibleTargets()
{
    std::cout << "\n[dependencies on mods that are not there]\n";
    const QList<ModEntry> mods = {
        mk(0, "Needs something", URL_MM, true, true, {URL_SKSE}),
        mk(1, "Another",         URL_KID, true, true, {URL_SKSE}),
    };
    const auto g = deps::buildGraph(mods);
    int ghosts = 0;
    for (const auto &n : g.nodes) if (n.ghost) ++ghosts;
    check("the absent dependency becomes ONE shared ghost", ghosts == 1,
          QString::number(ghosts));
    check("and both edges survive rather than vanishing", g.edges.size() == 2);

    // A row with no Nexus URL can never be depended upon - the picker cannot
    // offer it - so the canvas must not let the user aim an arrow at it.
    const QList<ModEntry> handAdded = { mk(0, "Hand-added folder", QString()) };
    const auto g3 = deps::buildGraph(handAdded);
    check("a row with no URL cannot be a target", !g3.nodes[0].canBeTarget);
    check("but a row with one can", deps::buildGraph({mk(0, "X", URL_MM)})
                                       .nodes[0].canBeTarget);
}

static void testSectionTravelsToTheNodes()
{
    std::cout << "\n[a node remembers which separator it sits under]\n";
    // The canvas scopes what it draws by separator, and the only place that
    // grouping exists is the snapshot - the graph itself has no idea where the
    // separators are unless the node carries it.
    QList<ModEntry> mods = {
        mk(0, "skse64",      URL_SKSE),
        mk(1, "MergeMapper", URL_MM, true, true, {URL_SKSE}),
    };
    mods[0].section = QStringLiteral("Utility mods");
    mods[1].section = QStringLiteral("SKSE mods");
    const auto g = deps::buildGraph(mods);

    check("each node keeps its own section",
          g.nodes[nodeNamed(g, "skse64")].section == QLatin1String("Utility mods")
       && g.nodes[nodeNamed(g, "MergeMapper")].section == QLatin1String("SKSE mods"));
    // Two thirds of the arrows on the author's real lists cross a boundary
    // like this one, which is why the view cannot draw a section alone.
    check("an edge may cross a section boundary",
          hasEdge(g, "MergeMapper", "skse64"));
}

static void testNoSelfEdge()
{
    std::cout << "\n[a mod cannot depend on itself]\n";
    // A patch auto-linked to its own modpage ends up with DependsOn == its own
    // URL; resolveDependencies already skips that, and so must the graph.
    const auto g = deps::buildGraph({mk(0, "Patch", URL_MM, true, true, {URL_MM})});
    check("no self-edge is drawn", g.edges.isEmpty(),
          QString::number(g.edges.size()));
}

static void testCycleRefusalAndTolerance()
{
    std::cout << "\n[cycles are refused, and survived if already present]\n";
    const QList<ModEntry> mods = {
        mk(0, "A", URL_SKSE),
        mk(1, "B", URL_MM, true, true, {URL_SKSE}),   // B needs A
    };
    const auto g = deps::buildGraph(mods);
    const int A = nodeNamed(g, "A"), B = nodeNamed(g, "B");

    check("A needing B would close a loop", deps::wouldCycle(g, A, B));
    check("but B needing A again is merely redundant",
          !deps::wouldCycle(g, B, A));
    check("nothing may depend on itself", deps::wouldCycle(g, A, A));
    check("out-of-range endpoints are not a cycle",
          !deps::wouldCycle(g, 99, A) && !deps::wouldCycle(g, A, -1));

    // A cycle can still reach the layout - two edges added in separate
    // sessions, or a hand-edited modlist. layerOf must terminate.
    QList<ModEntry> looped = mods;
    looped[0].dependsOn = {URL_MM};        // now A needs B and B needs A
    const auto gl = deps::buildGraph(looped);
    const auto d  = deps::layerOf(gl);
    check("layerOf terminates on a cycle", d.size() == gl.nodes.size());
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
    testGraphBuildsTheReportedChain();
    testOneUrlCanBeSeveralRows();
    testGhostsAndImpossibleTargets();
    testSectionTravelsToTheNodes();
    testNoSelfEdge();
    testCycleRefusalAndTolerance();
}

// -- file_pick: what each file on a mod page is ------------------------
//
// The page that prompted it, verbatim from the picker: Rafael's Shader Pack
// (Morrowind mod 53667) offers a 22.4 MB main file, a 2.3 MB add-on with
// "OpenMW" in its name, and a patch for the add-on. Nothing on screen said
// which was which, and the engine scorer pre-selected the add-on because its
// name mentioned OpenMW and the base file's did not.

static QList<file_pick::FileInfo> rafaelsPage(bool markPrimary = true)
{
    file_pick::FileInfo base;
    base.name      = QStringLiteral("Rafael's Shader Pack 2.0e");
    base.version   = QStringLiteral("2.0e");
    base.category  = QStringLiteral("MAIN");
    base.sizeBytes = 23488102;
    base.isPrimary = markPrimary;

    file_pick::FileInfo pbr;
    pbr.name      = QStringLiteral("Enhanced PBR Lighting for OpenMW 0.49-0.52");
    pbr.version   = QStringLiteral("2.0e");
    pbr.category  = QStringLiteral("MAIN");
    pbr.sizeBytes = 2411724;

    file_pick::FileInfo patch;
    patch.name      = QStringLiteral("Patch For Enhanced PBR Lighting For OpenMW 0.52");
    patch.version   = QStringLiteral("2.0e");
    patch.category  = QStringLiteral("UPDATE");
    patch.sizeBytes = 4096;

    return { base, pbr, patch };
}

// The engine scorer's real verdict on those three names for an OpenMW
// profile: "OpenMW" in the name scores 2, engine-neutral scores 1.
static const QList<int> kRafaelScores = { 1, 2, 2 };

static void testDescribesTheReportedPage()
{
    std::cout << "\nfile_pick::describe:\n";
    const auto files = rafaelsPage();
    const auto notes = file_pick::describe(files);
    check("one note per file", notes.size() == 3);

    check("the page's own download is the mod",
          notes[0].kind == file_pick::Kind::Base);

    check("the second main file is an add-on beside it",
          notes[1].kind == file_pick::Kind::AddOn);
    check("named after the file it sits beside",
          notes[1].detailArg == QLatin1String("Rafael's Shader Pack 2.0e"),
          notes[1].detailArg);

    check("the UPDATE is a patch", notes[2].kind == file_pick::Kind::Patch);
    // The whole point of the pairing: the patch is for the ADD-ON, not for
    // the mod, and its name is the only thing that says so.
    check("the patch is matched to the add-on it patches",
          notes[2].goesOn == 1, QString::number(notes[2].goesOn));
    check("and the name the sentence shows is the add-on's",
          notes[2].detailArg
              == QLatin1String("Enhanced PBR Lighting for OpenMW 0.49-0.52"));
}

static void testNoPrimaryMeansNoRanking()
{
    std::cout << "\nfile_pick::describe with no primary marked:\n";
    const auto notes = file_pick::describe(rafaelsPage(false));
    // Nothing claims to be the page's download, so nothing is an "add-on" to
    // anything - two MAIN files are two MAIN files, said plainly.
    check("neither main file is called the mod",
          notes[0].kind == file_pick::Kind::Main
              && notes[1].kind == file_pick::Kind::Main);
    check("so neither names the other",
          notes[0].detailArg.isEmpty() && notes[1].detailArg.isEmpty());
    check("the patch is still a patch",
          notes[2].kind == file_pick::Kind::Patch && notes[2].goesOn == 1);

    // Two files both flagged primary is a page saying nothing, not a page
    // saying both.
    auto two = rafaelsPage();
    two[1].isPrimary = true;
    const auto n2 = file_pick::describe(two);
    check("two primaries cancel out",
          n2[0].kind == file_pick::Kind::Main
              && n2[1].kind == file_pick::Kind::Main);
}

static void testPatchPairingNeedsARealName()
{
    std::cout << "\nfile_pick::describe patch pairing:\n";
    file_pick::FileInfo core;
    core.name      = QStringLiteral("Core");
    core.category  = QStringLiteral("MAIN");
    core.isPrimary = true;
    file_pick::FileInfo patch;
    patch.name     = QStringLiteral("Hotfix for the core meshes");
    patch.category = QStringLiteral("UPDATE");
    const auto notes = file_pick::describe({ core, patch });
    // "Core" appears inside half the names on any page. A short one-word name
    // identifies nothing, so the patch says it is a patch and stops there.
    check("a one-word file name does not claim the patch",
          notes[1].goesOn == -1, QString::number(notes[1].goesOn));
    check("so the wording has no name to drop in",
          notes[1].detailArg.isEmpty());

    file_pick::FileInfo old;
    old.name     = QStringLiteral("Rafael's Shader Pack 1.9");
    old.category = QStringLiteral("OLD_VERSION");
    file_pick::FileInfo misc;
    misc.name     = QStringLiteral("Config tool");
    misc.category = QStringLiteral("MISCELLANEOUS");
    file_pick::FileInfo opt;
    opt.name      = QStringLiteral("Alternative colours");
    opt.category  = QStringLiteral("OPTIONAL");
    const auto rest = file_pick::describe({ old, misc, opt });
    check("an archived upload is called an older version",
          rest[0].kind == file_pick::Kind::Old);
    check("a miscellaneous file is neither main nor patch",
          rest[1].kind == file_pick::Kind::Other);
    check("an optional file says the mod works without it",
          rest[2].kind == file_pick::Kind::Optional);
}

static void testDefaultIndexPrefersTheModItself()
{
    std::cout << "\nfile_pick::defaultIndex:\n";
    const auto files = rafaelsPage();
    const auto notes = file_pick::describe(files);

    // The reported bug in one assertion: the add-on outscores the base file
    // 2-to-1 on the engine heuristic, and must still lose to the page's own
    // main download.
    check("the mod itself wins over a higher-scoring add-on",
          file_pick::defaultIndex(files, notes, kRafaelScores) == 0,
          QString::number(file_pick::defaultIndex(files, notes, kRafaelScores)));

    // With nothing marked primary there is no such answer, and the engine
    // score is all that is left - the old behaviour, kept.
    const auto loose = rafaelsPage(false);
    check("without a primary the engine score decides again",
          file_pick::defaultIndex(loose, file_pick::describe(loose), kRafaelScores) == 1);

    // A main download built for the wrong engine is still the wrong file.
    check("a negative engine score overrules the primary",
          file_pick::defaultIndex(files, notes, { -1, 2, 2 }) == 1);

    // Defaulting to a patch installs a fragment of a mod.
    {
        auto twoPatches = rafaelsPage(false);
        twoPatches[1].category = QStringLiteral("UPDATE");
        const auto n = file_pick::describe(twoPatches);
        check("a patch is never the opening selection",
              file_pick::defaultIndex(twoPatches, n, { 1, 9, 9 }) == 0);
    }

    // A page of nothing but patches still has to open on something.
    {
        QList<file_pick::FileInfo> only;
        for (auto f : rafaelsPage(false)) {
            f.category = QStringLiteral("UPDATE");
            only.append(f);
        }
        const auto n = file_pick::describe(only);
        const int idx = file_pick::defaultIndex(only, n, {});
        check("an all-patch page selects a real row", idx >= 0 && idx < only.size(),
              QString::number(idx));
    }

    check("an empty list is index 0, not a crash",
          file_pick::defaultIndex({}, {}, {}) == 0);
    check("missing scores are treated as no opinion",
          file_pick::defaultIndex(files, notes, {}) == 0);
}

static void testPlainDescription()
{
    std::cout << "\nfile_pick::plainDescription:\n";
    check("a line break becomes a space",
          file_pick::plainDescription("Main file.<br />Install first.")
              == QLatin1String("Main file. Install first."),
          file_pick::plainDescription("Main file.<br />Install first."));
    check("markup does not run words together",
          file_pick::plainDescription("<b>Requires</b><i>the main file</i>")
              == QLatin1String("Requires the main file"),
          file_pick::plainDescription("<b>Requires</b><i>the main file</i>"));
    check("entities are decoded",
          file_pick::plainDescription("Rafael&#39;s pack &amp; patch")
              == QLatin1String("Rafael's pack & patch"),
          file_pick::plainDescription("Rafael&#39;s pack &amp; patch"));
    // &amp;lt; must decode to "&lt;", not all the way to a "<" that then
    // looks like markup the stripper already ran past.
    check("a double-encoded entity only decodes once",
          file_pick::plainDescription("&amp;lt;b&amp;gt;")
              == QLatin1String("&lt;b&gt;"),
          file_pick::plainDescription("&amp;lt;b&amp;gt;"));
    check("whitespace collapses",
          file_pick::plainDescription("  a\n\n   b  ") == QLatin1String("a b"));
    check("empty stays empty", file_pick::plainDescription(QString()).isEmpty());

    const QString longText = QStringLiteral("word ").repeated(200);
    const QString cut = file_pick::plainDescription(longText, 40);
    check("a long description is cut", cut.size() <= 44, QString::number(cut.size()));
    check("and cut at a word, not mid-word", cut.endsWith(QLatin1String("word...")), cut);
    check("a short one is untouched",
          file_pick::plainDescription("Short.", 40) == QLatin1String("Short."));
}


// The second page that prompted this: Skyrim's SKSE64, which ships one build
// per store because the GOG release of the game is a different executable.
//
//   Skyrim Script Extender (SKSE64) GOG    [v2.2.6]  MAIN  0.9 MB
//   Skyrim Script Extender (SKSE64) Steam  [v2.3.0]  MAIN  0.9 MB   <- primary
//
// The picker labelled the GOG one "separate add-on" and offered no hint that
// the choice was about which shop the game came from.

static QList<file_pick::FileInfo> sksePage()
{
    file_pick::FileInfo gog;
    gog.name      = QStringLiteral("Skyrim Script Extender (SKSE64) GOG");
    gog.version   = QStringLiteral("2.2.6");
    gog.category  = QStringLiteral("MAIN");
    gog.sizeBytes = 944128;

    file_pick::FileInfo steam;
    steam.name        = QStringLiteral("Skyrim Script Extender (SKSE64) Steam");
    steam.version     = QStringLiteral("2.3.0");
    steam.category    = QStringLiteral("MAIN");
    steam.sizeBytes   = 944640;
    steam.isPrimary   = true;
    steam.description =
        QStringLiteral("Compatible with Skyrim Special Edition 1.7.99 from Steam");

    return { gog, steam };
}

static void testStoreBuildsAreNotAddOns()
{
    using game_store::Store;
    std::cout << "\nfile_pick::describe with a build per store:\n";
    const auto files = sksePage();
    const auto notes = file_pick::describe(files);

    check("each build is tied to its store",
          notes[0].store == Store::Gog && notes[1].store == Store::Steam);

    // The reported bug: the primary flag made the other store's build look
    // like an extra, when it is the same mod for a different game exe.
    check("the other store's build is not an add-on",
          notes[0].kind != file_pick::Kind::AddOn);
    check("it is a complete download in its own right",
          notes[0].kind == file_pick::Kind::Main);
    check("and nothing names it after the primary",
          notes[0].detailArg.isEmpty());
    check("the primary is still the page's own download",
          notes[1].kind == file_pick::Kind::Base);

    // A store word needs a counterpart to mean anything. On a page with one
    // file that happens to say "Steam", it is just a word.
    {
        auto lone = sksePage();
        lone.removeAt(0);
        file_pick::FileInfo preset;
        preset.name     = QStringLiteral("Steam Deck performance preset");
        preset.category = QStringLiteral("OPTIONAL");
        lone.append(preset);
        const auto n = file_pick::describe(lone);
        check("one store named on a page is no choice at all",
              n[0].store == Store::Unknown && n[1].store == Store::Unknown);
    }

    // Same store twice is not a choice either.
    {
        auto both = sksePage();
        both[0].name = QStringLiteral("Skyrim Script Extender (SKSE64) Steam AE");
        const auto n = file_pick::describe(both);
        check("two builds for the same shop are not store variants",
              n[0].store == Store::Unknown && n[1].store == Store::Unknown);
    }
}

static void testTheUsersOwnStoreDecides()
{
    using game_store::Store;
    std::cout << "\nfile_pick::defaultIndex with a build per store:\n";
    const auto files = sksePage();
    const auto notes = file_pick::describe(files);

    // The whole point. The author flagged the Steam build as the page's
    // download; for somebody who bought the game on GOG that flag is wrong,
    // and the file it points at loads nothing.
    check("a GOG copy opens on the GOG build, primary flag or not",
          file_pick::defaultIndex(files, notes, {}, Store::Gog) == 0,
          QString::number(file_pick::defaultIndex(files, notes, {}, Store::Gog)));
    check("a Steam copy opens on the Steam build",
          file_pick::defaultIndex(files, notes, {}, Store::Steam) == 1);
    check("knowing nothing falls back to the page's own answer",
          file_pick::defaultIndex(files, notes, {}, Store::Unknown) == 1);

    // Knowing the shop does not license installing a fragment: a patch is
    // still never the opening selection, even the right shop's patch.
    {
        auto page = sksePage();
        page[0].category = QStringLiteral("UPDATE");
        const auto n = file_pick::describe(page);
        check("the right store's patch is still a patch",
              file_pick::defaultIndex(page, n, {}, Store::Gog) == 1);
    }

    // Nor a build for the wrong engine, which is a harder no than a store.
    check("a negative engine score still overrules the store",
          file_pick::defaultIndex(files, notes, { -1, 1 }, Store::Gog) == 1);

    // A store we have no build for leaves the page's own answer standing.
    {
        auto page = sksePage();
        page.removeAt(0);
        const auto n = file_pick::describe(page);
        check("a store with no build here changes nothing",
              file_pick::defaultIndex(page, n, {}, Store::Gog) == 0);
    }
}

static void run_file_pick()
{
    testDescribesTheReportedPage();
    testNoPrimaryMeansNoRanking();
    testPatchPairingNeedsARealName();
    testDefaultIndexPrefersTheModItself();
    testStoreBuildsAreNotAddOns();
    testTheUsersOwnStoreDecides();
    testPlainDescription();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    run_nexus_client();
    run_deps_resolver();
    run_file_pick();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
