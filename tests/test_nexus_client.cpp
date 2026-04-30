// tests/test_nexus_client.cpp
//
// Coverage for NexusClient's pure response parsers - the part of the
// Nexus API layer that doesn't need a QNetworkAccessManager to run.
// The request-building methods are exercised indirectly in the app;
// what matters for test confidence is that we extract the right
// fields out of real-world Nexus JSON.
//
// Build + run:
//   ./build/tests/test_nexus_client

#include "nexusclient.h"

#include <QCoreApplication>
#include <QByteArray>

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

// parseModInfo

static void testModInfo()
{
    using K = NexusClient::NexusError::Kind;
    std::cout << "\nparseModInfo:\n";

    // Happy path - shape mirrors /v1/games/{game}/mods/{modId}.json
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

    // Name is trimmed - Nexus occasionally returns leading/trailing whitespace
    {
        QByteArray json = R"({"name": "  Morrowind Rebirth  "})";
        auto info = NexusClient::parseModInfo(json);
        check("name trimmed", info && info->name == "Morrowind Rebirth",
              info ? info->name : info.error().toString());
    }

    // Missing individual fields within a valid object → parse succeeds
    // with defaults (mirrors Nexus's habit of omitting optional fields).
    {
        auto info = NexusClient::parseModInfo("{}");
        check("empty object → parse succeeds", info.has_value());
        check("missing name → empty",          info && info->name.isEmpty());
        check("missing desc → empty",          info && info->description.isEmpty());
        check("missing timestamp → 0",         info && info->updatedTimestamp == 0);
    }

    // Garbage input → structured error, not silent defaults.
    {
        auto info = NexusClient::parseModInfo("not json");
        check("malformed → InvalidJson",
              !info && info.error().kind == K::InvalidJson,
              info ? info->name : info.error().toString());
    }

    // Top-level array where an object was expected → WrongShape.
    {
        auto info = NexusClient::parseModInfo("[1,2,3]");
        check("array top-level → WrongShape",
              !info && info.error().kind == K::WrongShape,
              info ? info->name : info.error().toString());
    }
}

// parseFilesList

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

    // Empty "files" array → success with an empty list (mod with zero files
    // is a real state, not an error).
    auto empty = NexusClient::parseFilesList(R"({"files": []})");
    check("empty array → success",    empty.has_value());
    check("empty array → empty list", empty && empty->isEmpty());

    // Missing "files" key → MissingField("files").  This is the case that
    // previously collapsed into an empty list and masked error envelopes.
    auto missing = NexusClient::parseFilesList("{}");
    check("missing key → MissingField(files)",
          !missing && missing.error().kind == K::MissingField
                   && missing.error().detail == QStringLiteral("files"),
          missing ? QString() : missing.error().toString());

    // Malformed JSON → InvalidJson.
    auto garbage = NexusClient::parseFilesList("not json");
    check("malformed → InvalidJson",
          !garbage && garbage.error().kind == K::InvalidJson,
          garbage ? QString() : garbage.error().toString());

    // Top-level array where object was expected → WrongShape.
    auto wrong = NexusClient::parseFilesList("[]");
    check("array top-level → WrongShape",
          !wrong && wrong.error().kind == K::WrongShape,
          wrong ? QString() : wrong.error().toString());
}

// parseDownloadUri

static void testDownloadUri()
{
    using K = NexusClient::NexusError::Kind;
    std::cout << "\nparseDownloadUri:\n";

    // Canonical shape: an array of CDN candidates, first one is used.
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

    // Each failure mode reports a distinct, structured NexusError so
    // downstream code (and humans reading the log) can tell them apart.
    // A single empty-string sentinel would have dropped this information.
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
        // Array with an entry that has no URI at all, or has a blank one.
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

    // toString() produces stable, ASCII-only log tokens.
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "=== nexus_client tests ===\n";

    testModInfo();
    testFilesList();
    testDownloadUri();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
