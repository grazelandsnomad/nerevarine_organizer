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
        emit dependenciesScanned(item, game, modId,
                                 info->name,
                                 parsed.presentUrls,
                                 parsed.missingModIds);
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
            emit expectedChecksumFetched(item, f.md5, f.sizeBytes);
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
        // Emit an empty list on failure so the dialog exits its loading state.
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

    // Per-run shared state.  Wrap in shared_ptr so the inner lambda owns a
    // copy - plays nicely with multiple overlapping runs and guarantees
    // cleanup even if `this` is destroyed mid-flight (replies are parented
    // to `this`, so they'd get cancelled, but the counter still drains).
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
