// src/downloadqueue.cpp
//
// DownloadQueue - download-queue dock panel, scheduling, and crash-recovery.
// Extracted from mainwindow.cpp.  See include/downloadqueue.h for the public
// API contract.

#include "downloadqueue.h"
#include "modroles.h"
#include "nexusclient.h"
#include "settings.h"
#include "translator.h"

#include <QAbstractButton>
#include <QAction>
#include <QPushButton>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QMenuBar>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressBar>
#include <QToolButton>
#include <QVBoxLayout>

// File-local helpers

static QString dqFmtBytes(qint64 b)
{
    if (b < 0) return QStringLiteral("?");
    const double MB = 1024.0 * 1024.0;
    const double GB = MB * 1024.0;
    if (b >= GB) return QString::number(b / GB, 'f', 2) + " GB";
    if (b >= MB) return QString::number(b / MB, 'f', 1) + " MB";
    return QString::number(b / 1024.0, 'f', 0) + " KB";
}

// Construction

DownloadQueue::DownloadQueue(QListWidget           *modList,
                             QNetworkAccessManager *net,
                             NexusClient           *nexus,
                             QWidget               *parentWidget,
                             QObject               *parent)
    : QObject(parent)
    , m_modList(modList)
    , m_net(net)
    , m_nexus(nexus)
    , m_parentWidget(parentWidget)
{}

// setup() - creates the dock panel and returns its toggle action

QAction *DownloadQueue::setup(QMainWindow *window)
{
    m_dock = new QDockWidget(T("queue_dock_title"), window);
    m_dock->setObjectName("DownloadQueueDock");
    m_dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea
                            | Qt::BottomDockWidgetArea);

    auto *content = new QWidget(m_dock);
    auto *vbox    = new QVBoxLayout(content);
    vbox->setContentsMargins(6, 6, 6, 6);
    vbox->setSpacing(4);

    // Header: pause toggle on the left, aggregate counter on the right
    auto *header = new QHBoxLayout;
    m_pauseBtn = new QToolButton(content);
    m_pauseBtn->setCheckable(true);
    m_pauseBtn->setText(T("queue_pause"));
    m_pauseBtn->setToolTip(T("queue_pause_tooltip"));
    connect(m_pauseBtn, &QToolButton::toggled,
            this, &DownloadQueue::onQueuePauseToggled);
    m_totalLabel = new QLabel(content);
    m_totalLabel->setStyleSheet("color: #666;");
    header->addWidget(m_pauseBtn);
    header->addStretch();
    header->addWidget(m_totalLabel);
    vbox->addLayout(header);

    // Queue rows - drag-reorder within, no external drops
    m_list = new QListWidget(content);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAlternatingRowColors(true);
    m_list->setSpacing(1);
    connect(m_list->model(), &QAbstractItemModel::rowsMoved,
            this, &DownloadQueue::onQueueRowsMoved);
    vbox->addWidget(m_list, 1);

    m_dock->setWidget(content);
    window->addDockWidget(Qt::RightDockWidgetArea, m_dock);

    // Restore the dock's visibility from the previous session.  Default to
    // visible on first run so new users discover the queue the first time
    // they install a mod; afterwards, whatever they last set sticks.
    bool wantVisible = Settings::queueVisible(/*defaultVisible=*/true);
    m_dock->setVisible(wantVisible);

    // Persist any visibility change across launches.  Connect *after* the
    // initial setVisible so the restore itself doesn't overwrite the stored
    // preference.
    connect(m_dock, &QDockWidget::visibilityChanged, this, [](bool v) {
        Settings::setQueueVisible(v);
    });

    QAction *toggleAct = m_dock->toggleViewAction();
    toggleAct->setText(T("menu_toggle_queue"));

    updateQueueTotals();
    return toggleAct;
}

// fetchDownloadLink - Nexus CDN link → enqueueDownload

