// Download-queue dock panel, scheduling, crash recovery. API in the header.

#include "downloadqueue.h"
#include "archive_magic.h"
#include "modroles.h"
#include "nexusclient.h"
#include "settings.h"
#include "subprocess.h"
#include "translator.h"
#include "prompts.h"

#include <QAbstractButton>
#include <QAction>
#include <QPushButton>
#include <QDateTime>
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
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

static QString dqFmtBytes(qint64 b)
{
    if (b < 0) return QStringLiteral("?");
    const double MB = 1024.0 * 1024.0;
    const double GB = MB * 1024.0;
    if (b >= GB) return QString::number(b / GB, 'f', 2) + " GB";
    if (b >= MB) return QString::number(b / MB, 'f', 1) + " MB";
    return QString::number(b / 1024.0, 'f', 0) + " KB";
}

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

// Builds the dock, returns its toggle action.
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

    // pause toggle left, aggregate counter right
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

    // internal drag-reorder only, no external drops
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

    // visible by default on first run; after that the saved choice wins
    bool wantVisible = Settings::queueVisible(/*defaultVisible=*/true);
    m_dock->setVisible(wantVisible);

    // connect after the initial setVisible or the restore clobbers the saved pref
    connect(m_dock, &QDockWidget::visibilityChanged, this, [](bool v) {
        Settings::setQueueVisible(v);
    });

    QAction *toggleAct = m_dock->toggleViewAction();
    toggleAct->setText(T("menu_toggle_queue"));

    updateQueueTotals();
    return toggleAct;
}

