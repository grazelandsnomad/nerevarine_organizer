#ifndef DOWNLOAD_WATCH_H
#define DOWNLOAD_WATCH_H

// download_watch - notice a mod archive arriving in the browser's download
// folder, so a game with no mod-manager download button is still one click to
// install.
//
// Nexus only shows "Mod Manager Download" for games it has manager integration
// for, and Gothic 2 has none: every file in that section is a manual download
// and no nxm:// link is ever generated, for anyone. The API's direct download
// endpoint is no help either without Premium, since a free account needs the
// key/expires pair that only that missing button produces. So the link cannot
// be caught - but the file can, once the browser has finished writing it.
//
// Deliberately narrow: it only runs for a profile whose adapter says its mods
// can only be downloaded by hand (GameAdapter::manualDownloadsOnly), because
// everywhere else the nxm:// handler already does this properly and a banner
// per download would be noise.
//
// The state machine is pure and tested; the QFileSystemWatcher around it is a
// thin shell.

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

class QFileSystemWatcher;
class QTimer;

namespace download_watch {

// Where browsers put things: the XDG download location, plus the desktop as a
// distant second (people retarget downloads there).
QStringList defaultDirs();

// A partly-written download, under whatever name the browser gives it while it
// works: Chrome writes "No confirmat 416242.crdownload", Firefox "foo.part".
// These must never be offered - the file is incomplete and its final name is
// not even known yet.
bool isPartialName(const QString &fileName);

// The Nexus mod id a manual download's file name carries, or -1.
//
// Nexus names a manual download "<mod name>-<mod id>-<version>-<epoch>.<ext>",
// e.g. "Ultimate Texture Pack-135-1-0-1699999999.7z". Recovering the id is
// what lets a hand-downloaded mod keep a mod page, and therefore keep being
// checked for updates - the thing you otherwise lose by going manual.
//
// Anchored on the trailing 9-to-11 digit timestamp: without it this is not a
// Nexus name and nothing is guessed. Returns -1 rather than a maybe.
int nexusModIdFromFileName(const QString &fileName);

// Has a file stopped growing?
//
// A download that has just been renamed to its final name is still being
// written, and handing a half-file to the installer produces a corrupt-archive
// error the user cannot explain. So each candidate is sized repeatedly and only
// offered once two consecutive samples agree.
//
// Pure: sizes in, verdict out, so the timing logic is testable without waiting
// on a real download.
class SettleTracker {
public:
    // Returns true the first time `path` is seen at the same size twice.
    // Reports each path exactly once; later calls for it return false.
    bool observe(const QString &path, qint64 size);
    void forget(const QString &path);
    void clear();

private:
    QHash<QString, qint64> m_lastSize;
    QSet<QString>          m_reported;
};

// The live watcher. Emits archiveReady once per file, for files that appear
// AFTER it starts: whatever was already in the folder is the user's business,
// not a pending install.
class Watcher : public QObject {
    Q_OBJECT
public:
    explicit Watcher(QObject *parent = nullptr);

    // `dirs` empty means defaultDirs(). `accept` decides what counts as
    // installable - passed in rather than hardcoded so the app keeps one
    // definition of that (see isInstallableArchiveSuffix).
    void start(const QStringList &dirs, const std::function<bool(const QString &)> &accept);
    void stop();
    bool isRunning() const { return m_running; }

    // Never offer this path again this session (the user said no).
    void ignore(const QString &path);

signals:
    void archiveReady(const QString &path);

private:
    void rescan();

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer             *m_settle  = nullptr;
    SettleTracker       m_tracker;
    QSet<QString>       m_known;      // present when we started, or handled
    QStringList         m_dirs;
    std::function<bool(const QString &)> m_accept;
    bool                m_running = false;
};

} // namespace download_watch

#endif // DOWNLOAD_WATCH_H
