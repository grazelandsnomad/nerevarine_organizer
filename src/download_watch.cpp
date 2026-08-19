#include "download_watch.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace download_watch {

QStringList defaultDirs()
{
    QStringList out;
    auto push = [&out](const QString &d) {
        if (!d.isEmpty() && QFileInfo(d).isDir() && !out.contains(d)) out << d;
    };
    push(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    push(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    return out;
}

bool isPartialName(const QString &fileName)
{
    const QString low = QFileInfo(fileName).fileName().toLower();
    // Chrome, Firefox, wget/curl and the "still uploading" temp names, plus
    // anything hidden - a dotfile in a download folder is never a mod.
    return low.endsWith(QLatin1String(".crdownload"))
        || low.endsWith(QLatin1String(".part"))
        || low.endsWith(QLatin1String(".partial"))
        || low.endsWith(QLatin1String(".download"))
        || low.endsWith(QLatin1String(".tmp"))
        || low.endsWith(QLatin1String(".temp"))
        || low.startsWith(QLatin1Char('.'));
}

int nexusModIdFromFileName(const QString &fileName)
{
    const QString base = QFileInfo(fileName).completeBaseName();
    // "<name>-<modid>-<version parts...>-<epoch>". The epoch is the anchor: it
    // is the only field with a fixed shape, and without it this is somebody
    // else's naming scheme and nothing should be inferred.
    static const QRegularExpression rx(
        QStringLiteral("-(\\d{1,7})-(?:\\d+-)*\\d{9,11}$"));
    const auto m = rx.match(base);
    if (!m.hasMatch()) return -1;
    bool ok = false;
    const int id = m.captured(1).toInt(&ok);
    return (ok && id > 0) ? id : -1;
}

bool SettleTracker::observe(const QString &path, qint64 size)
{
    if (m_reported.contains(path)) return false;
    const auto it = m_lastSize.constFind(path);
    if (it == m_lastSize.constEnd() || *it != size) {
        m_lastSize.insert(path, size);
        return false;
    }
    // Two samples the same: the browser has stopped writing.
    m_reported.insert(path);
    m_lastSize.remove(path);
    return true;
}

void SettleTracker::forget(const QString &path)
{
    m_lastSize.remove(path);
    m_reported.insert(path);       // never offer it again
}

void SettleTracker::clear()
{
    m_lastSize.clear();
    m_reported.clear();
}

Watcher::Watcher(QObject *parent) : QObject(parent) {}

void Watcher::start(const QStringList &dirs,
                    const std::function<bool(const QString &)> &accept)
{
    stop();
    m_dirs   = dirs.isEmpty() ? defaultDirs() : dirs;
    m_accept = accept;
    if (m_dirs.isEmpty() || !m_accept) return;

    // Everything already there is the user's own business. Only files that
    // arrive from now on are a pending install.
    for (const QString &d : std::as_const(m_dirs))
        for (const QString &f : QDir(d).entryList(QDir::Files))
            m_known.insert(QDir(d).filePath(f));

    m_watcher = new QFileSystemWatcher(m_dirs, this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        if (m_settle && !m_settle->isActive()) m_settle->start();
        rescan();
    });

    // The directory signal fires when the browser renames the .crdownload
    // away, but the file is still being written at that moment, so sizes are
    // sampled on a timer until they stop changing.
    m_settle = new QTimer(this);
    m_settle->setInterval(1500);
    connect(m_settle, &QTimer::timeout, this, [this] { rescan(); });

    m_running = true;
    rescan();
}

void Watcher::stop()
{
    if (m_watcher) { m_watcher->deleteLater(); m_watcher = nullptr; }
    if (m_settle)  { m_settle->stop(); m_settle->deleteLater(); m_settle = nullptr; }
    m_tracker.clear();
    m_known.clear();
    m_running = false;
}

void Watcher::ignore(const QString &path)
{
    m_tracker.forget(path);
    m_known.insert(path);
}

void Watcher::rescan()
{
    if (!m_running) return;
    bool pending = false;
    for (const QString &d : std::as_const(m_dirs)) {
        const QDir dir(d);
        for (const QString &name : dir.entryList(QDir::Files)) {
            const QString path = dir.filePath(name);
            if (m_known.contains(path)) continue;
            if (isPartialName(name)) { pending = true; continue; }
            if (!m_accept(path)) { m_known.insert(path); continue; }

            const qint64 size = QFileInfo(path).size();
            if (m_tracker.observe(path, size)) {
                m_known.insert(path);
                emit archiveReady(path);
            } else {
                pending = true;
            }
        }
    }
    // Keep sampling only while something is actually in flight.
    if (m_settle) {
        if (pending && !m_settle->isActive()) m_settle->start();
        else if (!pending && m_settle->isActive()) m_settle->stop();
    }
}

} // namespace download_watch