void DownloadQueue::fetchDownloadLink(const QString &game, int modId, int fileId,
                                      const QString &key, const QString &expires,
                                      QListWidgetItem *placeholder)
{
    QNetworkReply *reply =
        m_nexus->requestDownloadLink(game, modId, fileId, key, expires);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, placeholder, game, modId, fileId]() {
        reply->deleteLater();

        // Free Nexus accounts can't use the bare /download_link.json
        // endpoint - that's a Premium-only feature. The only way to get a
        // download link without Premium is to click "Mod Manager Download"
        // on the website, which fires an nxm:// URL with a short-lived
        // signed key+expires that we can then submit to the same endpoint.
        // So when the API rejects us we open the mod's Files tab focused
        // on the right file; one click on the website and the nxm:// flow
        // takes over from there. If the user then prefers to drag the
        // archive in instead, the modId match in installLocalArchive
        // re-adopts the placeholder cleanly.
        auto offerManualFallback = [&]() {
            const QString modUrl =
                QString("https://www.nexusmods.com/%1/mods/%2?tab=files")
                    .arg(game).arg(modId);
            (void)fileId;  // intentional: let the user pick on the Files tab

            // Non-modal QDialog so the nxm:// callback can close it once
            // the user clicks "Mod Manager Download" on the website.  Plain
            // QDialog (rather than QMessageBox) so the "Open Nexus page"
            // action button doesn't auto-close - we want it to stay up
            // until either the user dismisses it or the download starts.
            auto *dlg = new QDialog(m_parentWidget);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->setWindowTitle(T("manual_dl_title"));
            auto *v = new QVBoxLayout(dlg);
            auto *body = new QLabel(T("manual_dl_body"), dlg);
            body->setTextFormat(Qt::RichText);
            body->setWordWrap(true);
            body->setMinimumWidth(420);
            body->setOpenExternalLinks(true);
            v->addWidget(body);
            auto *btns = new QDialogButtonBox(dlg);
            auto *openBtn = btns->addButton(T("manual_dl_open_nexus"),
                                            QDialogButtonBox::ActionRole);
            auto *closeBtn = btns->addButton(T("manual_dl_later"),
                                             QDialogButtonBox::RejectRole);
            v->addWidget(btns);
            QObject::connect(openBtn, &QAbstractButton::clicked, dlg,
                             [modUrl]() {
                QDesktopServices::openUrl(QUrl(modUrl));
            });
            QObject::connect(closeBtn, &QAbstractButton::clicked,
                             dlg, &QDialog::reject);
            if (placeholder) m_manualDlBoxes.insert(placeholder, dlg);
            dlg->show();

            // Reset the placeholder: drop the "installing" spinner so the
            // user can see the row, click Install again, OR just drop the
            // manually-downloaded archive onto the window - either path
            // now works cleanly.
            if (placeholder) {
                placeholder->setData(ModRole::InstallStatus, 0);
                placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                      Qt::ItemIsDragEnabled |
                                      Qt::ItemIsUserCheckable);
                QString name = placeholder->data(ModRole::CustomName).toString();
                if (name.isEmpty()) name = placeholder->text();
                if (name.startsWith(QStringLiteral("⠋ "))) name = name.mid(2);
                placeholder->setText(name);
                emit saveRequested();
            }
            emit statusMessage(T("manual_dl_status"), 6000);
        };

        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            offerManualFallback();
            return;
        }
        const auto uriResult = NexusClient::parseDownloadUri(body);
        if (!uriResult) {
            // "not-array" is the common real-world case: Nexus returned
            // 200 OK with an explanatory object instead of a CDN array
            // (typical for free accounts hitting the Premium-only one-
            // click endpoint, or files with mod-manager downloads
            // disabled).
            offerManualFallback();
            return;
        }
        QByteArray data = body;
        QUrl dlUrl(uriResult.value());
        QString filename = QUrl::fromPercentEncoding(
            QFileInfo(dlUrl.path()).fileName().toUtf8());
        if (filename.isEmpty()) filename = "mod_download";
        enqueueDownload(placeholder, dlUrl, filename);
    });
}

// enqueueDownload - append to queue, build UI row

