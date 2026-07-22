#include "starfield_archives.h"

#include <QFileInfo>

namespace starfield_archives {

const QString kStrayArchiveKey = QStringLiteral("sResourceArchiveList2");

namespace {

QStringList parseList(const QString &value)
{
    QStringList out;
    for (const QString &part : value.split(QLatin1Char(','))) {
        const QString t = part.trimmed();
        if (!t.isEmpty()) out << t;
    }
    return out;
}

// Append each archive not already present (case-insensitive), preserving order.
void appendArchives(QStringList &list, const QStringList &ba2s)
{
    for (const QString &b : ba2s) {
        bool have = false;
        for (const QString &e : list)
            if (e.compare(b, Qt::CaseInsensitive) == 0) { have = true; break; }
        if (!have) list << b;
    }
}

} // namespace

QStringList strayArchives(const QStringList &deployedBa2s,
                          const QStringList &deployedPlugins)
{
    QStringList stems;
    for (const QString &p : deployedPlugins) {
        const QString stem = QFileInfo(p).completeBaseName();
        if (!stem.isEmpty()) stems << stem;
    }

    QStringList stray;
    for (const QString &ba2 : deployedBa2s) {
        const QString name = QFileInfo(ba2).fileName();
        bool covered = false;
        for (const QString &stem : stems) {
            // "<stem>.ba2" and the "<stem> - Main/Textures.ba2" convention.
            if (name.startsWith(stem, Qt::CaseInsensitive)) { covered = true; break; }
        }
        if (!covered) stray << name;
    }
    return stray;
}

QString configureCustomIni(const QString &iniText, const QStringList &strayBa2s)
{
    QStringList lines = iniText.split(QLatin1Char('\n'));
    // A trailing newline splits into a final empty element. Keeping it would
    // re-emit a blank line every run, so the ini would grow by one line per
    // deploy and the transform would not be idempotent.
    if (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();

    QStringList out;
    bool inArchive = false, archiveSeen = false;
    bool sawInvalidate = false, sawDataDirs = false, sawStrayList = false;

    auto strayLine = [&](QStringList existing) {
        appendArchives(existing, strayBa2s);
        return kStrayArchiveKey + QStringLiteral("=")
             + existing.join(QStringLiteral(", "));
    };

    auto flushArchive = [&]() {
        // On leaving [Archive], add whatever keys it was missing. The
        // invalidation pair is unconditional; the stray list is only written
        // when there is something to put in it, so we never plant an empty key
        // that could shadow the game's own defaults.
        if (!sawInvalidate) out << QStringLiteral("bInvalidateOlderFiles=1");
        if (!sawDataDirs)   out << QStringLiteral("sResourceDataDirsFinal=");
        if (!sawStrayList && !strayBa2s.isEmpty()) out << strayLine({});
    };

    for (const QString &raw : lines) {
        QString line = raw;
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        const QString t = line.trimmed();

        if (t.startsWith(QLatin1Char('[')) && t.endsWith(QLatin1Char(']'))) {
            if (inArchive) flushArchive();
            inArchive = (t.compare(QStringLiteral("[Archive]"), Qt::CaseInsensitive) == 0);
            if (inArchive) {
                archiveSeen = true;
                sawInvalidate = sawDataDirs = sawStrayList = false;
            }
            out << line;
            continue;
        }

        if (inArchive) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq > 0) {
                const QString key = line.left(eq).trimmed();
                if (key.compare(QStringLiteral("bInvalidateOlderFiles"), Qt::CaseInsensitive) == 0) {
                    out << QStringLiteral("bInvalidateOlderFiles=1");
                    sawInvalidate = true;
                    continue;
                }
                if (key.compare(QStringLiteral("sResourceDataDirsFinal"), Qt::CaseInsensitive) == 0) {
                    out << QStringLiteral("sResourceDataDirsFinal=");
                    sawDataDirs = true;
                    continue;
                }
                if (key.compare(kStrayArchiveKey, Qt::CaseInsensitive) == 0) {
                    out << strayLine(parseList(line.mid(eq + 1)));
                    sawStrayList = true;
                    continue;
                }
            }
        }
        out << line;
    }
    if (inArchive) flushArchive();

    // No [Archive] anywhere (including the common case of a file we are
    // creating from nothing): append a fresh one.
    if (!archiveSeen) {
        out << QStringLiteral("[Archive]");
        out << QStringLiteral("bInvalidateOlderFiles=1");
        out << QStringLiteral("sResourceDataDirsFinal=");
        if (!strayBa2s.isEmpty()) out << strayLine({});
    }

    QString result = out.join(QStringLiteral("\r\n"));
    if (!result.isEmpty()) result += QStringLiteral("\r\n");
    return result;
}

} // namespace starfield_archives