// Resolve a Nexus CDN link, then enqueueDownload.
void DownloadQueue::fetchDownloadLink(const QString &game, int modId, int fileId,
                                      const QString &key, const QString &expires,
                                      QListWidgetItem *placeholder)
{
    QNetworkReply *reply =
        m_nexus->requestDownloadLink(game, modId, fileId, key, expires);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, placeholder, game, modId, fileId]() {
        reply->deleteLater();

        // Drop the placeholder out of its "installing" spinner so retry-Install
        // or a manual drag-drop both work again. Shared by the premium-fallback
        // and the bad-key (401) paths below.
        auto resetPlaceholder = [&]() {
            if (!placeholder) return;
            placeholder->setData(ModRole::InstallStatus, 0);
            placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                  Qt::ItemIsDragEnabled |
                                  Qt::ItemIsUserCheckable);
            QString name = placeholder->data(ModRole::CustomName).toString();
            if (name.isEmpty()) name = placeholder->text();
            if (name.startsWith(QStringLiteral("⠋ "))) name = name.mid(2);
            placeholder->setText(name);
            emit saveRequested();
        };

        // /download_link.json is Premium-only. Free accounts must click
        // "Mod Manager Download" on the site, which fires an nxm:// URL with a
        // short-lived signed key+expires we resubmit here. So on API rejection
        // open the Files tab; one click there hands off to the nxm:// flow.
        // If they drag the archive in instead, installLocalArchive re-adopts
        // the placeholder by modId match.
        auto offerManualFallback = [&]() {
            const QString modUrl =
                QString("https://www.nexusmods.com/%1/mods/%2?tab=files")
                    .arg(game).arg(modId);
            (void)fileId;  // let the user pick on the Files tab

            // Non-modal so the nxm:// callback can close it once the download
            // starts. Plain QDialog, not QMessageBox, so the action button
            // doesn't auto-close.
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

            resetPlaceholder();
            emit statusMessage(T("manual_dl_status"), 6000);
        };

        const QByteArray body = reply->readAll();
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            if (httpStatus == 401) {
                // Bad or expired apikey. Even the free nxm:// flow sends the key
                // in the header, so this is NOT a premium wall - steer the user
                // to fix the key instead of looping on "need Premium".
                resetPlaceholder();
                emit apiKeyRejected();
                return;
            }
            offerManualFallback();
            return;
        }
        const auto uriResult = NexusClient::parseDownloadUri(body);
        if (!uriResult) {
            // 200 with an explanatory object instead of a CDN array: free
            // account on the Premium endpoint, or mod-manager DL disabled.
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

// Append to the queue and build its UI row.
void DownloadQueue::enqueueDownload(QListWidgetItem *placeholder,
                                    const QUrl      &url,
                                    const QString   &filename)
{
    // a new download means the manual-fallback dialog did its job; close it
    if (placeholder) {
        if (auto box = m_manualDlBoxes.take(placeholder); box) box->close();
    }

    QueuedDownload q;
    q.placeholder = placeholder;
    q.url         = url;
    q.filename    = filename;

    // row widget: name + progress bar + cancel
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

    // don't force the dock open; saved visibility wins (Settings > Show Download Queue)
    updateQueueTotals();
    processDownloadQueue();
}

// Start the next waiting item when a slot frees up.
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

// Abort the active reply, or drop a still-queued entry.
void DownloadQueue::cancelQueued(QListWidgetItem *placeholder)
{
    int idx = -1;
    for (int i = 0; i < m_queue.size(); ++i)
        if (m_queue[i].placeholder == placeholder) { idx = i; break; }
    if (idx < 0) return;

    const bool wasActive = m_queue[idx].active;

    if (wasActive) {
        // finished() handler does the cleanup once the abort lands
        if (QNetworkReply *reply = m_active.value(placeholder))
            reply->abort();
        return;
    }

    // queued-only: drop row + entry, reset placeholder to not-installed
    removeQueueRow(placeholder);
    m_dlAttempts.remove(placeholder);
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

bool DownloadQueue::isDownloadActive(QListWidgetItem *placeholder) const
{
    return m_active.contains(placeholder);
}

// Crash recovery on startup.
void DownloadQueue::cleanStaleDownloads()
{
    // InstallStatus=2 with no live reply == leftover from a crash/forced quit.
    // Reset to not-installed so it can be retried; the partial archive is
    // overwritten on the next download.
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

        // strip spinner/"installing" text back to the plain name
        QString name = it->data(ModRole::CustomName).toString();
        if (name.isEmpty())
            name = QFileInfo(it->data(ModRole::ModPath).toString()).fileName();
        if (!name.isEmpty()) it->setText(name);
        ++healed;
    }
    if (healed > 0) emit saveRequested();
}

void DownloadQueue::onQueuePauseToggled()
{
    m_paused = m_pauseBtn->isChecked();
    m_pauseBtn->setText(m_paused ? T("queue_resume") : T("queue_pause"));
    if (!m_paused) processDownloadQueue();
    updateQueueTotals();
}

void DownloadQueue::onQueueRowsMoved(const QModelIndex &, int srcStart, int,
                                     const QModelIndex &, int dstRow)
{
    // sync m_queue to the new UI row order. active + queued rows are both
    // draggable; the active one keeps running wherever it lands.
    if (srcStart < 0 || srcStart >= m_queue.size()) return;
    int dst = dstRow > srcStart ? dstRow - 1 : dstRow;
    dst = qBound(0, dst, m_queue.size() - 1);
    m_queue.move(srcStart, dst);
    processDownloadQueue();
}

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

// Refresh the header total.
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

// Append one integrity-diagnostic line to the log.
void DownloadQueue::writeDiag(const QString &line) const
{
    if (m_diagLogPath.isEmpty()) {
        qWarning().noquote() << "[download]" << line;
        return;
    }
    QFile f(m_diagLogPath);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream(&f) << QDateTime::currentDateTime().toString(Qt::ISODate)
                        << " [download] " << line << '\n';
    } else {
        qWarning().noquote() << "[download]" << line;
    }
}