void DownloadQueue::enqueueDownload(QListWidgetItem *placeholder,
                                    const QUrl      &url,
                                    const QString   &filename)
{
    // A new download for this placeholder means the manual-fallback
    // dialog (if any) served its purpose - the nxm:// flow took over.
    if (placeholder) {
        if (auto box = m_manualDlBoxes.take(placeholder); box) box->close();
    }

    QueuedDownload q;
    q.placeholder = placeholder;
    q.url         = url;
    q.filename    = filename;

    // Build the row widget (name + progress bar + cancel button)
    auto *rowW = new QWidget(m_list);
    auto *hb   = new QHBoxLayout(rowW);
    hb->setContentsMargins(6, 2, 6, 2);
    hb->setSpacing(6);

    auto *nameLbl = new QLabel(filename, rowW);
    nameLbl->setMinimumWidth(120);
    nameLbl->setWordWrap(false);
    nameLbl->setToolTip(filename);

    auto *bar = new QProgressBar(rowW);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(true);
    bar->setFormat(T("queue_status_waiting"));
    bar->setMaximumWidth(180);
    bar->setMinimumWidth(100);

    auto *status = new QLabel(dqFmtBytes(0), rowW);
    status->setStyleSheet("color: #888; font-size: 9pt;");
    status->setMinimumWidth(70);

    auto *cancelBtn = new QToolButton(rowW);
    cancelBtn->setText(QStringLiteral("\u2715")); // ✕
    cancelBtn->setToolTip(T("queue_cancel_tooltip"));
    cancelBtn->setAutoRaise(true);
    connect(cancelBtn, &QToolButton::clicked, this,
            [this, placeholder]{ cancelQueued(placeholder); });

    hb->addWidget(nameLbl, 1);
    hb->addWidget(status);
    hb->addWidget(bar);
    hb->addWidget(cancelBtn);

    auto *row = new QListWidgetItem(m_list);
    row->setSizeHint(rowW->sizeHint());
    m_list->setItemWidget(row, rowW);

    q.queueRow    = row;
    q.progressBar = bar;
    q.statusLabel = status;
    m_queue.append(q);

    // Don't force the dock open here - the user's last-chosen visibility
    // (persisted in setup's visibilityChanged hook) wins.  They can always
    // reach it from the Settings menu's "Show Download Queue".
    updateQueueTotals();
    processDownloadQueue();
}

// processDownloadQueue - start next waiting item if a slot is available

void DownloadQueue::processDownloadQueue()
{
    if (m_paused) return;

    int activeCount = 0;
    for (const auto &q : m_queue) if (q.active) ++activeCount;
    if (activeCount >= kMaxConcurrent) return;

    for (int i = 0; i < m_queue.size(); ++i) {
        auto &q = m_queue[i];
        if (q.active) continue;
        q.active = true;
        if (q.progressBar) {
            q.progressBar->setFormat(T("queue_status_downloading"));
            q.progressBar->setValue(0);
        }
        downloadFile(q.url, q.filename, q.placeholder);
        break;
    }
    updateQueueTotals();
}

// cancelQueued - abort active reply or remove queued entry

void DownloadQueue::cancelQueued(QListWidgetItem *placeholder)
{
    int idx = -1;
    for (int i = 0; i < m_queue.size(); ++i)
        if (m_queue[i].placeholder == placeholder) { idx = i; break; }
    if (idx < 0) return;

    const bool wasActive = m_queue[idx].active;

    if (wasActive) {
        // Let the reply's finished() handler clean up the placeholder and
        // queue row when the abort propagates.
        if (QNetworkReply *reply = m_active.value(placeholder))
            reply->abort();
        return;
    }

    // Queued-only: remove row + entry, reset placeholder to not-installed.
    removeQueueRow(placeholder);
    if (m_modList->indexFromItem(placeholder).isValid()) {
        placeholder->setData(ModRole::InstallStatus, 0);
        placeholder->setData(ModRole::DownloadProgress, QVariant());
        placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                              Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable);
        QString name = placeholder->data(ModRole::CustomName).toString();
        if (name.isEmpty())
            name = QFileInfo(placeholder->data(ModRole::ModPath).toString())
                       .fileName();
        if (!name.isEmpty()) placeholder->setText(name);
        emit saveRequested();
    }
    emit statusMessage(T("status_queue_item_cancelled"), 3000);
}

// isDownloadActive - true when placeholder has an in-flight reply

bool DownloadQueue::isDownloadActive(QListWidgetItem *placeholder) const
{
    return m_active.contains(placeholder);
}

// cleanStaleDownloads - crash-recovery on startup

