#pragma once

// row_caption - the short text a mod row shows right of its name, and which
// of its kinds it is.
//
// Two halves. "win": what this mod overwrites. "lose": the one the user has to
// act on, which several things compete for:
//
// - overwritten by another mod;
// - a record clash with a mod it shares no file with;
// - no translation, or only part of one;
// - a translation already under way;
// - for a row that is itself a translation, the mod it translates.
//
// ModListDelegate::paint decided this in the order its statements happened to
// run, and a "translation of" that took the slot from a warning was caught
// only by reading the code. The precedence is stated here, once, and tested:
//
// 1. A translation in progress outranks every verdict.
// 2. Then "no translation" or "N of M strings untranslated".
// 3. Then the file conflict, and a record clash only when there is none.
// 4. "translation of X" is good news, so it takes the slot only when nothing
//    else wanted it.
//
// Qt Core only. The texts keep ModListDelegate's translation context.

#include <QString>
#include <QStringList>

namespace row_caption {

// Everything the choice reads, as the ModRoles hold it. A notices flag that is
// off silences its whole family, as the menu toggles do.
struct Inputs {
    bool        conflictNotices = false;
    QStringList overwrites;      // ModRole::ConflictOverwrites: "mod\t..." entries
    QStringList overwrittenBy;   // ModRole::ConflictOverwrittenBy
    QStringList sameRecords;     // ModRole::ConflictSameRecords: "mod\tkind" entries

    bool        translationNotices = false;
    int         translationState = 0;        // 0 covered, 1 none, 2 partial
    int         translationInProgress = 0;
    bool        isTranslationOfOther = false;
    QString     translationPartner;          // the mod it translates
    QStringList translationDetail;           // head: "...\t...\tuntranslated\ttotal"
};

// What fills the "lose" half, for its colour.
enum class LoseKind {
    None,
    OverwrittenBy,
    SameRecords,
    InProgress,
    NoTranslation,
    PartlyTranslated,
    TranslationOf,
};

struct Caption {
    QString  win;
    QString  lose;
    LoseKind loseKind = LoseKind::None;
};

Caption choose(const Inputs &in);

} // namespace row_caption