// Integrity gate before extraction. A byte-complete download (no reply error,
// Content-Length satisfied) can still be junk: some CDNs return an HTML/JSON
// error page with a 200, and some files arrive with valid archive magic but
// corrupt contents (Fair Care). Catch both here, before the confusing
// 7z-fatal downstream.
QString DownloadQueue::archiveProblem(const QString &savePath,
                                      const QString &ctype,
                                      QListWidgetItem *placeholder) const
{
    QByteArray header;
    qint64     size = 0;
    {
        QFile f(savePath);
        if (!f.open(QIODevice::ReadOnly))
            return QStringLiteral("unreadable");
        size   = f.size();
        header = f.read(16);
    }

    // error page served as a 200. Real downloads are octet-stream or a
    // specific archive type, never a rendered page, so an HTML/JSON/XML
    // content-type or body start means the CDN gave us an error not the file.
    const QString ct = ctype.toLower();
    const bool errorBody =
        ct.contains(QStringLiteral("text/html")) ||
        ct.contains(QStringLiteral("application/json")) ||
        ct.contains(QStringLiteral("application/xml")) ||
        ct.contains(QStringLiteral("text/xml")) ||
        header.startsWith("<!") || header.startsWith("<htm") ||
        header.startsWith("<HTM") || header.startsWith("<?xml") ||
        header.startsWith("{\"") || header.startsWith("[{");
    if (errorBody)
        return QStringLiteral("error-body type=%1").arg(ctype);
    if (size < 64)
        return QStringLiteral("too-small bytes=%1").arg(size);

    // Files with no md5/size skip InstallController::verifyArchive entirely -
    // the gap Fair Care corruption slipped through. Test those ourselves, but
    // only when the body claims to be an archive: a loose plugin
    // (.esp/.omwaddon, no magic) is a legit non-archive, so `7z t` on it would
    // false-positive.
    const QString expectedMd5 = placeholder
        ? placeholder->data(ModRole::ExpectedMd5).toString().trimmed()
        : QString();
    const qint64 expectedSize = placeholder
        ? placeholder->data(ModRole::ExpectedSize).toLongLong() : 0;
    const bool unverified = expectedMd5.isEmpty() && expectedSize <= 0;
    if (unverified && archive_magic::looksLikeArchive(header)) {
        const int code = subprocess::execute(QStringLiteral("7z"),
                                             {QStringLiteral("t"), savePath});
        // only a positive exit means 7z opened it and it's broken. -1 is
        // "couldn't launch / timed out" (7z may not be installed; .zip uses
        // unzip). Don't condemn a download we just can't verify.
        if (code > 0)
            return QStringLiteral("7z-test-failed exit=%1").arg(code);
    }
    return QString();  // usable
}

