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
