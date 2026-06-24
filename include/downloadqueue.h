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
// the class tests in isolation.

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
    // modList: MainWindow's mod-list widget; items are shared.
    // net: shared QNetworkAccessManager.
    // nexus: Nexus API client for CDN-link requests.
    // parentWidget: parent for any QMessageBox dialogs.
    explicit DownloadQueue(QListWidget           *modList,
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

    // -- Entry points called by MainWindow ---

    // Full pipeline: fetch the CDN link from the Nexus API, then enqueue it.
    // `key`/`expires` are the signed nxms:// params from premium CDN links;
    // empty for the free-tier flow.
    void fetchDownloadLink(const QString &game, int modId, int fileId,
                           const QString &key, const QString &expires,
                           QListWidgetItem *placeholder);

    // Append a ready item to the queue and start processing if a slot is free
    // and the queue isn't paused.
    void enqueueDownload(QListWidgetItem *placeholder,
                         const QUrl &url, const QString &filename);

    // Cancel an in-flight or queued download. Active ones abort the
    // QNetworkReply; its finished() handler resets the placeholder and advances
    // the queue. Queued-only entries are removed immediately.
    void cancelQueued(QListWidgetItem *placeholder);

    // Crash-recovery, call on startup after loadModList(). Resets any
    // placeholder stuck at InstallStatus=2 with no live reply.
    void cleanStaleDownloads();

    // Returns true when `placeholder` has an in-flight QNetworkReply.
    bool isDownloadActive(QListWidgetItem *placeholder) const;

    // True when nothing is queued or active; callers gate destructive FS ops on
    // this. Must check m_active too: an integrity-retry requeue briefly clears
    // the m_queue entry while a reply is still live, so m_queue alone would read
    // "idle" and let a move/consolidate yank the mods dir out from under it.
    bool isEmpty() const { return m_queue.isEmpty() && m_active.isEmpty(); }

signals:
    // On successful download; triggers MainWindow::verifyAndExtract.
    void extractionRequested(const QString &archivePath,
                             QListWidgetItem *placeholder);

    // When the queue changes a modlist item that needs persisting; connects to
    // MainWindow::saveModList().
    void saveRequested();

    // Routes to MainWindow::statusBar()->showMessage(). timeoutMs == 0 stays
    // until replaced.
    void statusMessage(const QString &msg, int timeoutMs = 0);

private slots:
    void onQueuePauseToggled();
    void onQueueRowsMoved(const QModelIndex &srcParent, int srcStart, int srcEnd,
                          const QModelIndex &dstParent, int dstRow);

private:
    // One per pending/active download. Order matches the (draggable) UI rows.
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

    // Integrity gate. Empty string if `savePath` is usable, else a short reason:
    // an error page served as 200, a tiny body, or (when Nexus gave no md5/size)
    // an archive whose magic is present but that fails a structural `7z t`.
    // `ctype` is the response Content-Type.
    QString archiveProblem(const QString &savePath, const QString &ctype,
                           QListWidgetItem *placeholder) const;

    // Append one line to m_diagLogPath (or qWarning if unset).
    void writeDiag(const QString &line) const;
    void updateQueueRowProgress(QListWidgetItem *placeholder,
                                qint64 received, qint64 total);
    void removeQueueRow(QListWidgetItem *placeholder);
    void updateQueueTotals();

    // -- Injected (owned by MainWindow, outlive us) ---
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
    QHash<QListWidgetItem*, int>            m_dlAttempts;// integrity-retry count

    // See setDiagLogPath().
    QString m_diagLogPath;

    // Open "manual download" hint dialog, one per placeholder. Auto-closed when
    // a new download for the same placeholder gets queued via nxm://.
    QHash<QListWidgetItem*, QPointer<QDialog>> m_manualDlBoxes;

    // -- Dock UI (owned after setup()) ---
    QDockWidget *m_dock       = nullptr;
    QListWidget *m_list       = nullptr;  // queue rows
    QLabel      *m_totalLabel = nullptr;
    QToolButton *m_pauseBtn   = nullptr;
};

#endif // DOWNLOADQUEUE_H
