#ifndef TRANSLATION_PROGRESS_H
#define TRANSLATION_PROGRESS_H

// Half-finished work on ONE mod, so a big one can be translated across many
// sittings.
//
// Project Cyrodiil is not a 480-row mod. A single plugin in this scene has
// been measured at 8435 strings; nobody answers that in an evening, and until
// now the only way to keep an answer was to press "Create translation mod",
// which also built the mod, added a modlist row and re-synced openmw.cfg. A
// month of fifteen-a-day was a month of that.
//
// -- Why this is not just the translation memory -----------------------
//
// translation_store::Memory is deliberately GLOBAL per language: "Bandit
// Chief" is answered once for every mod that ever mentions one. That is the
// right home for a settled answer and the wrong home for work in progress,
// because a machine guess nobody has read yet would pre-fill every other mod
// the user ever opens. So an answer lives here, against its own mod, until a
// human has vouched for it - and only then does it reach the memory.
//
// The entries map is deliberately the same shape a memory file uses, so the
// two are readable as each other: a half-finished job renamed is a memory.
//
// -- Keyed by source text, never by row ---------------------------------
//
// Rows are a case-insensitive sort of the mod's distinct strings, so a mod
// update that adds one string renumbers everything after it. Keying on text
// means an update simply leaves new strings blank and keeps the answers for
// the ones that survived, and it is why nothing here stores "the row I was
// on": where to resume is DERIVED, as the first string with no answer.
//
// An answer whose string has vanished from the mod is kept, not pruned. The
// next update may bring it back, and a month of typing is worth more than the
// bytes. staleAgainst() exists so the caller can say so out loud once.
//
// The file path is supplied by the caller, which keeps this free of MainWindow
// and testable - the same rule translation_store.h follows and for the same
// reason.

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>

namespace translation_progress {

// One answer, and whether anybody has looked at it.
struct Entry {
    QString translation;
    // A human has read this and vouched for it. False means it came back from
    // the machine translator and nobody has been over it yet, which is the one
    // state that must not reach the shared memory.
    //
    // True is the default for an entry that appears in the file without being
    // listed as unreviewed, so a file edited by hand reads as the user's own
    // work - which it is.
    bool reviewed = false;
};

class Progress {
public:
    // Reads `path`. A missing file is empty progress, not an error.
    bool load(const QString &path);

    // Writes atomically. Returns false only on a real I/O failure - the caller
    // is expected to SAY SO rather than close, since this is the call standing
    // between the user and a month of retyping.
    bool save(const QString &path) const;

    // The stored answer for `source`, or a default-constructed Entry when
    // there is none. Normalised the same way the memory normalises, so
    // trailing whitespace and case cannot split one string into two.
    Entry lookup(const QString &source) const;

    void record(const QString &source, const QString &translation, bool reviewed);
    void forget(const QString &source);

    // When this work was last built into a translation mod, invalid if never.
    //
    // A build is still a save point - the answers stay exactly where a later
    // sitting will find them, which is what makes re-reviewing a finished
    // translation possible at all. But finished work is not work in progress,
    // and a row that keeps saying "Translation in progress..." drowns out what
    // the coverage scan actually thinks of the mod.
    QDateTime builtAt() const { return m_built; }
    void      setBuilt(const QDateTime &whenUtc) { m_built = whenUtc; }

    // Whether the FILE said anything at all about being built. False for one
    // written before this existed - which is the caller's cue to look for the
    // built mod instead of assuming, rather than calling finished work
    // unfinished forever.
    bool hasBuildState() const { return m_sawBuilt; }

    // Provenance, round-tripped so a state directory is readable by a person
    // wondering what these files are.
    void setMod(const QString &modName, const QString &language);
    QString mod() const      { return m_mod; }
    QString language() const { return m_language; }

    int  size()  const { return int(m_map.size()); }
    bool empty() const { return m_map.isEmpty(); }

    // How many stored answers a human has vouched for.
    //
    // The count everything OUTSIDE the editor wants. size() is "has text",
    // which is what a machine-translate run leaves behind on every row at
    // once - a number built on it stops moving the moment a mod is pre-filled,
    // which is exactly when the user starts doing the real work. Entries are
    // never empty (load skips them, record forgets them), so the review flag
    // alone decides.
    int doneCount() const;

    // How many distinct strings the mod offered at the last sitting - the
    // denominator "N of M done" needs.
    //
    // It has to be STORED. Working it out means extracting the plugin, and the
    // untranslated scan asks this question once per installed mod; 866 of them
    // is not a question to answer twice. Zero means a file written before this
    // existed, which is the caller's cue to fall back rather than divide by it.
    int  total() const { return m_total; }
    void setTotal(int n) { m_total = n; }

    // How many stored answers no longer appear in `sources` - i.e. the mod was
    // updated and those strings went away. They are KEPT; this only counts
    // them so the caller can mention it.
    int staleAgainst(const QStringList &sources) const;

private:
    QHash<QString, Entry> m_map;    // normalized(source) -> entry
    QString   m_mod;
    QString   m_language;
    QDateTime m_built;              // invalid: never built
    bool      m_sawBuilt = false;   // the file carried a "built" key
    int       m_total = 0;          // strings the mod offered; 0: unknown
};

// The state filename for one (mod, language) pair, to be resolved against the
// caller's state directory.
//
// A readable slug plus a hash of the real name: the slug is so somebody
// looking at the directory can tell which file is which, and the hash is
// because folding punctuation away would otherwise let "Mod: A" and "Mod  A"
// collide and silently share one file.
// `stableId` is an identity for the mod PAGE that survives things the display
// name does not: a rename, and a reinstall (which lands in a new
// timestamped folder, so a path would not survive it either). "morrowind-59192"
// from the Nexus URL. Empty falls back to the name, which is all a
// hand-added mod has.
//
// Built by the caller rather than parsed here, so this stays free of the URL
// parser and its test target stays small.
QString fileNameFor(const QString &modName, const QString &language,
                    const QString &stableId = {});

} // namespace translation_progress

#endif // TRANSLATION_PROGRESS_H
