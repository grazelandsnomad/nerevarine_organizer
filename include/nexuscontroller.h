#pragma once

#include "nexusclient.h"   // for NexusClient::FileEntry in signal

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

class QListWidgetItem;

// Orchestration layer between MainWindow and NexusClient.
//
// NexusClient is a thin per-request wrapper (QNetworkRequest in,
// QNetworkReply* out). NexusController drives multi-request flows like
// "check every installed mod for a newer version".
//
// Never touches widgets: it signals out (updateFoundForItem,
// checkUpdatesFinished) and MainWindow's slots do the QListWidget work. The
// QListWidgetItem* is an opaque token; the controller never reads it.

class NexusController : public QObject
{
    Q_OBJECT
public:
    explicit NexusController(NexusClient *client, QObject *parent = nullptr);

    struct CheckTarget {
        QListWidgetItem *item;   // opaque token, echoed back in signals
        QString          game;
        int              modId;
    };

    // One requestModInfo per target, in parallel. Fires updateFoundForItem
    // when a target's Nexus "updated" timestamp beats dateAddedFor(item).
    // checkUpdatesFinished(foundCount) fires once after the last reply. Safe
    // to call while a previous run is in flight; each run has its own counter.
    // dateAddedFor keeps ModRole lookups on the UI side.
    void checkForUpdates(const QList<CheckTarget> &targets,
                         std::function<QDateTime(QListWidgetItem *)> dateAddedFor);

    // Fire-and-forget title fetch. Success -> titleFetched(item, name);
    // parse/network errors swallowed (a missing title is non-fatal).
    // No-op without an API key.
    void fetchModTitle(QListWidgetItem *item, const QString &game, int modId);

    // Pre-fetch md5 + size_in_bytes so post-download verify has something to
    // compare. Runs alongside the download. Success ->
    // expectedChecksumFetched(item, md5, sizeBytes); errors/missing fields
    // silent (download still completes, verify just checks less).
    void fetchExpectedChecksum(QListWidgetItem *item, const QString &game,
                               int modId, int fileId);

    // List a mod's downloadable files. Success ->
    // fileListFetched(item, game, modId, files); failure ->
    // fileListFetchFailed(item, errorString). game/modId echo back so the
    // slot can hand them to the download queue without re-capturing.
    void fetchFileList(QListWidgetItem *item, const QString &game, int modId);

    // Fetch mod-info and split deps into present/missing in one shot.
    // installedIdToUrl is a caller-built snapshot of same-game mods already in
    // the list. Success -> dependenciesScanned(item, game, modId, title,
    // presentDeps, missingIds); network error -> dependencyScanFailed (the slot
    // usually just proceeds with the install).
    void scanDependencies(QListWidgetItem *item, const QString &game, int modId,
                          const QMap<int, QString> &installedIdToUrl);

    // Fetch a mod's version changelog. changelogFetched fires on completion
    // either way; empty list means no changelog or the request failed.
    // No-op without an API key.
    void fetchChangelog(QListWidgetItem *item, const QString &game, int modId);

signals:
    void updateFoundForItem(QListWidgetItem *item);
    void checkUpdatesFinished(int foundCount);
    void titleFetched(QListWidgetItem *item, const QString &name);
    void expectedChecksumFetched(QListWidgetItem *item,
                                 const QString &md5, qint64 sizeBytes);
    void fileListFetched(QListWidgetItem *item, const QString &game, int modId,
                         const QList<NexusClient::FileEntry> &files);
    void fileListFetchFailed(QListWidgetItem *item, const QString &reason, int httpStatus);
    void dependenciesScanned(QListWidgetItem *item, const QString &game, int modId,
                             const QString &title,
                             const QStringList &presentDeps,
                             const QList<int> &missingModIds);
    void dependencyScanFailed(QListWidgetItem *item, const QString &game, int modId);
    // From fetchChangelog; entries empty on failure or no changelog. game +
    // modId echo back so the receiver can filter without touching the
    // (possibly-deleted) item pointer.
    void changelogFetched(QListWidgetItem *item,
                          const QString &game, int modId,
                          const QList<NexusClient::ChangelogEntry> &entries);

private:
    NexusClient *m_client;
};
