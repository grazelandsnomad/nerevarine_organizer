#ifndef TRANSLATION_STORE_H
#define TRANSLATION_STORE_H

// Remembers what a string was translated to, so it is typed once and not once
// per mod.
//
// "Bandit Chief" is not in one plugin, it is in dozens. Without a memory the
// user retypes "Lider Bandido" every time a mod happens to carry that record,
// which is the part of the job a manager is supposed to absorb. So the unit of
// storage is the SOURCE TEXT, not the record: any mod, any game, any plugin
// that shows "Bandit Chief" gets the same answer offered.
//
// Keyed by exact source text on purpose. Fuzzy matching would let "Bandit
// Chief" answer for "Bandit Chief's Key", and a wrong translation silently
// written into a plugin is worse than an empty field the user fills in. Case
// and surrounding whitespace are normalised away; nothing else is.
//
// One store per target language, so a Spanish and a French memory never see
// each other. The file path is supplied by the caller rather than derived here,
// which keeps this free of MainWindow and testable (resolveUserStatePath lives
// in mainwindow_internal.h and would drag the world in).
//
// Persisted as JSON: this is user-authored content that outlives any modlist,
// and a corrupt line must not cost the rest of the file.

#include <QHash>
#include <QString>
#include <QStringList>

namespace translation_store {

class Memory {
public:
    // Reads `path`. A missing file is an empty memory, not an error - the
    // first translation the user types is what creates it.
    bool load(const QString &path);

    // Writes atomically. Returns false only on a real I/O failure.
    bool save(const QString &path) const;

    // The remembered translation for `source`, or an empty string. Empty in,
    // empty out.
    QString lookup(const QString &source) const;

    // Records a translation. An empty `translation` forgets the entry instead
    // of storing a blank, so clearing a field in the editor un-remembers it.
    void remember(const QString &source, const QString &translation);

    int  size()  const { return int(m_map.size()); }
    bool empty() const { return m_map.isEmpty(); }

    // Every source string held, for a management UI. Order is unspecified.
    QStringList sources() const;

    // Merge a Nerevarine Scribe translation database (UTF-8 lines of
    // "source=translation", "#" comments) into this memory. Additive only:
    // an entry already in the memory wins over the imported one, because the
    // memory holds what the user has refined here. Returns {entries read,
    // entries added}; {-1, -1} when the file cannot be opened.
    struct ImportResult { int read = 0; int added = 0; };
    ImportResult importScribeDb(const QString &path);

private:
    // normalised source -> translation. The normalised form is only ever a
    // lookup key; the translation is stored exactly as typed.
    QHash<QString, QString> m_map;
};

// The lookup key for a source string: trimmed and case-folded. Exposed so a
// caller can group identical strings the same way the store does.
QString normalize(const QString &source);

} // namespace translation_store

#endif // TRANSLATION_STORE_H
