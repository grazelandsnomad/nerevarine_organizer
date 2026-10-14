#ifndef DOWNLOADQUEUE_H
#define DOWNLOADQUEUE_H

// DownloadQueue
//
// Owns the download-queue dock, one-at-a-time scheduling, active-download
// tracking, and crash-recovery for rows stuck at "installing".
//
// Lifecycle:
//   1. Construct with the four injected deps.
//   2. setup(mainWindow) once during init (after menuBar exists); it adds the
//      dock and returns the visibility-toggle QAction for the Settings menu.
//   3. Forward NXM/Nexus requests to fetchDownloadLink().
//   4. Connect extractionRequested/saveRequested/statusMessage to MainWindow.
//
// Nothing here calls back into MainWindow directly: coupling is via signals so
// the class tests in isolation. A row is named by its id (mod_row::rowId) and
// never held as an item: the queue resolves the id to a live row through the
// resolver MainWindow installs at the moment it needs to read or write one,
// and a row that is gone by then resolves to nothing.

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QUuid>

#include <functional>

class QDialog;

class QAction;
class QDockWidget;
class QLabel;
class QListWidget;
class QListWidgetItem;
class ModListView;
#include "modrow.h"
class QMainWindow;
class QModelIndex;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QToolButton;
class QWidget;
class NexusClient;

class DownloadQueue : public QObject {
    Q_OBJECT
public:
    // modList: MainWindow's mod-list widget, for the crash-recovery sweep and
    // repaints; rows are otherwise reached by id.
    // net: shared QNetworkAccessManager.
    // nexus: Nexus API client for CDN-link requests.
    // parentWidget: parent for any QMessageBox dialogs.
    explicit DownloadQueue(ModListView           *modList,
                           QNetworkAccessManager *net,
                           NexusClient           *nexus,
                           QWidget               *parentWidget,
                           QObject               *parent = nullptr);

    // Once during init, after menuBar() exists. Adds the dock to `window`,
    // restores saved visibility, returns the toggle QAction for the menu.
    QAction *setup(QMainWindow *window);

    // Call when the active profile's mods dir changes.
    void setModsDir(const QString &dir) { m_modsDir = dir; }

    // Where to append download-integrity diagnostics (one line per corrupt
    // body/retry). MainWindow points this at a writable user-state path so the
    // log survives an AppImage's read-only mount. Empty: fall back to qWarning.
    void setDiagLogPath(const QString &path) { m_diagLogPath = path; }

    // How a row id becomes a row the queue may read or write. The default
    // searches the mod list; MainWindow widens it to the rows it parks across
    // a profile switch, which leave the list but are still being installed.
    // A row removed - or the list cleared by a profile switch or an import -
    // while its download runs resolves to nullptr from then on.
    using RowResolver = std::function<ModRow(const QUuid &)>;
    void setRowResolver(RowResolver resolve);

    // -- Entry points called by MainWindow ---

    // Full pipeline: fetch the CDN link from the Nexus API, then enqueue it.
    // `key`/`expires` are the signed nxms:// params from premium CDN links;
    // empty for the free-tier flow.
    void fetchDownloadLink(const QString &game, int modId, int fileId,
                           const QString &key, const QString &expires,
                           const QUuid &row);

    // Append a ready item to the queue and start processing if a slot is free
    // and the queue isn't paused.
    void enqueueDownload(const QUuid &row,
                         const QUrl &url, const QString &filename);

    // Cancel an in-flight or queued download. Active ones abort the
    // QNetworkReply; its finished() handler resets the row and advances
    // the queue. Queued-only entries are removed immediately.
    void cancelQueued(const QUuid &row);

    // Crash-recovery, call on startup after loadModList(). Resets any
    // row stuck at InstallStatus=2 with no live reply.
    void cleanStaleDownloads();

    // Returns true when `row` has an in-flight QNetworkReply.
    bool isDownloadActive(const QUuid &row) const;

    // Returns true while `row` is in the download queue at all -
    // waiting OR actively downloading. False once the download handed off to
    // verification/extraction (the row is then the InstallController's).
    bool isQueued(const QUuid &row) const;

    // True when nothing is queued or active; callers gate destructive FS ops on
    // this. Must check m_active too: an integrity-retry requeue briefly clears
    // the m_queue entry while a reply is still live, so m_queue alone would read
    // "idle" and let a move/consolidate yank the mods dir out from under it.
    bool isEmpty() const { return m_queue.isEmpty() && m_active.isEmpty(); }

signals:
    // On successful download; triggers MainWindow::verifyAndExtract.
    void extractionRequested(const QString &archivePath,
                             const QUuid &row);

