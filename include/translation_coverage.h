#ifndef TRANSLATION_COVERAGE_H
#define TRANSLATION_COVERAGE_H

// Does anything in this list translate this plugin?
//
// The verdict engine behind every red "no translation" caption. It used to live
// inside TranslationScanWorker::run(), which is a QThread - so five calibrated
// judgements (the pairing ratio, the near-verbatim rejector, the localized
// Strings rule, and the two Partial thresholds) had no test between them and no
// way to get one. Lifted out whole: the worker keeps the directory walk and the
// extraction, and hands the answers here.
//
// Pure. Everything arrives as values, nothing is read from disk, and no widget
// or thread is involved - which is the point.
//
// -- What it is NOT given -----------------------------------------------
//
// Base-game text. The caller runs vanilla_text::dropBaseGameText first, and it
// matters more than it sounds: Daedric Maul carries 72 game settings its
// author's editor re-saved by accident, so this saw 27 keys where the editor
// offered 2, found 25 of them unchanged - Bethesda's words, correctly untouched
// by any translator - and the near-verbatim rejector below threw out the user's
// finished translation as a near-copy. Feeding this the mod's own content is
// what makes the rejector safe to keep.

#include "loadordercontroller.h"   // TranslationCoverage::State
#include "plugin_strings.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace translation_coverage {

// One plugin of one mod, already extracted and already filtered.
struct Entry {
    int     modIdx = 0;
    QString pluginName;             // file name, lower case
    plugin_strings::StringSet strings;
};

// One plugin's verdict. The caller merges these per mod - a mod with several
// plugins takes the worst of them.
struct Verdict {
    int     modIdx = 0;
    QString pluginName;
    int     translatable = 0;
    TranslationCoverage::State state = TranslationCoverage::State::Ok;
    QString partnerMod;             // empty unless something paired
    QStringList samples;
    int     common    = 0;
    int     identical = 0;
};

// Drop display names that are the mod's own invented proper nouns, leaving the
// text a translation would actually have to change.
//
// True Vvardenfell - Dagoths Domain names one unique flame atronach
// "Veythrazel" and says nothing else. That is a name in every language, so the
// finished translation of that mod is the mod - yet the scan counted one
// untranslated string, found no partner, and painted the row red for work that
// does not exist.
//
// Only FNAM records holding a SINGLE word, and those two restrictions are what
// make it safe: the same word inside a book or a line of dialogue still has a
// sentence around it that needs translating, and a display name of several
// words is a description of a thing rather than what it is called. The judgement itself is
// term_protect's, not a second opinion - see everyWordIsName for why a lone
// word is admitted here and refused there.
//
// Naturally Morrowind-only, like the display-name half of dropBaseGameText:
// FNAM is the TES3 subrecord, and a Skyrim name arrives as FULL.
//
// `ordinaryOverrides` is `[ordinary]` from the user's rules file, and is the
// documented way out when this is too eager: a word listed there is ordinary
// English, so a name built from it stays countable and the mod stays flagged.
//
// This changes the FLAG only. The editor still offers the row, so a user who
// wants to rename Veythrazel in Spanish still can.
void dropBareNames(plugin_strings::StringSet &set,
                   const QSet<QString> &ordinaryOverrides);

// A plugin pairs with the candidate sharing the most keys, provided it shares
// at least this much of the smaller set. Below it the two are different text
// that happens to overlap, not a translation of one another.
constexpr double kPairRatio = 0.5;

// Judge every entry against every other. `modNames` is indexed by Entry::modIdx;
// `stringFiles` holds the lower-cased "<pluginbase>_<language>" tokens found
// under any enabled mod's Strings/ dir, which is the only evidence a localized
// plugin offers. `targetLanguage` empty means "no opinion" and yields Ok.
//
// Returns one Verdict per entry, in order.
QList<Verdict> judge(const QList<Entry> &entries,
                     const QSet<QString> &stringFiles,
                     const QStringList &modNames,
                     const QString &targetLanguage);

} // namespace translation_coverage

#endif // TRANSLATION_COVERAGE_H
