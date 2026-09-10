#include "nexuscontroller.h"

#include "deps_resolver.h"
#include "nexusclient.h"

#include <QDateTime>
#include <QNetworkReply>

#include <functional>
#include <memory>

NexusController::NexusController(NexusClient *client, QObject *parent)
    : QObject(parent), m_client(client) {}

void NexusController::fetchModTitle(QListWidgetItem *item,
                                    const QString &game, int modId)
{
    if (!m_client->hasApiKey() || !item) return;
    QNetworkReply *reply = m_client->requestModInfo(game, modId);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, item]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const auto info = NexusClient::parseModInfo(reply->readAll());
        if (!info || info->name.isEmpty()) return;
        emit titleFetched(item, info->name);
    });
}

void NexusController::scanDependencies(QListWidgetItem *item,
                                       const QString &game, int modId,
                                       const QMap<int, QString> &installedIdToUrl)
{
    if (!item) return;
    QNetworkReply *reply = m_client->requestModInfo(game, modId);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, item, game, modId, installedIdToUrl]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit dependencyScanFailed(item, game, modId);
            return;
        }
        const auto info = NexusClient::parseModInfo(reply->readAll());
        if (!info) {
            emit dependencyScanFailed(item, game, modId);
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
        const auto finish = [this, item, game, modId, title, parsed,
                             installedIdToUrl](
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
            emit dependenciesScanned(item, game, modId, title,
                                     parsed.presentUrls, missing, merged);
        };

        if (info->gameIdNumeric <= 0) { finish({}); return; }
        QNetworkReply *rq =
            m_client->requestModRequirements(info->gameIdNumeric, modId);
        connect(rq, &QNetworkReply::finished, this, [rq, finish]() {
            rq->deleteLater();
            QList<deps::TableRequirement> table;
            if (rq->error() == QNetworkReply::NoError) {
                const auto rows =
                    NexusClient::parseModRequirements(rq->readAll());
                if (rows) {
                    for (const auto &r : *rows) {
                        if (r.external || r.modId <= 0) continue;
                        table.append({r.modId, r.name, r.notes});
                    }
                }
            }
            finish(table);
        });
    });
}

void NexusController::fetchFileList(QListWidgetItem *item,
                                    const QString &game, int modId)
{
    if (!item) return;
    QNetworkReply *reply = m_client->requestModFiles(game, modId);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, item, game, modId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            emit fileListFetchFailed(item, reply->errorString(), status);
            return;
        }
        auto files = NexusClient::parseFilesList(reply->readAll());
        if (!files) {
            emit fileListFetchFailed(item, files.error().toString(), 200);
            return;
        }
        emit fileListFetched(item, game, modId, *files);
    });
}

void NexusController::fetchExpectedChecksum(QListWidgetItem *item,
                                            const QString &game,
                                            int modId, int fileId)
{
    if (!m_client->hasApiKey() || !item) return;
    QNetworkReply *reply = m_client->requestModFiles(game, modId);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, item, fileId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const auto files = NexusClient::parseFilesList(reply->readAll());
        if (!files) return;
        for (const auto &f : *files) {
            if (f.fileId != fileId) continue;
            QStringList siblings;
            siblings.reserve(int(files->size()));
            for (const auto &other : *files) siblings << other.name;
            emit modFileSiblings(item, f.name, siblings);
            emit expectedChecksumFetched(item, f.name, f.md5, f.sizeBytes);
            return;
        }
    });
}

void NexusController::fetchChangelog(QListWidgetItem *item,
                                     const QString &game, int modId)
{
    if (!m_client->hasApiKey() || !item) return;
    QNetworkReply *reply = m_client->requestChangelog(game, modId);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, item, game, modId]() {
        reply->deleteLater();
        // Empty list on failure so the dialog leaves its loading state.
        if (reply->error() != QNetworkReply::NoError) {
            emit changelogFetched(item, game, modId, {});
            return;
        }
        emit changelogFetched(item, game, modId,
                              NexusClient::parseChangelog(reply->readAll()));
    });
}

void NexusController::checkForUpdates(
    const QList<CheckTarget> &targets,
    std::function<QDateTime(QListWidgetItem *)> dateAddedFor)
{
    if (targets.isEmpty()) {
        emit checkUpdatesFinished(0);
        return;
    }

    // Per-run state in a shared_ptr so each lambda owns a copy: handles
    // overlapping runs and survives `this` dying mid-flight (replies are
    // parented to `this` so they'd cancel, but the counter still drains).
    struct RunState { int pending; int found; };
    auto state = std::make_shared<RunState>(
        RunState{static_cast<int>(targets.size()), 0});

    for (const auto &t : targets) {
        QNetworkReply *reply = m_client->requestModInfo(t.game, t.modId);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, item = t.item, state, dateAddedFor]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                const auto info = NexusClient::parseModInfo(reply->readAll());
                if (info && info->updatedTimestamp > 0) {
                    const QDateTime modUpdated =
                        QDateTime::fromSecsSinceEpoch(info->updatedTimestamp);
                    const QDateTime dateAdded  = dateAddedFor(item);
                    if (dateAdded.isValid() && modUpdated > dateAdded) {
                        emit updateFoundForItem(item);
                        ++state->found;
                    }
                }
            }
            if (--state->pending == 0)
                emit checkUpdatesFinished(state->found);
        });
    }
}