void DownloadQueue::cleanStaleDownloads()
{
    // A mod still at InstallStatus=2 with no live reply is a relic from a
    // previous crash or forced quit.  Reset it to not-installed so the user
    // can retry; leftover partial archives (if any) will be overwritten the
    // next time the same file is downloaded.
    int healed = 0;
    for (int i = 0; i < m_modList->count(); ++i) {
        auto *it = m_modList->item(i);
        if (it->data(ModRole::ItemType).toString() != ItemType::Mod) continue;
        if (it->data(ModRole::InstallStatus).toInt() != 2)           continue;
        if (m_active.contains(it))                                    continue;

        it->setData(ModRole::InstallStatus,   0);
        it->setData(ModRole::DownloadProgress, QVariant());
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                     Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable);

        // Strip the "⠋ installing (name)" display text back to the plain name.
        QString name = it->data(ModRole::CustomName).toString();
        if (name.isEmpty())
            name = QFileInfo(it->data(ModRole::ModPath).toString()).fileName();
        if (!name.isEmpty()) it->setText(name);
        ++healed;
    }
    if (healed > 0) emit saveRequested();
}

// Private: onQueuePauseToggled

void DownloadQueue::onQueuePauseToggled()
{
    m_paused = m_pauseBtn->isChecked();
    m_pauseBtn->setText(m_paused ? T("queue_resume") : T("queue_pause"));
    if (!m_paused) processDownloadQueue();
    updateQueueTotals();
}

// Private: onQueueRowsMoved - sync m_queue to new UI order

void DownloadQueue::onQueueRowsMoved(const QModelIndex &, int srcStart, int,
                                     const QModelIndex &, int dstRow)
{
    // Sync m_queue to match the new UI row order.  The user can drag both
    // active and queued rows; we preserve whichever is active wherever they
    // dropped it.
    if (srcStart < 0 || srcStart >= m_queue.size()) return;
    int dst = dstRow > srcStart ? dstRow - 1 : dstRow;
    dst = qBound(0, dst, m_queue.size() - 1);
    m_queue.move(srcStart, dst);
    processDownloadQueue();
}

// Private: updateQueueRowProgress

void DownloadQueue::updateQueueRowProgress(QListWidgetItem *placeholder,
                                            qint64 received, qint64 total)
{
    for (auto &q : m_queue) {
        if (q.placeholder != placeholder) continue;
        q.receivedBytes = received;
        q.totalBytes    = total;
        if (q.progressBar) {
            int pct = (total > 0) ? int(received * 100 / total) : 0;
            q.progressBar->setValue(pct);
            q.progressBar->setFormat(total > 0
                ? QStringLiteral("%p%")
                : T("queue_status_downloading"));
        }
        if (q.statusLabel) {
            q.statusLabel->setText(total > 0
                ? QString("%1 / %2").arg(dqFmtBytes(received), dqFmtBytes(total))
                : dqFmtBytes(received));
        }
        break;
    }
    updateQueueTotals();
}

// Private: removeQueueRow

void DownloadQueue::removeQueueRow(QListWidgetItem *placeholder)
{
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i].placeholder != placeholder) continue;
        QListWidgetItem *row = m_queue[i].queueRow;
        m_queue.removeAt(i);
        if (row && m_list) {
            int ri = m_list->row(row);
            if (ri >= 0) delete m_list->takeItem(ri);
        }
        break;
    }
    updateQueueTotals();
}

// Private: updateQueueTotals - refresh aggregate header label

void DownloadQueue::updateQueueTotals()
{
    if (!m_totalLabel) return;
    int    active = 0, waiting = 0;
    qint64 totalBytes = 0, receivedBytes = 0;
    for (const auto &q : m_queue) {
        if (q.active) ++active; else ++waiting;
        totalBytes    += q.totalBytes;
        receivedBytes += q.receivedBytes;
    }
    QString totals = (totalBytes > 0)
        ? QString("%1 / %2").arg(dqFmtBytes(receivedBytes), dqFmtBytes(totalBytes))
        : QString();
    m_totalLabel->setText(T("queue_totals")
        .arg(active).arg(waiting)
        .arg(totals.isEmpty() ? QStringLiteral("-") : totals));
}

// Private: downloadFile - issue the HTTP GET, wire progress + completion