    // When the queue changes a modlist item that needs persisting; connects to
    // MainWindow::saveModList().
    void saveRequested();

    // Routes to MainWindow::statusBar()->showMessage(). timeoutMs == 0 stays
    // until replaced.
    void statusMessage(const QString &msg, int timeoutMs = 0);

    // Nexus rejected the apikey (HTTP 401) on a download-link request. Distinct
    // from the premium-required fallback: the key itself is bad/expired, not the
    // account tier - even the free nxm:// flow needs a valid key. MainWindow
    // offers to open the API-key dialog instead of the "need Premium" loop.
    void apiKeyRejected();

private slots:
    void onQueuePauseToggled();
    void onQueueRowsMoved(const QModelIndex &srcParent, int srcStart, int srcEnd,
                          const QModelIndex &dstParent, int dstRow);

private:
    // One per pending/active download. Order matches the (draggable) UI rows.
    struct QueuedDownload {
        QUuid            row;                     // the mod-list row, by id
        QListWidgetItem *queueRow    = nullptr;   // row in m_list
        QProgressBar    *progressBar = nullptr;   // widget inside queueRow
        QLabel          *statusLabel = nullptr;   // widget inside queueRow
        QUrl             url;
        QString          filename;
        qint64           receivedBytes = 0;
        qint64           totalBytes    = 0;
        bool             active        = false;
    };

    void processDownloadQueue();
    void downloadFile(const QUrl &url, const QString &filename,
                      const QUuid &row);

    // Is a file already sitting where this download would land, and is it
    // worth handing to the installer instead of transferring it again?
    //
    // Deliberately lenient, because it does not have to be strict: reuse
    // still goes through the normal verify-then-extract path, so a truncated
    // or corrupt file fails md5/size verification, gets deleted there, and
    // the next attempt downloads fresh. The worst case is today's behaviour.
    bool haveUsableArchive(const QString &path, const QUuid &row) const;

    // The integrity gate's slow half, on a worker: a structural `7z t`
    // (download_integrity), then afterIntegrityGate on the UI thread.
    void testArchiveThenContinue(const QUuid &row, const QUrl &url,
                                 const QString &filename, const QString &savePath);
    // After the gate: `problem` empty -> hand the archive to extraction; else
    // retry the download once, then keep the bad copy and say so.
    void afterIntegrityGate(const QUuid &row, const QUrl &url,
                            const QString &filename, const QString &savePath,
                            const QString &problem);
    // Rows whose download is done and whose archive a worker is testing,
    // with the file being tested. Part of the queue as far as isQueued and
    // cancelQueued are concerned.
    QHash<QUuid, QString> m_testing;

    // Append one line to m_diagLogPath (or qWarning if unset).
    void writeDiag(const QString &line) const;
    void updateQueueRowProgress(const QUuid &row,
                                qint64 received, qint64 total);
    void removeQueueRow(const QUuid &row);
    void updateQueueTotals();

    // Put a row back to "not installed" after a failed download: clears the
    // spinner state so the user can retry, and emits saveRequested(). Every
    // failure exit from downloadFile() must go through this - a path that skips
    // it leaves the row spinning forever, and because processDownloadQueue()
    // marks the entry active *before* starting it, the (single) concurrency
    // slot stays occupied and the whole queue wedges.
    void resetPlaceholderToIdle(const QUuid &row);

    // The live row for an id, or nullptr. See setRowResolver.
    ModRow rowFor(const QUuid &row) const;
    RowResolver m_resolve;

    // -- Injected (owned by MainWindow, outlive us) ---
    ModListView           *m_modList;
    QNetworkAccessManager *m_net;
    NexusClient           *m_nexus;
    QWidget               *m_parentWidget;
    QString                m_modsDir;

    // -- Queue state ---
    QList<QueuedDownload>                   m_queue;
    bool                                    m_paused = false;
    static constexpr int                    kMaxConcurrent = 1;
    QHash<QUuid, QNetworkReply*> m_active;    // in-flight replies
    QHash<QUuid, QElapsedTimer>  m_startTime; // per-download ETA
    QHash<QUuid, int>            m_dlAttempts;// integrity-retry count

    // See setDiagLogPath().
    QString m_diagLogPath;

    // Open "manual download" hint dialog, one per row. Auto-closed when a new
    // download for the same row gets queued via nxm://.
    QHash<QUuid, QPointer<QDialog>> m_manualDlBoxes;

    // -- Dock UI (owned after setup()) ---
    QDockWidget *m_dock       = nullptr;
    QListWidget *m_list       = nullptr;  // queue rows
    QLabel      *m_totalLabel = nullptr;
    QToolButton *m_pauseBtn   = nullptr;
};

#endif // DOWNLOADQUEUE_H
