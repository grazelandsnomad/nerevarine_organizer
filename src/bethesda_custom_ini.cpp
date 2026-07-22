#include "bethesda_custom_ini.h"

#include <QFileInfo>

namespace bethesda_custom_ini {

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
            // Exactly "<stem>.ba2", or the "<stem> - Main/Textures.ba2"
            // convention. A bare startsWith() would also swallow
            // "<stem>Patch - Main.ba2", which the engine does NOT auto-load,
            // and we would then stay silent about an archive that never loads.
            if (name.compare(stem + QStringLiteral(".ba2"), Qt::CaseInsensitive) == 0
                || name.startsWith(stem + QStringLiteral(" - "), Qt::CaseInsensitive)) {
                covered = true;
                break;
            }
        }
        if (!covered) stray << name;
    }
    return stray;
}

QString configureCustomIni(const QString &iniText)
{
    QStringList lines = iniText.split(QLatin1Char('\n'));
    // A trailing newline splits into a final empty element. Keeping it would
    // re-emit a blank line every run, so the ini would grow by one line per
    // deploy and the transform would not be idempotent.
    if (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();

    QStringList out;
    bool inArchive = false, archiveSeen = false;
    bool sawInvalidate = false, sawDataDirs = false;

    auto flushArchive = [&]() {
        // On leaving [Archive], add whatever keys it was missing. Only the
        // invalidation pair: the archive-list keys are left strictly alone, so
        // whatever the user or the base ini set there survives untouched.
        if (!sawInvalidate) out << QStringLiteral("bInvalidateOlderFiles=1");
        if (!sawDataDirs)   out << QStringLiteral("sResourceDataDirsFinal=");
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
                sawInvalidate = sawDataDirs = false;
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
    }

    QString result = out.join(QStringLiteral("\r\n"));
    if (!result.isEmpty()) result += QStringLiteral("\r\n");
    return result;
}

} // namespace bethesda_custom_ini
