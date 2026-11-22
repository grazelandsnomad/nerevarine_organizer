#include "nexuscontroller.h"

#include "deps_resolver.h"
#include "nexusclient.h"
#include "update_budget.h"
#include "update_sweep.h"
#include "logging.h"

#include <QDateTime>
#include <QNetworkReply>
#include <QSet>
#include <QUuid>

#include <QTimer>
#include <functional>
#include <memory>

NexusController::NexusController(NexusClient *client, QObject *parent)
    : QObject(parent), m_client(client) {}

void NexusController::fetchModTitle(const QUuid &row,
                                    const QString &game, int modId)
{
    if (!m_client->hasApiKey() || row.isNull()) return;
    QNetworkReply *reply = m_client->requestModInfo(game, modId);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, row]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const auto info = NexusClient::parseModInfo(reply->readAll());
        if (!info || info->name.isEmpty()) return;
        emit titleFetched(row, info->name);
    });
}

void NexusController::scanDependencies(const QUuid &row,
                                       const QString &game, int modId,
                                       const QMap<int, QString> &installedIdToUrl)
{
    if (row.isNull()) return;
    QNetworkReply *reply = m_client->requestModInfo(game, modId);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, row, game, modId, installedIdToUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit dependencyScanFailed(row, game, modId);
            return;
        }
        const auto info = NexusClient::parseModInfo(reply->readAll());
        if (!info) {
            emit dependencyScanFailed(row, game, modId);
            return;
        }
        const auto parsed = deps::parseDescriptionDeps(
            info->description, game, modId, installedIdToUrl);

        // Second source: the page's AUTHORED requirements table, which the
        // v1 API does not carry. Chained, and every failure - network, a
        // GraphQL error, an adult-gated page, a missing game_id - lands in
        // the same place: an empty table, and the dialog exactly as the
        // description alone would build it.
        const QString title = info->name;
        const int gameNum   = info->gameIdNumeric;
        const auto finish = [this, row, game, modId, title, parsed,
                             installedIdToUrl, gameNum](
                                const QList<deps::TableRequirement> &table) {
            const auto merged = deps::mergeRequirements(
                parsed.classified, table, installedIdToUrl);
            // A missing TABLE row must pop the dialog even when the
            // description links nothing at all - Baka Framework is a hard
            // requirement precisely nobody linked.
            QList<int> missing = parsed.missingModIds;
            for (const auto &d : merged)
                if (!d.installed && !missing.contains(d.modId))
                    missing.append(d.modId);
            emit dependenciesScanned(row, game, modId, title,
                                     parsed.presentUrls, missing, merged,
                                     gameNum);
        };

        if (info->gameIdNumeric <= 0) { finish({}); return; }
        fetchRequirementsPage(info->gameIdNumeric, modId, 0,
                              std::make_shared<QList<deps::TableRequirement>>(),
                              0, finish);
    });
}

void NexusController::fetchFileList(const QUuid &row,
                                    const QString &game, int modId)
{
    if (row.isNull()) return;
    QNetworkReply *reply = m_client->requestModFiles(game, modId);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, row, game, modId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit fileListFetchFailed(row, reply->errorString(), status);
            return;
        }
        auto files = NexusClient::parseFilesList(reply->readAll());
        if (!files) {
            emit fileListFetchFailed(row, files.error().toString(), 200);
            return;
        }
        emit fileListFetched(row, game, modId, *files);
    });
}

void NexusController::fetchExpectedChecksum(const QUuid &row,
                                            const QString &game,
                                            int modId, int fileId)
{
    if (!m_client->hasApiKey() || row.isNull()) return;
    QNetworkReply *reply = m_client->requestModFiles(game, modId);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, row, fileId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const auto files = NexusClient::parseFilesList(reply->readAll());
        if (!files) return;
        for (const auto &f : *files) {
            if (f.fileId != fileId) continue;
            QStringList siblings;
            siblings.reserve(int(files->size()));
            for (const auto &other : *files) siblings << other.name;
            emit modFileSiblings(row, f.name, siblings);
            emit expectedChecksumFetched(row, fileId, f.fileName, f.md5, f.sizeBytes);
            return;
        }
    });
}

void NexusController::fetchChangelog(const QUuid &row,
                                     const QString &game, int modId)
{
    if (!m_client->hasApiKey() || row.isNull()) return;
    QNetworkReply *reply = m_client->requestChangelog(game, modId);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, row, game, modId]() {
        reply->deleteLater();
        // Empty list on failure so the dialog leaves its loading state.
        if (reply->error() != QNetworkReply::NoError) {
            emit changelogFetched(row, game, modId, {});
            return;
        }
        emit changelogFetched(row, game, modId,
                              NexusClient::parseChangelog(reply->readAll()));
    });
}

