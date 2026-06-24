#include "ini_doc.h"

#include <QFile>
#include <QIODevice>
#include <QTextStream>
#include <Qt>

bool IniDoc::load(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    while (!in.atEnd()) lines.append(in.readLine());
    return true;
}

bool IniDoc::save(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    QTextStream out(&f);
    for (const QString &l : lines) out << l << "\n";
    return true;
}

int IniDoc::findKey(const QString &section, const QString &key) const
{
    QString sec = "[" + section + "]";
    bool in = false;
    for (int i = 0; i < lines.size(); ++i) {
        QString t = lines[i].trimmed();
        if (t.startsWith('[') && t.endsWith(']')) {
            in = (t.compare(sec, Qt::CaseInsensitive) == 0);
            continue;
        }
        if (!in) continue;
        int eq = t.indexOf('=');
        if (eq < 0) continue;
        if (t.left(eq).trimmed().compare(key, Qt::CaseInsensitive) == 0)
            return i;
    }
    return -1;
}

int IniDoc::findSection(const QString &section) const
{
    QString sec = "[" + section + "]";
    for (int i = 0; i < lines.size(); ++i)
        if (lines[i].trimmed().compare(sec, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

QString IniDoc::get(const QString &section, const QString &key) const
{
    int i = findKey(section, key);
    if (i < 0) return {};
    int eq = lines[i].indexOf('=');
    return eq < 0 ? QString() : lines[i].mid(eq + 1).trimmed();
}

void IniDoc::set(const QString &section, const QString &key, const QString &value)
{
    int i = findKey(section, key);
    if (i >= 0) { lines[i] = key + "=" + value; return; }
    int s = findSection(section);
    if (s >= 0) { lines.insert(s + 1, key + "=" + value); return; }
    if (!lines.isEmpty() && !lines.last().isEmpty()) lines.append(QString());
    lines.append("[" + section + "]");
    lines.append(key + "=" + value);
}
