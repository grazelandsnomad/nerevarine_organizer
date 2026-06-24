#include "bethesda_loadorder.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace bethesda_loadorder {

static bool isMaster(const QString &p)
{
    return p.endsWith(QLatin1String(".esm"), Qt::CaseInsensitive)
        || p.endsWith(QLatin1String(".esl"), Qt::CaseInsensitive);
}

QStringList mastersFirst(const QStringList &plugins)
{
    QStringList masters, regular;
    for (const QString &p : plugins) {
        if (isMaster(p)) masters << p;
        else            regular << p;
    }
    return masters + regular;
}

QString pluginsTxtContent(const QStringList &activeInOrder)
{
    QString out;
    for (const QString &p : activeInOrder)
        out += p + QLatin1String("\r\n");
    return out;
}

QString asteriskPluginsTxtContent(const QStringList &activeInOrder)
{
    QString out;
    for (const QString &p : activeInOrder)
        out += QLatin1Char('*') + p + QLatin1String("\r\n");
    return out;
}

StampResult applyTimestampOrder(const QString &dataDir,
                                const QStringList &pluginsInOrder,
                                qint64 baseEpochMs, qint64 stepMs)
{
    StampResult r;
    const QDir data(dataDir);
    for (int i = 0; i < pluginsInOrder.size(); ++i) {
        const QString path = data.filePath(pluginsInOrder[i]);
        QFile f(path);
        if (!f.exists()) { r.errors << pluginsInOrder[i]; continue; }

        const QDateTime when =
            QDateTime::fromMSecsSinceEpoch(baseEpochMs + qint64(i) * stepMs);

        // setFileTime needs the file open; a read-only handle suffices to touch
        // mtime on a file we own. For a hardlinked deploy this also bumps the
        // source's mtime (same inode), which is harmless.
        const bool opened = f.open(QIODevice::ReadWrite) || f.open(QIODevice::ReadOnly);
        if (opened && f.setFileTime(when, QFileDevice::FileModificationTime))
            ++r.stamped;
        else
            r.errors << pluginsInOrder[i];
        if (f.isOpen()) f.close();
    }
    return r;
}

} // namespace bethesda_loadorder