// Per-run state in a shared_ptr so each lambda owns a copy: handles
// overlapping runs and survives `this` dying mid-flight (replies are parented
// to `this` so they'd cancel, but the queue still drains).
//
// The queue is the point. This used to be a for loop that fired every request
// in one event-loop turn; see kCheckSpacingMs for what that cost.
struct NexusController::CheckRun {
    QList<CheckTarget> queue;
    int  inFlight = 0;          // replies asked for and not yet answered
    int  done = 0, total = 0;   // for the progress signal
    int  found = 0, superseded = 0;
    bool finished = false;      // checkUpdatesFinished fires exactly once
    bool rateLimited = false;   // Nexus said 429 and the run gave up
    QTimer *pump = nullptr;
};

// What one bulk reply per game told us, while the others are still coming.
struct NexusController::Prefilter {
    QList<CheckTarget>              targets;
    QHash<QString, QHash<int, qint64>> activity;  // game -> mod id -> newest
    QSet<QString>                   usable;       // games whose reply parsed
    int                             pending = 0;
};

void NexusController::checkForUpdates(
    const QList<CheckTarget> &targets,
    std::function<QDateTime(const QUuid &)> dateAddedFor)
{
    if (targets.isEmpty()) {
        emit checkUpdatesFinished(0, 0);
        return;
    }

    // One request per game first: which of its mods changed at all in the
    // last month. On a 409-mod list that one reply rules out almost every
    // row, so the paced per-mod sweep below has a handful to do instead of
    // 409. See update_sweep.h.
    QSet<QString> games;
    for (const CheckTarget &t : targets)
        if (!t.game.isEmpty()) games.insert(t.game);
    if (games.isEmpty()) { runCheck(targets, dateAddedFor); return; }

    auto pre      = std::make_shared<Prefilter>();
    pre->targets  = targets;
    pre->pending  = int(games.size());
    const int nGames = int(games.size());
    for (const QString &game : games) {
        QNetworkReply *reply =
            m_client->requestUpdatedMods(game, QString::fromLatin1(update_sweep::kPeriod));
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, game, pre, nGames, dateAddedFor] {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                if (const auto m = NexusClient::parseUpdatedMods(reply->readAll())) {
                    pre->activity.insert(game, *m);
                    pre->usable.insert(game);
                } else {
                    qCWarning(logging::lcNexus)
                        << "update sweep: unreadable reply for" << game
                        << "- asking about every mod instead";
                }
            } else {
                qCWarning(logging::lcNexus)
                    << "update sweep: no reply for" << game << reply->errorString()
                    << "- asking about every mod instead";
            }
            if (--pre->pending > 0) return;

            const QDateTime now = QDateTime::currentDateTime();
            QList<CheckTarget> keep;
            for (const CheckTarget &t : std::as_const(pre->targets)) {
                const bool usable = pre->usable.contains(t.game);
                const auto &byId  = pre->activity[t.game];
                const auto it     = byId.constFind(t.modId);
                if (update_sweep::mustAsk(t.lastCheckedAt, now, usable,
                                          it != byId.constEnd(),
                                          it != byId.constEnd() ? *it : 0))
                    keep.append(t);
            }
            qCInfo(logging::lcNexus).nospace()
                << "update sweep: " << pre->targets.size() << " installed, "
                << keep.size() << " need asking ("
                << (pre->targets.size() - keep.size()) << " ruled out by "
                << nGames << " request(s))";
            // The window counts from the real total, which is only known now.
            emit checkUpdatesProgress(0, int(keep.size()));
            runCheck(keep, dateAddedFor);
        });
    }
}

void NexusController::runCheck(
    const QList<CheckTarget> &targets,
    std::function<QDateTime(const QUuid &)> dateAddedFor)
{
    if (targets.isEmpty()) {
        emit checkUpdatesFinished(0, 0);
        return;
    }

    auto state = std::make_shared<CheckRun>();
    state->queue = targets;
    state->total = int(targets.size());

    state->pump = new QTimer(this);
    state->pump->setInterval(kCheckSpacingMs);
    connect(state->pump, &QTimer::timeout, this,
            [this, state, dateAddedFor] { pumpChecks(state, dateAddedFor); });
    state->pump->start();
    // One straight away, so a single-mod check does not wait for a tick.
    pumpChecks(state, dateAddedFor);
}