// Issue the GET, wire up progress + completion.
void DownloadQueue::downloadFile(const QUrl    &downloadUrl,
                                  const QString &filename,
                                  QListWidgetItem *placeholder)
{
    QString savePath = QDir(m_modsDir).filePath(filename);

    QFile *file = new QFile(savePath, this);
    if (!file->open(QIODevice::WriteOnly)) {
        ui::warn(m_parentWidget, T("file_error_title"), T("file_error_write").arg(savePath));
        delete file;
        return;
    }

    placeholder->setData(ModRole::DownloadProgress, 0);

    // notify: download started
    QProcess::startDetached("notify-send",
        {"-a", T("window_title"), "-i", "nerevarine_organizer",
         T("notif_download_title"), T("notif_download_body").arg(filename)});

    QNetworkRequest req(downloadUrl);
    // Ask the CDN not to gzip. Qt auto-decompresses but reports the
    // *compressed* size in ContentLengthHeader, so a truncated gzip slips past
    // the completeness guard below (decompressed file looks "larger" than the
    // declared compressed length) and lands as valid-magic-but-corrupt.
    // identity keeps bytes raw so declared == on-disk and the guard means something.
    req.setRawHeader("Accept-Encoding", "identity");
    QNetworkReply *reply = m_net->get(req);
    m_active[placeholder] = reply;

    // elapsed timer for rate/ETA
    m_startTime[placeholder].start();

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, placeholder, filename](qint64 received, qint64 total) {
        if (!m_modList->indexFromItem(placeholder).isValid()) return;
        int pct = (total > 0) ? static_cast<int>(received * 100 / total) : 0;
        placeholder->setData(ModRole::DownloadProgress, pct);
        m_modList->update(m_modList->indexFromItem(placeholder));
        updateQueueRowProgress(placeholder, received, total);

        // status bar: "Downloading foo.zip: 12.4 MB / 45.1 MB (27%) - ETA 0:42"
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
            [this, reply, file, savePath, filename, placeholder, downloadUrl]() {
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
            m_dlAttempts.remove(placeholder);
            emit statusMessage(T("status_download_cancelled"), 3000);
            processDownloadQueue();
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(savePath);
            m_dlAttempts.remove(placeholder);
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

        // Diagnostics + completeness guard. No network error flagged, but the
        // CDN sometimes hands back partial content that "fatal"-errors in the
        // extractor. Log what landed, and reject a body the server declared
        // larger than what we got (silent truncation) instead of extracting it.
        const qint64  declared = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        const QString ctype    = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        const qint64  onDisk   = QFileInfo(savePath).size();
        writeDiag(QStringLiteral("saved %1 bytes=%2 declared=%3 type=%4 from=%5")
                      .arg(filename).arg(onDisk).arg(declared)
                      .arg(ctype, reply->url().toString()));
        if (declared > 0 && onDisk < declared) {
            QFile::remove(savePath);
            m_dlAttempts.remove(placeholder);
            if (m_modList->indexFromItem(placeholder).isValid()) {
                placeholder->setData(ModRole::InstallStatus, 0);
                placeholder->setData(ModRole::DownloadProgress, QVariant());
                placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                      Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable);
                emit saveRequested();
            }
            ui::warn(m_parentWidget, T("download_incomplete_title"),
                     T("download_incomplete_body").arg(filename)
                         .arg(onDisk).arg(declared));
            emit statusMessage(T("status_download_failed"), 4000);
            processDownloadQueue();
            return;
        }

        // Integrity gate. A byte-complete download can still be unusable (200
        // error page, or the Fair Care case: valid magic but corrupt). Retry
        // the same URL once for a transient bad transfer; if it persists, keep
        // the bad copy + log it instead of feeding the extractor a 7z-fatal.
        if (const QString problem = archiveProblem(savePath, ctype, placeholder);
            !problem.isEmpty()) {
            int &attempts = m_dlAttempts[placeholder];
            if (attempts < 1) {
                ++attempts;
                writeDiag(QStringLiteral("retry %1 attempt=%2 reason=%3")
                              .arg(filename).arg(attempts).arg(problem));
                QFile::remove(savePath);
                emit statusMessage(T("status_download_retrying").arg(filename), 4000);
                // Re-queue, don't call downloadFile() directly: removeQueueRow()
                // above already dropped this m_queue entry, so a bare
                // downloadFile() would run uncounted against kMaxConcurrent (a
                // second download could start alongside it) with no progress
                // row and no cancel. enqueueDownload() rebuilds entry+row and
                // lets processDownloadQueue() honour the slot.
                enqueueDownload(placeholder, downloadUrl, filename);
                return;
            }
            m_dlAttempts.remove(placeholder);

            // retries exhausted: keep the artifact + record magic so the cause
            // is inspectable, then point the user at the manual-drag workaround
            QByteArray magic;
            if (QFile f(savePath); f.open(QIODevice::ReadOnly)) magic = f.read(8);
            const QString keptPath = savePath + QStringLiteral(".corrupt");
            QFile::remove(keptPath);
            const bool kept = QFile::rename(savePath, keptPath);
            if (!kept) QFile::remove(savePath);
            writeDiag(QStringLiteral("CORRUPT %1 reason=%2 magic=%3 kept=%4")
                          .arg(filename, problem,
                               QString::fromLatin1(magic.toHex()),
                               kept ? keptPath : QStringLiteral("(rename failed)")));
            if (m_modList->indexFromItem(placeholder).isValid()) {
                placeholder->setData(ModRole::InstallStatus, 0);
                placeholder->setData(ModRole::DownloadProgress, QVariant());
                placeholder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                      Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable);
                emit saveRequested();
            }
            ui::warn(m_parentWidget, T("download_corrupt_title"),
                     T("download_corrupt_body").arg(filename, problem,
                         kept ? keptPath : QStringLiteral("-")));
            emit statusMessage(T("status_download_failed"), 4000);
            processDownloadQueue();
            return;
        }
        m_dlAttempts.remove(placeholder);  // clean transfer, reset counter

        // extracting phase - indeterminate progress
        if (m_modList->indexFromItem(placeholder).isValid())
            placeholder->setData(ModRole::DownloadProgress, -1);

        emit statusMessage(T("status_downloaded_extracting").arg(filename));
        emit extractionRequested(savePath, placeholder);
        processDownloadQueue();

        // notify: download complete
        QProcess::startDetached("notify-send",
            {"-a", T("window_title"), "-i", "nerevarine_organizer",
             T("notif_done_title"), T("notif_done_body").arg(filename)});
    });
}
