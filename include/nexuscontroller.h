#pragma once

#include "nexusclient.h"   // for NexusClient::FileEntry in signal

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

class QListWidgetItem;

// NexusController - orchestration layer between MainWindow and NexusClient.
//
// NexusClient is a thin per-request wrapper (build a QNetworkRequest, hand
// back a QNetworkReply*).  NexusController sits on top and drives multi-
// request flows: "check every installed mod for a newer version", and will
// grow to own the download queue + placeholder-row lifecycle in follow-up
// commits.
//
// The controller never touches widgets directly.  It signals back out
// (updateFoundForItem, checkUpdatesFinished) and MainWindow's slots do the
// QListWidget manipulation + toast-style notifications.  The opaque
// QListWidgetItem* passed in/out is a token - the controller doesn't look
// inside it, so when we eventually factor out a ModEntry value type this
// layer won't have to change.

class NexusController : public QObject
{
    Q_OBJECT
public:
    explicit NexusController(NexusClient *client, QObject *parent = nullptr);

    struct CheckTarget {
        QListWidgetItem *item;   // opaque to the controller, echoed back in signals
        QString          game;
        int              modId;
    };

    // Kick off one requestModInfo per target in parallel.  For each target
    // whose Nexus-side "updated" timestamp is newer than `dateAddedFor(item)`,
    // updateFoundForItem(item) fires.  When every reply has come back
    // (success or error), checkUpdatesFinished(foundCount) fires exactly
    // once.  Calling this while a previous run is still in flight is
    // supported - each run has its own pending-counter.
    //
    // `dateAddedFor` lets the caller keep ModRole lookups on the UI side;
    // the controller gets a plain timestamp and stays widget-agnostic.
    void checkForUpdates(const QList<CheckTarget> &targets,
                         std::function<QDateTime(QListWidgetItem *)> dateAddedFor);

    // Fire-and-forget title fetch.  On success, titleFetched(item, name)
    // fires; parse errors / network errors are silently swallowed
    // (matches the existing behaviour - a missing title is non-fatal).
    // No-ops when the client has no API key.
    void fetchModTitle(QListWidgetItem *item, const QString &game, int modId);

    // Pre-fetch md5 + size_in_bytes for a specific file so post-download
    // verification has something to compare against.  Runs in parallel with
    // the actual download.  On success, expectedChecksumFetched(item, md5,
    // sizeBytes) fires; fetch errors / missing fields are silent (the
    // download still completes, the verify step just has less to check).
    void fetchExpectedChecksum(QListWidgetItem *item, const QString &game,
                               int modId, int fileId);

    // List every downloadable file for a Nexus mod.  On success,
    // fileListFetched(item, game, modId, files) fires; on network failure,
    // fileListFetchFailed(item, errorString).  Game/modId echo back so the
    // slot can hand them to the download queue without re-capturing.
    void fetchFileList(QListWidgetItem *item, const QString &game, int modId);

    // Fetch mod-info + derive the dependency scan (present/missing buckets)
    // in one shot.  `installedIdToUrl` is a caller-built snapshot of mods
    // of the same game already in the list (the controller stays widget-
    // agnostic - it never reads the list itself).  Emits
    // dependenciesScanned(item, game, modId, title, presentDeps, missingIds)
    // on success, dependencyScanFailed(item, game, modId) on network error
    // (the slot typically just proceeds with the install in that case).
    void scanDependencies(QListWidgetItem *item, const QString &game, int modId,
                          const QMap<int, QString> &installedIdToUrl);

    // Fetch the full version changelog for a mod.  On completion (success or
    // network error), changelogFetched fires; an empty list means no changelog
    // is available or the request failed.  No-ops when the client has no key.
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
    // Emitted when fetchChangelog completes; entries is empty on failure or when
    // the mod has no changelog.  game + modId are echoed back so the receiver
    // can filter without touching the (potentially-deleted) item pointer.
    void changelogFetched(QListWidgetItem *item,
                          const QString &game, int modId,
                          const QList<NexusClient::ChangelogEntry> &entries);

private:
    NexusClient *m_client;
};