void NexusController::pumpChecks(const std::shared_ptr<CheckRun> &state,
                                 std::function<QDateTime(const QUuid &)> dateAddedFor)
{
    if (state->finished) return;

    // Everything asked and everything answered.
    if (state->queue.isEmpty() && state->inFlight == 0) {
        state->finished = true;
        if (state->pump) { state->pump->stop(); state->pump->deleteLater(); state->pump = nullptr; }
        emit checkUpdatesFinished(state->found, state->superseded);
        return;
    }
    if (state->queue.isEmpty()) return;             // draining the last replies
    if (state->inFlight >= kCheckMaxInFlight) return;   // let them land first

    // Spending the last of the allowance on a scan leaves the user unable to
    // download what it finds. The rows not reached keep their previous marks
    // and their previous stamp, so the next run carries on from here.
    if (update_budget::stopForQuota(m_client->rateLimit().remaining())) {
        const int left = m_client->rateLimit().remaining();
        state->queue.clear();
        emit checkUpdatesStopped(state->done, state->total, left, false);
        if (state->inFlight == 0) pumpChecks(state, dateAddedFor);
        return;
    }

    const CheckTarget t = state->queue.takeFirst();
    ++state->inFlight;
    QNetworkReply *reply = m_client->requestModInfo(t.game, t.modId);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, row = t.row, modId = t.modId, fileId = t.fileId,
             state, dateAddedFor]() {
        reply->deleteLater();
        --state->inFlight;
        ++state->done;
        emit checkUpdatesProgress(state->done, state->total);
        // A refusal used to be dropped here, which made it indistinguishable
        // from "this mod has no update" - so a rate-limited check could report
        // that everything was up to date without having asked anything.
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429 && !state->rateLimited) {
            state->rateLimited = true;
            state->queue.clear();
            emit checkUpdatesStopped(state->done, state->total,
                                     m_client->rateLimit().remaining(), true);
        }
        if (reply->error() == QNetworkReply::NoError) {
            const auto info = NexusClient::parseModInfo(reply->readAll());
            if (info) {
                // Asked and answered. Only on a reply that parsed: a network
                // error or a refusal must never be written down as a check,
                // or it would hide a real update until the window passed.
                emit modChecked(row);
            }
            if (info && info->updatedTimestamp > 0) {
                const QDateTime modUpdated =
                    QDateTime::fromSecsSinceEpoch(info->updatedTimestamp);
                const QDateTime dateAdded  = dateAddedFor(row);
                if (dateAdded.isValid() && modUpdated > dateAdded) {
                    emit updateFoundForItem(row);
                    ++state->found;
                }
            }
            // The timestamp cannot see a file retired on a page that has
            // not changed since - so for a row that knows its file, ask
            // the page's file list too. The numeric game id it needs is in
            // this reply. Counted in before this reply is counted out, so
            // the run cannot finish in between. This one is unauthenticated
            // (v2 GraphQL), so it costs no allowance and is not paced.
            if (info && fileId > 0 && info->gameIdNumeric > 0) {
                ++state->inFlight;
                QNetworkReply *files =
                    m_client->requestModFilesV2(info->gameIdNumeric, modId);
                connect(files, &QNetworkReply::finished, this,
                        [this, files, row, fileId, state, dateAddedFor]() {
                    files->deleteLater();
                    --state->inFlight;
                    if (files->error() == QNetworkReply::NoError) {
                        const auto list = NexusClient::parseModFilesV2(files->readAll());
                        if (list) {
                            const auto v = file_status::judge(fileId, *list);
                            if (v.state == file_status::Verdict::State::Superseded) {
                                emit fileSupersededForItem(row, v);
                                ++state->superseded;
                            }
                        }
                    }
                    pumpChecks(state, dateAddedFor);
                });
            }
        }
        pumpChecks(state, dateAddedFor);
    });
}

void NexusController::fetchRequirementsPage(
    int gameIdNumeric, int modId, int offset,
    std::shared_ptr<QList<deps::TableRequirement>> acc, int pagesSoFar,
    std::function<void(const QList<deps::TableRequirement> &)> done)
{
    QNetworkReply *rq =
        m_client->requestModRequirements(gameIdNumeric, modId, offset);
    connect(rq, &QNetworkReply::finished, this,
            [this, rq, gameIdNumeric, modId, offset, acc, pagesSoFar, done]() {
        rq->deleteLater();
        NexusClient::PageInfo page;
        if (rq->error() == QNetworkReply::NoError) {
            const auto rows =
                NexusClient::parseModRequirements(rq->readAll(), &page);
            if (rows) {
                for (const auto &r : *rows) {
                    if (r.external || r.modId <= 0) continue;
                    acc->append({r.modId, r.name, r.notes});
                }
            }
        }
        // Advance by what the server SENT, not by what was kept or asked
        // for: a dropped row must not make the next page overlap this one,
        // and a server that caps lower than kV2PageCap still pages cleanly.
        // The page limit is a backstop against a server that keeps
        // answering - 20 pages is 1,600 rows, far past any real table.
        const int next = offset + page.received;
        constexpr int kMaxPages = 20;
        if (page.received > 0 && next < page.total
            && pagesSoFar + 1 < kMaxPages) {
            fetchRequirementsPage(gameIdNumeric, modId, next, acc,
                                  pagesSoFar + 1, done);
            return;
        }
        done(*acc);
    });
}
