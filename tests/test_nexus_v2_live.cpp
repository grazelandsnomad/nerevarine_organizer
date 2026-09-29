// test_nexus_v2_live - the v2 GraphQL queries, asked of the real endpoint.
//
// Not a coverage test: a canary. Every v2 caller in this app degrades
// gracefully - a failed or short answer falls back to the v1 path, or to the
// description-only dialog - so when the endpoint changes, NOTHING breaks
// visibly. Both queries shipped truncating for exactly that reason: the
// requirements table came back 20 rows at a time and the name lookup 20 ids
// at a time, and the app looked fine throughout. This asks the questions the
// app asks, through the app's own client code, and checks the answers are
// whole.
//
// Labelled "integration": CI's `ctest -L unit` does not run it, and it exits
// 77 (reported as skipped) with no network or with
// NEREVARINE_SKIP_INTEGRATION set.

#include "nexusclient.h"
#include "test_harness.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSet>
#include <QTimer>

#include <iostream>

namespace {

constexpr int kFallout4 = 1151;

// Wait for one reply, with a ceiling so a hung endpoint fails the check
// instead of hanging the run.
QByteArray await(QNetworkReply *reply, bool *ok)
{
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(30000);
    loop.exec();
    *ok = reply->isFinished() && reply->error() == QNetworkReply::NoError;
    const QByteArray body = reply->isFinished() ? reply->readAll() : QByteArray();
    reply->deleteLater();
    return body;
}

bool networkAvailable()
{
    const QHostInfo info = QHostInfo::fromName(QStringLiteral("api.nexusmods.com"));
    return info.error() == QHostInfo::NoError && !info.addresses().isEmpty();
}

// Vehicle Overhaul Continued: a table long enough to span more than the
// server's default page, which is the whole point. It had 23 rows when this
// was written; the check is "everything the server says exists arrived", so
// the author adding a row does not break it.
void testRequirementsArriveWhole(NexusClient &client)
{
    std::cout << "\n[v2 requirements table, paged to the end]\n";
    int offset = 0, total = -1, nodes = 0, pages = 0;
    QList<NexusClient::Requirement> rows;
    for (; pages < 20; ++pages) {
        bool ok = false;
        const QByteArray body =
            await(client.requestModRequirements(kFallout4, 93291, offset), &ok);
        check("the request succeeds", ok);
        if (!ok) return;
        NexusClient::PageInfo page;
        const auto got = NexusClient::parseModRequirements(body, &page);
        check("and parses", got.has_value());
        if (!got) return;
        rows += *got;
        total  = page.total;
        nodes += page.received;
        offset += page.received;
        if (page.received == 0 || offset >= page.total) break;
    }
    check("the server reports more rows than one default page holds", total > 20,
          QString::number(total));
    check("and every one of them arrives", nodes == total,
          QStringLiteral("%1 of %2").arg(nodes).arg(total));
    // The row that made the table worth having: a hard requirement the
    // description never links.
    bool baka = false;
    for (const auto &r : rows)
        if (r.modId == 43627) baka = true;
    check("including Baka Framework", baka);
}

// 158 ids - the size of the deps dialog that exposed the name truncation.
// Ids in a range that all resolved when this was written; a few going away
// over time is fine, the regression was 20 or 80 coming back out of 158.
void testNamesArriveWhole(NexusClient &client)
{
    std::cout << "\n[v2 names, chunked to the page cap]\n";
    QList<int> ids;
    for (int i = 60000; i < 60158; ++i) ids << i;

    const auto chunks = NexusClient::chunkForV2(ids);
    check("158 ids are two requests", chunks.size() == 2);

    QSet<int> named;
    for (const QList<int> &chunk : chunks) {
        bool ok = false;
        const QByteArray body = await(client.requestModNames(kFallout4, chunk), &ok);
        check("each request succeeds", ok);
        const auto names = NexusClient::parseModNames(body);
        for (auto it = names.cbegin(); it != names.cend(); ++it) named.insert(it.key());
        // Within ONE chunk: without `count` the server answers 20.
        check("a whole chunk comes back, not the server's default 20",
              names.size() > 20, QString::number(names.size()));
    }
    check("nearly all 158 are named", named.size() >= 150,
          QString::number(named.size()));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    if (!qgetenv("NEREVARINE_SKIP_INTEGRATION").isEmpty()) {
        std::cout << "[v2 live tests skipped - NEREVARINE_SKIP_INTEGRATION=1]\n";
        return 77;
    }
    if (!networkAvailable()) {
        // Offline says nothing about the endpoint. Skip, do not fail.
        std::cout << "[v2 live tests skipped - no network]\n";
        return 77;
    }

    NexusClient client;
    client.setNetworkAccessManager(new QNetworkAccessManager);

    testRequirementsArriveWhole(client);
    testNamesArriveWhole(client);

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
