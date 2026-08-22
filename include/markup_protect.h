#ifndef MARKUP_PROTECT_H
#define MARKUP_PROTECT_H

// Keep the parts of a game string that are not prose out of the translator's
// hands: markup, dialogue variables, and escapes.
//
// Morrowind book and dialogue records are not plain sentences. A single record
// out of Master Trainers reads:
//
//   <DIV ALIGN="LEFT"><FONT COLOR="000000" SIZE="3">%Name heard there is
//   someone in the Temple who is a master of Restoration.
//
// Handed that whole thing, the machine translator treats it as text. It
// translates ALIGN and LEFT, decides COLOR wants an accent, moves the closing
// tag to where Spanish would want the noun it thinks the tag is, and renames
// %Name - which is not a name at all but the variable the game substitutes the
// player's interlocutor into. What comes back looks like a translation and is
// a broken record: the layout is gone and the dialogue addresses nobody.
//
// So the non-prose spans are lifted out, the prose is translated on its own,
// and the spans go back exactly as they were. This also translates BETTER:
// the sentence the translator sees is a sentence.
//
// -- Three kinds of span, one rule ------------------------------------
//
//   <...>       markup: tags and their attributes
//   %Name       the game's own substitutions (%Name, %PCName, %PCRace, ...)
//   \n \r \t    escapes written into the record as two characters
//
// -- Why the token is opaque, unlike term_protect's --------------------
//
// term_protect measured that a word-shaped token gets REORDERED by the
// translator, which is right for a proper noun: "NR0 Catacombs" comes back
// "Catacumbas NR0", the natural Spanish. That is exactly wrong here. A tag
// that opens a record has to stay where it opens it, and a tag moved into the
// middle of the sentence is worse than one left in English. A bracketed token
// is treated as opaque punctuation and left in place, which is what these
// need, so `{0}` it is.
//
// -- Why restore() checks its own work --------------------------------
//
// Protection is not a guarantee. The endpoint is free to drop a token, double
// one, or glue it to a word, and a string that silently lost its opening tag
// renders as raw markup in the game. So the round trip is verified against the
// source, and when it does not hold up the string is rebuilt from the source's
// own spans - and said to be, because a rebuilt string is a guess at where the
// prose belongs and the user should look at it.
//
// Pure: strings in, strings out.

#include <QString>
#include <QStringList>

namespace markup_protect {

// The non-prose spans of `text`, in the order they appear. Each occurrence is
// listed separately, so a record with two <FONT> tags yields two entries and
// each is put back where it was.
QStringList findSpans(const QString &text);

// The token standing in for spans[i].
QString tokenFor(int index);

// Replace each span with its token, left to right.
QString mask(const QString &text, const QStringList &spans);

// True when what is left is punctuation and tokens: a record that is nothing
// but markup has nothing to translate, and sending it anyway spends a request
// to get back a mangled tag.
bool isOnlySpans(const QString &masked);

struct Restored {
    QString text;
    // The round trip did not hold up and the string was rebuilt from the
    // source's spans. The text is usable; it is also a guess.
    bool    repaired = false;
    // A span that was in the middle of the source and did not survive. The
    // rebuild puts back what opens and closes the string; it cannot know
    // where a lost tag belonged inside the sentence.
    bool    lostInside = false;
};

// Put the spans back into a translated, masked string, and check the result
// against `source` - which must be the SAME text that was masked, so the two
// agree on what should be there.
Restored restore(const QString &source, const QString &translated,
                 const QStringList &spans);

} // namespace markup_protect

#endif // MARKUP_PROTECT_H
