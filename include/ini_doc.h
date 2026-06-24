#ifndef INI_DOC_H
#define INI_DOC_H

#include <QString>
#include <QStringList>

// Minimal preserving INI editor - keeps original lines, comments, casing, and
// key order; only rewrites the specific line(s) we care about. Designed for
// Bethesda .ini files where QSettings::IniFormat would mangle space-containing
// keys (e.g. "bFull Screen") and drop user comments.
struct IniDoc {
    QStringList lines;

    bool load(const QString &path);
    bool save(const QString &path) const;

    // Find the line index for `key` inside [section]. Returns -1 if missing.
    int findKey(const QString &section, const QString &key) const;
    int findSection(const QString &section) const;

    QString get(const QString &section, const QString &key) const;
    void    set(const QString &section, const QString &key, const QString &value);
};

#endif // INI_DOC_H
