#ifndef TRANSLATION_RULES_H
#define TRANSLATION_RULES_H

// The English-to-<language> rules the user tunes, in a file rather than in the
// binary.
//
// Three things sit between a mod's English string and what ends up in the
// plugin: a curated table of lore terms, a list of proper nouns to hide from
// the machine translator, and the translator itself. All three get things
// wrong in ways only the person playing in that language can see - "Chest"
// came back as "Pecho", the body part, where a Skyrim container is a "Cofre" -
// and a rule that needs a rebuild to change is a rule that never gets fixed.
//
// So they live in a plain text file per language, loaded when the editor
// opens, merged OVER the built-in defaults. Nothing here can be lost by an
// update, and a bad entry is undone by deleting a line.
//
// -- Precedence -------------------------------------------------------
//
//   1. the translation memory  - what the user typed, always wins
//   2. THIS FILE               - what the user decided in general
//   3. lore_overrides          - the built-in table
//   4. the machine translator
//
// -- The file ---------------------------------------------------------
//
//   [terms]      whole-cell replacements: a string equal to the left side is
//                answered with the right and never sent anywhere.
//   [protect]    names to hide from the translator wherever they appear, in
//                addition to the ones term_protect finds by repetition. For a
//                name that appears only once, which repetition cannot catch.
//   [ordinary]   words to treat as ordinary English, so repeating them does
//                NOT freeze them. The escape hatch when protection is too
//                eager.
//   [after]      plain-text fixes applied to the machine result, written
//                LEFT=>RIGHT. For wording inside a sentence, which a
//                whole-cell term cannot reach.
//
// Blank lines and lines starting with # are ignored. Keys keep their spaces
// and case; matching is case-insensitive.

#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

namespace translation_rules {

struct Rules {
    // Lowercased source text -> replacement, for whole-cell matches.
    QHash<QString, QString>       terms;
    QStringList                   protect;
    QSet<QString>                 ordinary;    // lowercased
    QList<QPair<QString, QString>> after;      // applied in file order

    bool isEmpty() const
    { return terms.isEmpty() && protect.isEmpty()
          && ordinary.isEmpty() && after.isEmpty(); }
};

// Reads `path`. A missing file is an empty rule set, not an error - the file
// only exists once the user has something to say.
Rules load(const QString &path);

// Writes a commented, empty template at `path` if nothing is there yet, so
// "edit the rules" opens something that explains itself. Returns false only
// on a real write failure. `language` is used in the header comment.
bool ensureTemplate(const QString &path, const QString &language);

// Apply the [after] fixes to a machine-translated string.
QString applyAfter(const QString &text, const Rules &r);

} // namespace translation_rules

#endif // TRANSLATION_RULES_H
