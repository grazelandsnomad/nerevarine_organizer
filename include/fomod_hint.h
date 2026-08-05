#ifndef FOMOD_HINT_H
#define FOMOD_HINT_H

// Is a FOMOD Yes/No question asking about a mod the user might already have?
//
// The wizard annotates Yes/No steps with a modlist verdict. The "present" half
// is safe: something in the modlist actually matched the step name, so the step
// demonstrably names a mod. The "absent" half is not - a step that matches
// nothing may simply not be about a mod at all. Dunmer Lanterns Replacer asks
// "Would you like the lanterns to use a glow effect?" and got back "No.
// Recommended. This mod is not currently present in the modlist", which reads
// as a missing dependency being reported and pre-picked the wrong answer.
//
// A failed lookup is not evidence of absence unless the thing looked up was a
// mod name, and the modlist cannot establish that for a mod it does not hold.
// The only evidence left is the wording: a question about what the user already
// has ("Do you use X?", "for users of X") is about another mod, a question
// about what the user would like is about this one. No phrasing match means no
// verdict - silence rather than a guess.

#include <QString>

namespace fomod {

// True when the step/group wording asks about the user's existing setup rather
// than about an option of the mod being installed. Case- and
// whitespace-insensitive; the two strings are matched as one question.
bool asksAboutAnotherMod(const QString &stepName, const QString &groupName);

} // namespace fomod

#endif // FOMOD_HINT_H
