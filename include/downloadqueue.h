#ifndef DOWNLOADQUEUE_H
#define DOWNLOADQUEUE_H

// DownloadQueue
//
// Manages the download-queue dock panel, one-at-a-time download scheduling,
// active-download tracking, and crash-recovery for stuck "installing" rows.
//
// Lifecycle:
//   1. Construct with the four injected dependencies.
//   2. Call setup(mainWindow) once during MainWindow init (after menuBar
//      exists).  setup() adds the dock to the window and returns the
//      QAction that toggles its visibility, so the caller can insert it
//      into the Settings menu.
//   3. Forward NXM / Nexus-download requests to fetchDownloadLink().
//   4. Connect the three signals (extractionRequested, saveRequested,
//      statusMessage) to the corresponding MainWindow slots.
//
// None of the DownloadQueue methods call back into MainWindow directly -
// all coupling is through signals so the class is testable in isolation.

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QDialog;

class QAction;
class QDockWidget;
class QLabel;
class QListWidget;
class QListWidgetItem;
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
    // `modList`     - the mod-list widget owned by MainWindow; items are shared.
    // `net`         - the shared QNetworkAccessManager.
    // `nexus`       - the thin Nexus API client for CDN-link requests.
    // `parentWidget`- used as the parent for any QMessageBox dialogs.
    explicit DownloadQueue(QListWidget           *modList,
                           QNetworkAccessManager *net,
                           NexusClient           *nexus,
                           QWidget               *parentWidget,
                           QObject               *parent = nullptr);

    // Call once during MainWindow init, after menuBar() exists.
    // Adds the dock to `window`, restores its saved visibility, and returns
    // the toggle QAction so the caller can insert it into Settings menu.
    QAction *setup(QMainWindow *window);

    // Keep in sync whenever the active game profile's mods directory changes.
    void setModsDir(const QString &dir) { m_modsDir = dir; }

    // -- Entry points called by MainWindow ---

    // Full Nexus-download pipeline: fetches the CDN link from the Nexus API
    // and then enqueues the resulting URL.  `key`/`expires` are the signed
    // nxms:// params from premium CDN links; pass empty strings for the
    // standard free-tier flow.
    void fetchDownloadLink(const QString &game, int modId, int fileId,
                           const QString &key, const QString &expires,
                           QListWidgetItem *placeholder);

    // Append a ready-to-download item at the end of the queue and kick off
    // processing if a slot is available and the queue is not paused.
    void enqueueDownload(QListWidgetItem *placeholder,
                         const QUrl &url, const QString &filename);

    // Cancel an in-flight or queued download for `placeholder`.
    // For active downloads the underlying QNetworkReply is aborted; its
    // finished() handler then resets the placeholder and advances the queue.
    // For queued-only entries the row is removed immediately.
    void cancelQueued(QListWidgetItem *placeholder);

    // Crash-recovery: call on startup after loadModList().
    // Resets any placeholder stuck at InstallStatus=2 with no live reply.
    void cleanStaleDownloads();

    // Returns true when `placeholder` has an in-flight QNetworkReply.
    bool isDownloadActive(QListWidgetItem *placeholder) const;

    // True when there are no queued or active downloads - used by callers
    // that need to gate destructive filesystem operations on queue idleness.
    bool isEmpty() const { return m_queue.isEmpty(); }

signals:
    // Emitted when a download finishes successfully - triggers verifyAndExtract
    // in MainWindow.
    void extractionRequested(const QString &archivePath,
                             QListWidgetItem *placeholder);

    // Emitted whenever the queue modifies a modlist item that should be
    // persisted - connects to MainWindow::saveModList().
    void saveRequested();

    // Routes status-bar messages through MainWindow::statusBar()->showMessage().
    // `timeoutMs` == 0 means "stay until replaced".
    void statusMessage(const QString &msg, int timeoutMs = 0);

private slots:
    void onQueuePauseToggled();
    void onQueueRowsMoved(const QModelIndex &srcParent, int srcStart, int srcEnd,
                          const QModelIndex &dstParent, int dstRow);

private:
    // One entry per pending or active download.  Order matches the UI row
    // order (user-draggable).
    struct QueuedDownload {
        QListWidgetItem *placeholder = nullptr;   // row in m_modList
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
                      QListWidgetItem *placeholder);
    void updateQueueRowProgress(QListWidgetItem *placeholder,
                                qint64 received, qint64 total);
    void removeQueueRow(QListWidgetItem *placeholder);
    void updateQueueTotals();

    // -- Injected (owned by MainWindow, outlive this object) ---
    QListWidget           *m_modList;
    QNetworkAccessManager *m_net;
    NexusClient           *m_nexus;
    QWidget               *m_parentWidget;
    QString                m_modsDir;

    // -- Queue state ---
    QList<QueuedDownload>                   m_queue;
    bool                                    m_paused = false;
    static constexpr int                    kMaxConcurrent = 1;
    QHash<QListWidgetItem*, QNetworkReply*> m_active;    // in-flight replies
    QHash<QListWidgetItem*, QElapsedTimer>  m_startTime; // per-download ETA

    // Open "manual download" hint dialog (one per placeholder). Auto-closed
    // when a new download for the same placeholder gets queued via nxm://.
    QHash<QListWidgetItem*, QPointer<QDialog>> m_manualDlBoxes;

    // -- Dock panel UI (owned after setup()) ---
    QDockWidget *m_dock       = nullptr;
    QListWidget *m_list       = nullptr;  // queue rows
    QLabel      *m_totalLabel = nullptr;
    QToolButton *m_pauseBtn   = nullptr;
};

#endif // DOWNLOADQUEUE_H
