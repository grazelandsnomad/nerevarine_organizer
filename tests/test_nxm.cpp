// NXM/NXMS URL handling + Nexus reachability. nxms:// is the SSL/CDN variant;
// dropping it causes "Unknown protocol: nxms" in KDE.

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

#include "test_harness.h"

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
    std::cout << "\n[nxms:// URL parsing]\n";

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

static void testNexusModUrlParsing()
{
    std::cout << "\n[nexus web mod-page URL parsing]\n";

    auto canonical = parseNexusModUrl("https://www.nexusmods.com/morrowind/mods/49042");
    check("plain web URL accepted",   canonical.has_value());
    check("web URL game extracted",       canonical && canonical->game  == "morrowind");
    check("web URL modId extracted",      canonical && canonical->modId == 49042);

    // trailing path/query/fragment after the id is fine
    auto tabbed = parseNexusModUrl("https://www.nexusmods.com/skyrimspecialedition/mods/266?tab=files");
    check("trailing ?tab=files accepted",  tabbed && tabbed->modId == 266);
    check("game with query still parsed",  tabbed && tabbed->game == "skyrimspecialedition");

    auto filesTail = parseNexusModUrl("https://www.nexusmods.com/morrowind/mods/42/files");
    check("trailing /files segment accepted", filesTail && filesTail->modId == 42);

    // slugs are case-insensitive, stored lowercase
    auto mixedCase = parseNexusModUrl("https://www.nexusmods.com/Morrowind/mods/7");
    check("game slug lowercased", mixedCase && mixedCase->game == "morrowind");

    // rejections
    check("empty URL rejected",        !parseNexusModUrl("").has_value());
    check("non-mod path rejected",     !parseNexusModUrl("https://www.nexusmods.com/morrowind/users/1").has_value());
    check("missing id rejected",       !parseNexusModUrl("https://www.nexusmods.com/morrowind/mods").has_value());
    check("non-numeric id rejected",   !parseNexusModUrl("https://www.nexusmods.com/morrowind/mods/abc").has_value());
}

static void testNexusModUrlBuild()
{
    std::cout << "\n[nexus web mod-page URL building]\n";

    const QString built = nexusModUrl("morrowind", 49042);
    check("builds expected URL",
          built == "https://www.nexusmods.com/morrowind/mods/49042");

    // build then parse gives back the same thing
    auto rt = parseNexusModUrl(nexusModUrl("oblivion", 12345));
    check("build/parse game",  rt && rt->game  == "oblivion");
    check("build/parse modId", rt && rt->modId == 12345);
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

    // any HTTP response (even 401) = host up, TLS works
    QNetworkRequest req(QUrl("https://api.nexusmods.com/v1/games.json"));
    req.setRawHeader("accept", "application/json");
    QNetworkReply *reply = nam.get(req);

    QObject::connect(reply, &QNetworkReply::finished, [&] {
        int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reachable = (status > 0);
        reply->deleteLater();
        loop.quit();
    });

    loop.exec();

    check("api.nexusmods.com reachable over HTTPS", reachable && !timedOut);
    if (timedOut)
        std::cout << "    (request timed out after 8 s)\n";
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    std::cout << "=== Nerevarine Organizer - NXM tests ===\n";

    // unit tests always run
    testNxmUrlParsing();
    testNxmsUrlParsing();
    testInvalidUrls();
    testNexusModUrlParsing();
    testNexusModUrlBuild();

    // integration tests need KIO protocol files + outbound HTTPS; skip in
    // CI/sandbox via the env var
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
