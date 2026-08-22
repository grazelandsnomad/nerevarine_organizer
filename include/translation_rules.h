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
//   [patterns]   whole-cell replacements with a hole in them, written
//                LEFT=RIGHT with %1..%9 standing for whatever the name is.
//                For a mod that names a hundred things the same way and puts
//                the words in an order the target language does not use.
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
    // Whole-cell shapes: {"%1 Devotee", "Devoto de %1"}, in file order.
    QList<QPair<QString, QString>> patterns;

    bool isEmpty() const
    { return terms.isEmpty() && protect.isEmpty()
          && ordinary.isEmpty() && after.isEmpty() && patterns.isEmpty(); }
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

// The answer the first matching pattern gives for `text`, or empty when none
// matches. Free-standing rather than a Rules method because the built-in
// table hands over patterns of its own (lore_overrides::patternsFor) and both
// lists have to go through one matcher.
//
// -- Why the whole cell and nothing less -------------------------------
//
// Varieties of Faith calls its worship titles "Akatosh Devotee", and Google
// keeps the English word order: "Akatosh Devoto", which is not Spanish. The
// shape "%1 Devotee = Devoto de %1" fixes every deity in the mod, including
// the ones nobody thought to list.
//
// Loose inside a sentence it would be a menace. "He is an Akatosh Devotee,
// you know" is prose, and prose needs the translator that can see the grammar
// around it; a pattern firing there would hand back a half-translated
// sentence. So the match is anchored to the entire string, the same limit
// lore_overrides sets on itself.
//
// The literal parts match without regard to case. What %1 captures keeps its
// own, because what it captures is a name - and it may be at most four words,
// because a name is short and a capture allowed to run on swallows prose.
QString applyPatterns(const QString &text,
                      const QList<QPair<QString, QString>> &patterns);

} // namespace translation_rules

#endif // TRANSLATION_RULES_H
