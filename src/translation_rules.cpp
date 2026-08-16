#include "translation_rules.h"

#include <QFile>
#include <QSaveFile>
#include <QTextStream>

namespace translation_rules {

Rules load(const QString &path)
{
    Rules r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return r;

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    QString section;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2).trimmed().toLower();
            continue;
        }

        if (section == QLatin1String("terms")) {
            // First '=' splits, so a replacement may contain one.
            const int sep = line.indexOf(QLatin1Char('='));
            if (sep <= 0) continue;
            const QString from = line.left(sep).trimmed();
            const QString to   = line.mid(sep + 1).trimmed();
            if (!from.isEmpty() && !to.isEmpty())
                r.terms.insert(from.toLower(), to);
        } else if (section == QLatin1String("protect")) {
            r.protect << line;
        } else if (section == QLatin1String("ordinary")) {
            r.ordinary.insert(line.toLower());
        } else if (section == QLatin1String("after")) {
            // "=>" rather than "=": these are phrases, and a phrase is far
            // more likely to contain "=" than the arrow.
            const int sep = line.indexOf(QStringLiteral("=>"));
            if (sep <= 0) continue;
            const QString from = line.left(sep).trimmed();
            const QString to   = line.mid(sep + 2).trimmed();
            if (!from.isEmpty()) r.after.append({from, to});
        }
    }
    return r;
}

bool ensureTemplate(const QString &path, const QString &language)
{
    if (QFile::exists(path)) return true;

    const QString lang = language.isEmpty() ? QStringLiteral("your language")
                                            : language;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << "# Nerevarine Organizer - translation rules for " << lang << "\n"
        << "#\n"
        << "# Edit this file and reopen the translate window to apply it.\n"
        << "# Blank lines and lines starting with # are ignored.\n"
        << "#\n"
        << "# What you type in the editor always wins over anything here.\n"
        << "\n"
        << "[terms]\n"
        << "# Whole-string replacements, used instead of asking the machine\n"
        << "# translator. The left side must match the WHOLE original string.\n"
        << "#\n"
        << "#   Chest=Cofre\n"
        << "#   Wardrobe=Armario\n"
        << "\n"
        << "[protect]\n"
        << "# Names to keep exactly as they are, wherever they appear. Names\n"
        << "# that repeat across several strings are already protected\n"
        << "# automatically; this is for the ones that appear only once.\n"
        << "#\n"
        << "#   Forfeoranna Heim\n"
        << "\n"
        << "[ordinary]\n"
        << "# The opposite: words that should stay translatable even when they\n"
        << "# repeat, if automatic protection is freezing something it should\n"
        << "# not.\n"
        << "#\n"
        << "#   blade\n"
        << "\n"
        << "[after]\n"
        << "# Fixes applied to the machine translator's answer, for wording\n"
        << "# inside a sentence that a whole-string rule cannot reach.\n"
        << "# Written as LEFT=>RIGHT.\n"
        << "#\n"
        << "#   Gran espada=>Mandoble\n";
    out.flush();
    return f.commit();
}

QString applyAfter(const QString &text, const Rules &r)
{
    QString out = text;
    for (const auto &[from, to] : r.after)
        out.replace(from, to, Qt::CaseInsensitive);
    return out;
}

} // namespace translation_rules
