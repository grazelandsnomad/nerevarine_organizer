// Standalone test for NXM/NXMS URL handling and Nexus reachability.
// Both nxm:// and nxms:// must be supported. nxms:// is the SSL/CDN
// variant used for premium and some standard downloads; dropping it
// causes "Unknown protocol: nxms" in KDE.

#include "nxmurl.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <iostream>

// -- Mini test runner ---

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok)
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name << "\n";
        ++s_failed;
    }
}

// -- Test suites ---
//
// The production parser lives in include/nxmurl.h + src/nxmurl.cpp.
// These tests hit that same function directly - no mirrored copy to
// drift out of sync any more.

static void testNxmUrlParsing()
{
    std::cout << "\n[nxm:// URL parsing]\n";

    auto r = parseNxmUrl("nxm://morrowind/mods/42/files/100?key=abc&expires=9999");
    check("scheme accepted",    r.has_value());
    check("game extracted",     r && r->game    == "morrowind");
    check("modId extracted",    r && r->modId   == 42);
    check("fileId extracted",   r && r->fileId  == 100);
    check("key extracted",      r && r->key     == "abc");
    check("expires extracted",  r && r->expires == "9999");
}

static void testNxmsUrlParsing()
{
    std::cout << "\n[nxms:// URL parsing - SSL/CDN variant]\n";

    auto r = parseNxmUrl("nxms://morrowind/mods/42/files/100?key=xyz&expires=8888");
    check("nxms:// scheme accepted",   r.has_value());
    check("nxms:// game extracted",    r && r->game    == "morrowind");
    check("nxms:// modId extracted",   r && r->modId   == 42);
    check("nxms:// fileId extracted",  r && r->fileId  == 100);
    check("nxms:// key extracted",     r && r->key     == "xyz");
    check("nxms:// expires extracted", r && r->expires == "8888");
}

static void testInvalidUrls()
{
    std::cout << "\n[invalid URL rejection]\n";

    auto https   = parseNxmUrl("https://www.nexusmods.com/morrowind/mods/42");
    auto empty   = parseNxmUrl("");
    auto badPath = parseNxmUrl("nxm://morrowind/bad/path");
    auto badMod  = parseNxmUrl("nxm://morrowind/mods/abc/files/100");
    auto badFile = parseNxmUrl("nxm://morrowind/mods/42/files/xyz");

    check("https:// rejected",       !https.has_value());
    check("https:// reason is invalid-scheme", !https && https.error() == "invalid-scheme");
    check("empty string rejected",   !empty.has_value());
    check("bad path rejected",       !badPath.has_value());
    check("bad path reason is invalid-path",  !badPath && badPath.error() == "invalid-path");
    check("non-numeric modId",       !badMod.has_value());
    check("non-numeric modId reason is invalid-ids",
          !badMod && badMod.error() == "invalid-ids");
    check("non-numeric fileId",      !badFile.has_value());
}

static void testProtocolFilesExist()
{
    std::cout << "\n[KIO protocol registration]\n";

    QString dir = QDir::homePath() + "/.local/share/kservices5";
    check("nxm.protocol file exists",  QFile::exists(dir + "/nxm.protocol"));
    check("nxms.protocol file exists", QFile::exists(dir + "/nxms.protocol"));
}

static void testNexusApiReachable()
{
    std::cout << "\n[Nexus API reachability]\n";

    QNetworkAccessManager nam;
    QEventLoop loop;
    bool reachable = false;
    bool timedOut  = false;

    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        timedOut = true;
        loop.quit();
    });
    timer.start(8000);

    // Hit the public games list endpoint - any HTTP response (even 401) means
    // the server is up and TLS works for both nxm and nxms download URLs.
    QNetworkRequest req(QUrl("https://api.nexusmods.com/v1/games.json"));
    req.setRawHeader("accept", "application/json");
    QNetworkReply *reply = nam.get(req);

    QObject::connect(reply, &QNetworkReply::finished, [&] {
        int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Any HTTP status code means the host is reachable and TLS succeeded.
        reachable = (status > 0);
        reply->deleteLater();
        loop.quit();
    });

    loop.exec();

    check("api.nexusmods.com reachable over HTTPS", reachable && !timedOut);
    if (timedOut)
        std::cout << "    (request timed out after 8 s)\n";
}

// -- Entry point ---

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    std::cout << "=== Nerevarine Organizer - NXM tests ===\n";

    // Unit tests - always run.
    testNxmUrlParsing();
    testNxmsUrlParsing();
    testInvalidUrls();

    // Integration tests - skip in CI / sandboxed builds where the KIO
    // protocol files aren't installed and outbound HTTPS isn't reachable.
    // Locally, running the binary with no env set runs everything.
    const bool skipIntegration = !qgetenv("NEREVARINE_SKIP_INTEGRATION").isEmpty();
    if (skipIntegration) {
        std::cout << "\n[integration tests skipped - "
                     "NEREVARINE_SKIP_INTEGRATION=1]\n";
    } else {
        testProtocolFilesExist();
        testNexusApiReachable();
    }

    std::cout << "\n";
    if (s_failed == 0) {
        std::cout << "\033[32m" << s_passed << " / " << s_passed
                  << " tests passed\033[0m\n";
    } else {
        std::cout << s_passed << " passed, "
                  << "\033[31m" << s_failed << " failed\033[0m\n";
    }

    return s_failed ? 1 : 0;
}