void DownloadQueue::downloadFile(const QUrl    &downloadUrl,
                                  const QString &filename,
                                  QListWidgetItem *placeholder)
{
    QString savePath = QDir(m_modsDir).filePath(filename);

    QFile *file = new QFile(savePath, this);
    if (!file->open(QIODevice::WriteOnly)) {
        QMessageBox::warning(m_parentWidget, T("file_error_title"),
            T("file_error_write").arg(savePath));
        delete file;
        return;
    }

    placeholder->setData(ModRole::DownloadProgress, 0);

    // System notification: download started
    QProcess::startDetached("notify-send",
        {"-a", T("window_title"), "-i", "nerevarine_organizer",
         T("notif_download_title"), T("notif_download_body").arg(filename)});

    QNetworkRequest req(downloadUrl);
    QNetworkReply *reply = m_net->get(req);
    m_active[placeholder] = reply;

    // Start elapsed-timer for rate/ETA computation.
    m_startTime[placeholder].start();

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, placeholder, filename](qint64 received, qint64 total) {
        if (!m_modList->indexFromItem(placeholder).isValid()) return;
        int pct = (total > 0) ? static_cast<int>(received * 100 / total) : 0;
        placeholder->setData(ModRole::DownloadProgress, pct);
        m_modList->update(m_modList->indexFromItem(placeholder));
        updateQueueRowProgress(placeholder, received, total);

        // Status-bar: "Downloading foo.zip: 12.4 MB / 45.1 MB (27%) - ETA 0:42"
        auto fmtBytes = [](qint64 b) {
            if (b < 0) return QString("?");
            const double MB = 1024.0 * 1024.0;
            const double GB = MB * 1024.0;
            if (b >= GB) return QString::number(b / GB, 'f', 2) + " GB";
            if (b >= MB) return QString::number(b / MB, 'f', 1) + " MB";
            return QString::number(b / 1024.0, 'f', 0) + " KB";
        };
        auto fmtEta = [](qint64 s) {
            if (s < 0) return QString("--:--");
            if (s >= 3600)
                return QString("%1:%2:%3")
                    .arg(s / 3600)
                    .arg((s / 60) % 60, 2, 10, QChar('0'))
                    .arg(s % 60,        2, 10, QChar('0'));
            return QString("%1:%2")
                .arg(s / 60)
                .arg(s % 60, 2, 10, QChar('0'));
        };

        qint64 elapsedMs = m_startTime.value(placeholder).elapsed();
        qint64 etaSec = -1;
        if (total > 0 && received > 0 && elapsedMs > 500) {
            double rate = received * 1000.0 / elapsedMs;
            if (rate > 0) etaSec = static_cast<qint64>((total - received) / rate);
        }

        emit statusMessage(T("status_download_progress")
            .arg(filename,
                 fmtBytes(received),
                 total > 0 ? fmtBytes(total) : QString("?"))
            .arg(pct)
            .arg(fmtEta(etaSec)));
    });

    connect(reply, &QNetworkReply::readyRead, [reply, file]() {
        file->write(reply->readAll());
    });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, file, savePath, filename, placeholder]() {
        file->close();
        file->deleteLater();
        m_active.remove(placeholder);
        m_startTime.remove(placeholder);
        reply->deleteLater();
        removeQueueRow(placeholder);

        if (reply->error() == QNetworkReply::OperationCanceledError) {
            QFile::remove(savePath);
            if (m_modList->indexFromItem(placeholder).isValid()) {
                placeholder->setData(ModRole::InstallStatus, 0);
                placeholder->setData(ModRole::DownloadProgress, QVariant());
                placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                      Qt::ItemIsDragEnabled |
                                      Qt::ItemIsUserCheckable);
                emit saveRequested();
            }
            emit statusMessage(T("status_download_cancelled"), 3000);
            processDownloadQueue();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(savePath);
            if (m_modList->indexFromItem(placeholder).isValid()) {
                placeholder->setData(ModRole::InstallStatus, 0);
                placeholder->setData(ModRole::DownloadProgress, QVariant());
                placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                      Qt::ItemIsDragEnabled |
                                      Qt::ItemIsUserCheckable);
                emit saveRequested();
            }
            emit statusMessage(T("status_download_failed"), 4000);
            processDownloadQueue();
            return;
        }

        // Extracting phase - indeterminate progress indicator
        if (m_modList->indexFromItem(placeholder).isValid())
            placeholder->setData(ModRole::DownloadProgress, -1);

        emit statusMessage(T("status_downloaded_extracting").arg(filename));
        emit extractionRequested(savePath, placeholder);
        processDownloadQueue();

        // System notification: download complete
        QProcess::startDetached("notify-send",
            {"-a", T("window_title"), "-i", "nerevarine_organizer",
             T("notif_done_title"), T("notif_done_body").arg(filename)});
    });
}
