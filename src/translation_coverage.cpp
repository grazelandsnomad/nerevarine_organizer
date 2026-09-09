#include "translation_coverage.h"

#include "language_guess.h"
#include "term_protect.h"
#include "vanilla_text.h"

#include <algorithm>

namespace translation_coverage {
namespace {

// Strings to judge the language on: both tiers, since an NPC-name-only mod is
// exactly the case this has to answer. Capped because the test is a proportion,
// not a census, and a 5000-string plugin decides itself long before the end.
QStringList sampleText(const plugin_strings::StringSet &s)
{
    constexpr int kMax = 300;
    QStringList out;
    out.reserve(qMin(kMax, int(s.byKey.size() + s.auxByKey.size())));
    for (auto it = s.byKey.cbegin(); it != s.byKey.cend(); ++it) {
        if (out.size() >= kMax) return out;
        out << it.value();
    }
    for (auto it = s.auxByKey.cbegin(); it != s.auxByKey.cend(); ++it) {
        if (out.size() >= kMax) return out;
        out << it.value();
    }
    return out;
}

} // namespace

void dropBareNames(plugin_strings::StringSet &set,
                   const QSet<QString> &ordinaryOverrides)
{
    // Both tiers: a creature or NPC name is Secondary, an item or door name is
    // Core, and the mod that prompted this holds exactly one of the former.
    for (QHash<QString, QString> *keys : { &set.byKey, &set.auxByKey }) {
        for (auto it = keys->begin(); it != keys->end(); ) {
            // ONE word, and that restriction is doing most of the safety
            // work. everyWordIsName leans on term_protect's 222-word ordinary
            // list, which was built to decide what to hide from a translator -
            // a job where a missing word costs one wasted request. Here it
            // would cost a mod its red flag, and the list is nowhere near good
            // enough for that on a phrase: "Ceremonial Morag Tong Blade" is a
            // real item name that needs translating, and it read as pure name
            // because neither "Ceremonial" nor "Blade" is in the list.
            //
            // A multi-word display name is almost always a description of a
            // thing. A single invented word is almost always what something is
            // called. So a phrase stays flagged - the conservative direction,
            // and what the app already did - and only the lone name goes.
            const QString value = it.value().trimmed();
            const bool oneWord =
                !value.isEmpty()
                && !std::any_of(value.cbegin(), value.cend(),
                                [](QChar c) { return c.isSpace(); });
            if (oneWord && vanilla_text::isDisplayNameKey(it.key())
                && term_protect::everyWordIsName(value, ordinaryOverrides))
                it = keys->erase(it);
            else
                ++it;
        }
    }
}

QList<Verdict> judge(const QList<Entry> &entries,
                     const QSet<QString> &stringFiles,
                     const QStringList &modNames,
                     const QString &targetLanguage)
{
    QList<Verdict> out;
    out.reserve(entries.size());

    const auto nameOf = [&modNames](int modIdx) {
        return (modIdx >= 0 && modIdx < modNames.size()) ? modNames[modIdx]
                                                         : QString();
    };

    // Pair every plugin with the best candidate from a DIFFERENT mod: the one
    // sharing the most keys, provided it shares most of the smaller set.
    // Same-mod plugins are skipped - a mod does not translate itself.
    for (int a = 0; a < entries.size(); ++a) {
        const Entry &ea = entries[a];
        Verdict v;
        v.modIdx     = ea.modIdx;
        v.pluginName = ea.pluginName;

        // A localized plugin keeps its text in Strings/, so coverage is a
        // file-presence question and no comparison is possible.
        //
        // But the absence of a Spanish Strings file is only evidence when the
        // Strings files can be seen AT ALL. Sanguine Symphony is localized and
        // ships its Strings inside a BSA, which the walk cannot read
        // (bsareader is TES3-only), so it found nothing - not even the English
        // set that is definitely in there - and reported "no translation".
        // Meanwhile the editor refused the same plugin for being localized, so
        // the manager said both at once.
        //
        // Finding SOME Strings file for this plugin is what makes the missing
        // one meaningful. Finding none means we could not look.
        if (ea.strings.localized) {
            const QString base = ea.pluginName.section(QLatin1Char('.'), 0, 0);
            bool sawAny = false;
            for (const QString &tok : stringFiles) {
                if (tok.startsWith(base + QLatin1Char('_'))) { sawAny = true; break; }
            }
            if (targetLanguage.isEmpty() || !sawAny) {
                // Nothing to say: no target language set, or the Strings are
                // somewhere we cannot read.
                v.state = TranslationCoverage::State::Ok;
                out << v;
                continue;
            }
            v.state = stringFiles.contains(base + QLatin1Char('_') + targetLanguage)
                          ? TranslationCoverage::State::Ok
                          : TranslationCoverage::State::NoTranslation;
            out << v;
            continue;
        }

        // Core text drives the verdict whenever there is any, so nothing about
        // the existing results moves. Only when a plugin has none does the
        // secondary tier take over - and then it may answer the pairing
        // question but never the percentage (plugin_strings.h).
        const bool secondaryOnly = ea.strings.byKey.isEmpty();
        const auto tier = secondaryOnly ? plugin_strings::Tier::Secondary
                                        : plugin_strings::Tier::Core;
        const auto keysOf = [tier](const plugin_strings::StringSet &s) {
            return tier == plugin_strings::Tier::Core ? s.byKey.size()
                                                      : s.auxByKey.size();
        };

        int bestShared = 0, bestIdx = -1;
        plugin_strings::Comparison best;
        for (int b = 0; b < entries.size(); ++b) {
            if (b == a || entries[b].modIdx == ea.modIdx) continue;
            if (entries[b].strings.localized) continue;
            const auto cmp = plugin_strings::compare(
                ea.strings, entries[b].strings, /*maxSamples=*/8, tier);
            const int smaller = qMin(keysOf(ea.strings),
                                     keysOf(entries[b].strings));
            if (smaller <= 0) continue;
            if (double(cmp.common) < kPairRatio * double(smaller)) continue;
            // TES3 has no amber verdict to absorb this case (below), so a
            // candidate that is a near-verbatim COPY - a compatibility patch or
            // an edited duplicate of the same English plugin - must not count
            // as a translation partner at all. Measured on the live Morrowind
            // list: every such pair sits at ~100% identical, while a real
            // translation measured 1.4% (USSEP-ES), so 90% cannot misfire on
            // one.
            //
            // Safe only because base-game text is filtered out before this
            // runs. Unfiltered, a mod that re-saved 25 of Bethesda's game
            // settings looked ~93% identical to its own finished translation
            // and was thrown out here. See translation_coverage.h.
            //
            // There used to be a `common >= 10` floor as well, and the same
            // filtering is what retired it. Unfiltered, a handful of shared
            // keys was mostly base-game noise and too thin to judge on;
            // filtered, every key is content one of the two mods actually
            // wrote, so a pair identical across ALL of them is a copy however
            // few they are. The floor was measurably harmful once the filter
            // went in - it let three English mods declare each other
            // translated: Nordic Dagon Fel and Ashfront share 8 identical
            // strings, Death and Taxes and Ashfront 6, OAAB_Data and
            // OAABandoned Shack 2, and every one slipped underneath it.
            //
            // A real translation is unaffected either way: Daedric Maul's
            // filtered pair is 2 common, 0 identical.
            if (ea.strings.tes3
                && double(cmp.identical) >= 0.9 * double(cmp.common))
                continue;
            if (cmp.common > bestShared) {
                bestShared = cmp.common;
                bestIdx    = b;
                best       = cmp;
            }
        }

        v.translatable = keysOf(ea.strings);
        if (bestIdx < 0) {
            // No partner supplies alternative text - but that has two causes,
            // and only one of them is a problem. A mod can also have no partner
            // because it IS the translation: Better Crowd Citizens Spanish
            // ships only the Spanish version, so nothing pairs with it and it
            // was flagged red while offering to translate "Ciudadana" into
            // Spanish. Ask whether there is anything left to translate before
            // saying there is.
            if (!targetLanguage.isEmpty()
                && language_guess::alreadyInLanguage(
                       nameOf(ea.modIdx), sampleText(ea.strings), targetLanguage)) {
                v.state = TranslationCoverage::State::Ok;
                out << v;
                continue;
            }
            v.state = TranslationCoverage::State::NoTranslation;
            out << v;
            continue;
        }
        // kPartialRatio/kPartialCount were measured on core types only.
        // Proper-noun-heavy secondary text sits far above that floor while
        // being perfectly translated, so a partial verdict there would cry
        // wolf: a covered secondary-only plugin reports Ok until the threshold
        // has been measured on a real translated pair.
        // TES3 additionally reports red/Ok only: kPartialRatio/kPartialCount
        // were calibrated on a TES4 pair (USSEP-ES), and the live Morrowind
        // list holds no translated pair to measure a TES3 floor against.
        // Revisit when one exists.
        const bool partial = !secondaryOnly && !ea.strings.tes3
                          && best.ratio() >= plugin_strings::kPartialRatio
                          && best.identical >= plugin_strings::kPartialCount;
        v.state      = partial ? TranslationCoverage::State::Partial
                               : TranslationCoverage::State::Ok;
        v.partnerMod = nameOf(entries[bestIdx].modIdx);
        v.samples    = best.samples;
        v.common     = best.common;
        v.identical  = best.identical;
        out << v;
    }

    return out;
}

} // namespace translation_coverage
