#include "bethesda_archives.h"

namespace bethesda_archives {

namespace {

// Seed SArchiveList only when the ini has none (rare; launcher writes one on
// first run). A real install with DLC already has its own list; we just append.
const QStringList kVanillaBsas = {
    QStringLiteral("Oblivion - Meshes.bsa"),
    QStringLiteral("Oblivion - Textures - Compressed.bsa"),
    QStringLiteral("Oblivion - Sounds.bsa"),
    QStringLiteral("Oblivion - Voices1.bsa"),
    QStringLiteral("Oblivion - Voices2.bsa"),
    QStringLiteral("Oblivion - Misc.bsa"),
};

QStringList parseList(const QString &value)
{
    QStringList out;
    for (const QString &part : value.split(QLatin1Char(','))) {
        const QString t = part.trimmed();
        if (!t.isEmpty()) out << t;
    }
    return out;
}

// Append each bsa not already present (case-insensitive), preserving order.
void appendBsas(QStringList &list, const QStringList &bsas)
{
    for (const QString &b : bsas) {
        bool have = false;
        for (const QString &e : list)
            if (e.compare(b, Qt::CaseInsensitive) == 0) { have = true; break; }
        if (!have) list << b;
    }
}

} // namespace

QString configureArchives(const QString &iniText, const QStringList &modBsas)
{
    QStringList lines = iniText.split(QLatin1Char('\n'));
    // A trailing newline splits into a final empty element. Keeping it would
    // re-emit a blank line every run, so Oblivion.ini grew by a line on each
    // deploy and the transform was not idempotent.
    if (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();

    QStringList out;
    bool inArchive = false, archiveSeen = false;
    bool sawList = false, sawInvalidate = false, sawInvalFile = false;

    auto flushArchive = [&]() {
        // On leaving [Archive], add whatever keys it was missing.
        if (!sawInvalidate) out << QStringLiteral("bInvalidateOlderFiles=1");
        if (!sawInvalFile)  out << QStringLiteral("SInvalidationFile=");
        if (!sawList) {
            QStringList l = kVanillaBsas;
            appendBsas(l, modBsas);
            out << QStringLiteral("SArchiveList=") + l.join(QStringLiteral(", "));
        }
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
                sawList = sawInvalidate = sawInvalFile = false;
            }
            out << line;
            continue;
        }

        if (inArchive) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq > 0) {
                const QString key = line.left(eq).trimmed();
                if (key.compare(QStringLiteral("SArchiveList"), Qt::CaseInsensitive) == 0) {
                    QStringList l = parseList(line.mid(eq + 1));
                    appendBsas(l, modBsas);
                    out << QStringLiteral("SArchiveList=") + l.join(QStringLiteral(", "));
                    sawList = true;
                    continue;
                }
                if (key.compare(QStringLiteral("bInvalidateOlderFiles"), Qt::CaseInsensitive) == 0) {
                    out << QStringLiteral("bInvalidateOlderFiles=1");
                    sawInvalidate = true;
                    continue;
                }
                if (key.compare(QStringLiteral("SInvalidationFile"), Qt::CaseInsensitive) == 0) {
                    out << QStringLiteral("SInvalidationFile=");
                    sawInvalFile = true;
                    continue;
                }
            }
        }
        out << line;
    }
    if (inArchive) flushArchive();

    // No [Archive] section anywhere: append a fresh one.
    if (!archiveSeen) {
        QStringList l = kVanillaBsas;
        appendBsas(l, modBsas);
        out << QStringLiteral("[Archive]");
        out << QStringLiteral("bInvalidateOlderFiles=1");
        out << QStringLiteral("SInvalidationFile=");
        out << QStringLiteral("SArchiveList=") + l.join(QStringLiteral(", "));
    }

    QString result = out.join(QStringLiteral("\r\n"));
    if (!result.isEmpty()) result += QStringLiteral("\r\n");
    return result;
}

} // namespace bethesda_archives
