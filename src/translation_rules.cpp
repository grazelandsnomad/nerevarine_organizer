#include "translation_rules.h"

#include <QFile>
#include <QRegularExpression>
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
        } else if (section == QLatin1String("patterns")) {
            // '=' like [terms], not '=>' like [after]: these are whole-cell
            // shapes rather than sentence fragments, so the same split the
            // whole-cell section uses is the one that reads consistently.
            const int sep = line.indexOf(QLatin1Char('='));
            if (sep <= 0) continue;
            const QString from = line.left(sep).trimmed();
            const QString to   = line.mid(sep + 1).trimmed();
            // A pattern with no hole in it is a [terms] entry written in the
            // wrong section; taking it here would silently shadow one.
            if (!from.isEmpty() && !to.isEmpty() && from.contains(QLatin1Char('%')))
                r.patterns.append({from, to});
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
        << "#   Gran espada=>Mandoble\n"
        << "\n"
        << "[patterns]\n"
        << "# Whole-string shapes, for a mod that names many things the same\n"
        << "# way. %1 stands for whatever the name is, and comes back\n"
        << "# untranslated on the other side. Only matches a WHOLE string, so\n"
        << "# the same words inside a sentence are left to the translator.\n"
        << "#\n"
        << "#   %1 Devotee=Devoto de %1\n"
        << "#   Altar of %1=Altar de %1\n";
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

QString applyPatterns(const QString &text,
                      const QList<QPair<QString, QString>> &patterns)
{
    const QString subject = text.trimmed();
    if (subject.isEmpty() || patterns.isEmpty()) return {};

    // "%1 Devotee" becomes ^\s*(.+?)\s+Devotee\s*$ - the literal parts
    // escaped so a name with a "." or "(" in it cannot turn into a regex, the
    // holes non-greedy so two of them split at the first opportunity rather
    // than the last.
    static const QRegularExpression kHole(QStringLiteral("%([1-9])"));

    for (const auto &[shape, replacement] : patterns) {
        QString rx = QStringLiteral("^\\s*");
        QList<int> order;            // which %n each capture group carries
        qsizetype last = 0;
        auto it = kHole.globalMatch(shape);
        while (it.hasNext()) {
            const auto m = it.next();
            rx += QRegularExpression::escape(
                      shape.mid(last, m.capturedStart() - last));
            rx += QStringLiteral("(.+?)");
            order << m.captured(1).toInt();
            last = m.capturedEnd();
        }
        if (order.isEmpty()) continue;          // no hole: not a pattern
        rx += QRegularExpression::escape(shape.mid(last));
        rx += QStringLiteral("\\s*$");

        const QRegularExpression re(rx, QRegularExpression::CaseInsensitiveOption
                                        | QRegularExpression::UseUnicodePropertiesOption);
        if (!re.isValid()) continue;
        const auto m = re.match(subject);
        if (!m.hasMatch()) continue;

        // Every hole has to have caught something worth the name.
        QHash<int, QString> caught;
        bool ok = true;
        for (int i = 0; i < order.size(); ++i) {
            const QString c = m.captured(i + 1).trimmed();
            if (c.isEmpty()) { ok = false; break; }
            caught.insert(order[i], c);
        }
        if (!ok) continue;

        // The capture goes back exactly as it was written: it is a name, and
        // the case of a name is part of it.
        QString out = replacement;
        for (auto k = caught.cbegin(); k != caught.cend(); ++k)
            out.replace(QStringLiteral("%%1").arg(k.key()), k.value());
        return out;
    }
    return {};
}

} // namespace translation_rules
