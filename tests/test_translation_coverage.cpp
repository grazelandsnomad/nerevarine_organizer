// The verdict behind every red "no translation" caption.
//
// It lived inside TranslationScanWorker::run() - a QThread - so five calibrated
// judgements had nothing pinning them: the pairing ratio, the TES3
// near-verbatim rejector, the localized-Strings rule, and the two Partial
// thresholds. It took a user's finished translation being disowned to get them
// out where a test could reach them.

#include "translation_coverage.h"
#include "vanilla_text.h"

#include <QCoreApplication>

#include <iostream>

#include "test_harness.h"

namespace {

using translation_coverage::Entry;
using translation_coverage::Verdict;
using State = TranslationCoverage::State;

// A TES3 plugin's strings, hand-built: judge() takes StringSets directly, so
// nothing here has to touch a disk.
plugin_strings::StringSet tes3Set(const QHash<QString, QString> &core)
{
    plugin_strings::StringSet s;
    s.valid = true;
    s.tes3  = true;
    s.byKey = core;
    return s;
}

Verdict verdictFor(const QList<Verdict> &all, int modIdx)
{
    for (const Verdict &v : all)
        if (v.modIdx == modIdx) return v;
    return {};
}

// -- The regression --------------------------------------------------------

// Daedric Maul is 7 KB of one weapon plus 72 game settings its author's editor
// re-saved without meaning to. The editor filtered those and offered 2 strings;
// the user translated both and built the translation mod. The scan compared all
// 27 keys, found 25 unchanged - Bethesda's words, correctly untouched - and the
// near-verbatim rejector threw the finished translation out. The source row
// read "no translation" with its own translation one row below it.
//
// Numbers verbatim from the live pair: common=27, identical=25, ratio=0.926.
void testTheDaedricMaulShape()
{
    std::cout << "\n[a finished translation is not a near-verbatim copy]\n";

    QHash<QString, QString> source, translated;
    // The 25 the editor refuses: base-game settings the mod merely re-saved.
    for (int i = 0; i < 25; ++i) {
        const QString key = QStringLiteral("GMST:sSetting%1:STRV:0").arg(i);
        const QString txt = QStringLiteral("Bethesda's own wording %1").arg(i);
        source.insert(key, txt);
        translated.insert(key, txt);            // untouched, correctly
    }
    // The 2 the mod actually owns, and did get translated.
    source.insert("WEAP:daedric_maul:FNAM:0",   "Daedric Maul");
    translated.insert("WEAP:daedric_maul:FNAM:0", QString::fromUtf8("Mazo Daédrico"));
    source.insert("GMST:sWerewolfPopup:STRV:0", "You have been detected changing.");
    translated.insert("GMST:sWerewolfPopup:STRV:0",
                      QString::fromUtf8("Se te ha detectado cambiando."));

    const QStringList names{QStringLiteral("Daedric Maul"),
                            QStringLiteral("Daedric Maul - Spanish (Nerevarine)")};

    // Unfiltered - what shipped, and what the user reported.
    {
        const QList<Entry> entries{
            {0, "daedric_maul.esp", tes3Set(source)},
            {1, "daedric_maul.esp", tes3Set(translated)},
        };
        const auto v = verdictFor(
            translation_coverage::judge(entries, {}, names, "spanish"), 0);
        check("unfiltered, the translation is thrown out as a near-copy",
              v.state == State::NoTranslation);
    }

    // Filtered, as the scan now does before judging. The base game's 25 go, and
    // what remains is the 2 strings the mod owns - neither of them identical.
    {
        vanilla_text::Table vanilla;
        for (int i = 0; i < 25; ++i)
            vanilla.insert(QStringLiteral("sSetting%1").arg(i),
                           QStringLiteral("Bethesda's own wording %1").arg(i));

        auto a = tes3Set(source), b = tes3Set(translated);
        vanilla_text::dropBaseGameText(a, vanilla);
        vanilla_text::dropBaseGameText(b, vanilla);
        check("the filter leaves the mod's own two strings", a.byKey.size() == 2,
              QString::number(a.byKey.size()));

        const QList<Entry> entries{{0, "daedric_maul.esp", a},
                                   {1, "daedric_maul.esp", b}};
        const auto v = verdictFor(
            translation_coverage::judge(entries, {}, names, "spanish"), 0);
        check("filtered, the translation is recognised", v.state == State::Ok);
        check("and it is named as the partner",
              v.partnerMod == names[1], v.partnerMod);
    }
}

// -- The protections that must survive it ----------------------------------

void testTheNearVerbatimRejectorStillGuards()
{
    std::cout << "\n[a near-verbatim copy is still not a translation]\n";
    // A compatibility patch: the same English plugin with one line edited.
    QHash<QString, QString> a, b;
    for (int i = 0; i < 20; ++i) {
        const QString k = QStringLiteral("BOOK:b%1:FNAM:0").arg(i);
        a.insert(k, QStringLiteral("An English title %1").arg(i));
        b.insert(k, QStringLiteral("An English title %1").arg(i));
    }
    b["BOOK:b0:FNAM:0"] = QStringLiteral("An edited English title");

    const QStringList names{QStringLiteral("Some Mod"),
                            QStringLiteral("Some Mod Patch")};
    const QList<Entry> entries{{0, "a.esp", tes3Set(a)}, {1, "a.esp", tes3Set(b)}};
    const auto v = verdictFor(
        translation_coverage::judge(entries, {}, names, "spanish"), 0);
    check("19 of 20 identical is a copy, not a translation",
          v.state == State::NoTranslation);

    // Small pairs are judged by the same rule, with no floor under it. There
    // WAS a `common >= 10` floor, and dropping base-game text is what made it
    // both unnecessary and wrong: on the live list Nordic Dagon Fel and
    // Ashfront share 8 identical English strings, OAAB_Data and OAABandoned
    // Shack share 2, and the floor let each pair call the other a translation.
    // Filtered, a few shared keys are a few deliberate ones.
    QHash<QString, QString> small, smallCopy, smallTr;
    for (int i = 0; i < 4; ++i) {
        const QString k = QStringLiteral("BOOK:s%1:FNAM:0").arg(i);
        small.insert(k, QStringLiteral("Title %1").arg(i));
        smallCopy.insert(k, QStringLiteral("Title %1").arg(i));
        smallTr.insert(k, QStringLiteral("Titulo %1").arg(i));
    }
    const QList<Entry> fewCopied{{0, "s.esp", tes3Set(small)},
                                 {1, "s.esp", tes3Set(smallCopy)}};
    check("four shared keys, all identical, is still a copy",
          verdictFor(translation_coverage::judge(fewCopied, {}, names, "spanish"), 0)
              .state == State::NoTranslation);

    // And the reason the floor could go: losing it costs a genuinely small
    // translation nothing. This is Daedric Maul's measured shape - a handful of
    // shared keys, none of them left in English.
    const QList<Entry> fewTranslated{{0, "s.esp", tes3Set(small)},
                                     {1, "s.esp", tes3Set(smallTr)}};
    {
        const auto t = verdictFor(
            translation_coverage::judge(fewTranslated, {}, names, "spanish"), 0);
        check("four shared keys, none identical, is a translation",
              t.state == State::Ok);
        check("and it still names its partner", t.partnerMod == names[1],
              t.partnerMod);
    }
}

void testThePairingGate()
{
    std::cout << "\n[a partner has to share most of the smaller set]\n";
    QHash<QString, QString> a;
    for (int i = 0; i < 10; ++i)
        a.insert(QStringLiteral("BOOK:b%1:FNAM:0").arg(i),
                 QStringLiteral("Title %1").arg(i));

    // The gate is against the SMALLER of the two key counts, not the source's -
    // so a small partner that covers all of itself still pairs, and only a
    // candidate that leaves most of the smaller set untouched is rejected.
    // Both of these carry 10 keys; the first shares 4 of them, the second 6.
    QHash<QString, QString> few, many;
    for (int i = 0; i < 10; ++i) {
        few.insert(i < 4 ? QStringLiteral("BOOK:b%1:FNAM:0").arg(i)
                         : QStringLiteral("BOOK:other%1:FNAM:0").arg(i),
                   QStringLiteral("Titulo %1").arg(i));
        many.insert(i < 6 ? QStringLiteral("BOOK:b%1:FNAM:0").arg(i)
                          : QStringLiteral("BOOK:other%1:FNAM:0").arg(i),
                    QStringLiteral("Titulo %1").arg(i));
    }

    const QStringList names{QStringLiteral("Mod"), QStringLiteral("Partner")};
    check("sharing 4 of 10 does not pair",
          verdictFor(translation_coverage::judge(
                         {{0, "a.esp", tes3Set(a)}, {1, "b.esp", tes3Set(few)}},
                         {}, names, "spanish"), 0).state == State::NoTranslation);
    check("sharing 6 of 10 does",
          verdictFor(translation_coverage::judge(
                         {{0, "a.esp", tes3Set(a)}, {1, "b.esp", tes3Set(many)}},
                         {}, names, "spanish"), 0).state == State::Ok);

    // And the reading the first fixture accidentally pinned, which is worth
    // keeping: a partner smaller than the source that covers all of ITSELF
    // pairs, because the ratio is against the smaller set.
    QHash<QString, QString> small;
    for (int i = 0; i < 4; ++i)
        small.insert(QStringLiteral("BOOK:b%1:FNAM:0").arg(i),
                     QStringLiteral("Titulo %1").arg(i));
    check("a small partner covering all of itself pairs",
          verdictFor(translation_coverage::judge(
                         {{0, "a.esp", tes3Set(a)}, {1, "b.esp", tes3Set(small)}},
                         {}, names, "spanish"), 0).state == State::Ok);
}

void testAModDoesNotTranslateItself()
{
    std::cout << "\n[a mod's own second plugin is not its translator]\n";
    QHash<QString, QString> t;
    for (int i = 0; i < 10; ++i)
        t.insert(QStringLiteral("BOOK:b%1:FNAM:0").arg(i),
                 QStringLiteral("Titulo %1").arg(i));

    // Same modIdx on both entries: one mod shipping two plugins.
    const QList<Entry> entries{{0, "a.esp", tes3Set(t)}, {0, "b.esp", tes3Set(t)}};
    check("same-mod plugins never pair",
          verdictFor(translation_coverage::judge(
                         entries, {}, {QStringLiteral("Mod")}, "spanish"), 0)
              .state == State::NoTranslation);
}

void testTheLocalizedStringsRule()
{
    std::cout << "\n[a localized plugin is judged on its Strings files]\n";
    plugin_strings::StringSet loc;
    loc.valid = true;
    loc.localized = true;
    const QList<Entry> entries{{0, "sanguine.esp", loc}};
    const QStringList names{QStringLiteral("Sanguine Symphony")};

    // Its Strings are inside a BSA this walk cannot read, so NOTHING was seen -
    // not even the English set that is certainly in there. Absence is not
    // evidence when you could not look.
    check("no Strings seen at all means no opinion",
          verdictFor(translation_coverage::judge(entries, {}, names, "spanish"), 0)
              .state == State::Ok);

    // English seen, Spanish absent: now the absence means something.
    check("English seen but not Spanish is untranslated",
          verdictFor(translation_coverage::judge(
                         entries, {QStringLiteral("sanguine_english")},
                         names, "spanish"), 0).state == State::NoTranslation);
    check("Spanish seen is translated",
          verdictFor(translation_coverage::judge(
                         entries, {QStringLiteral("sanguine_english"),
                                   QStringLiteral("sanguine_spanish")},
                         names, "spanish"), 0).state == State::Ok);
    // No target language: nothing to be missing.
    check("with no target language there is no verdict",
          verdictFor(translation_coverage::judge(
                         entries, {QStringLiteral("sanguine_english")},
                         names, QString()), 0).state == State::Ok);
}

void testAModThatIsItselfTheTranslation()
{
    std::cout << "\n[a mod already in the target language needs no partner]\n";
    // Better Crowd Citizens Spanish ships only the Spanish version, so nothing
    // pairs with it - and it was flagged red while offering to translate
    // "Ciudadana" into Spanish.
    QHash<QString, QString> spanish;
    const char *words[] = {"Ciudadana", "Guardia de la ciudad", "Mercader",
                           "Ciudadano", "Herrero", "Cazador"};
    for (int i = 0; i < 6; ++i)
        spanish.insert(QStringLiteral("NPC_:n%1:FNAM:0").arg(i),
                       QString::fromUtf8(words[i]));
    plugin_strings::StringSet s = tes3Set({});
    s.auxByKey = spanish;                       // names only: secondary tier

    const auto v = verdictFor(
        translation_coverage::judge(
            {{0, "bcc.esp", s}}, {},
            {QStringLiteral("Better Crowd Citizens Spanish")}, "spanish"), 0);
    check("it reads as already translated, not as untranslated",
          v.state == State::Ok);
}

void testTes3AndSecondaryNeverGoPartial()
{
    std::cout << "\n[amber is a TES4 core-text verdict only]\n";
    // A pairing well over kPartialRatio/kPartialCount: 30 shared, 25 identical.
    QHash<QString, QString> a, b;
    for (int i = 0; i < 30; ++i) {
        const QString k = QStringLiteral("BOOK:b%1:FNAM:0").arg(i);
        a.insert(k, QStringLiteral("Title %1").arg(i));
        b.insert(k, i < 25 ? QStringLiteral("Title %1").arg(i)
                           : QStringLiteral("Titulo %1").arg(i));
    }
    const QStringList names{QStringLiteral("Mod"), QStringLiteral("Partner")};

    // TES3: red or Ok only - the thresholds were calibrated on a TES4 pair and
    // the live Morrowind list holds none to measure a TES3 floor against.
    // (This shape is caught by the near-verbatim rejector first: 25/30 is 83%,
    // under 90%, so it pairs - and then must NOT come back amber.)
    const auto t3 = verdictFor(translation_coverage::judge(
        {{0, "a.esp", tes3Set(a)}, {1, "b.esp", tes3Set(b)}}, {}, names,
        "spanish"), 0);
    check("a TES3 pairing never reports Partial", t3.state != State::Partial);

    // The same numbers on a TES4 plugin do earn amber.
    plugin_strings::StringSet a4 = tes3Set(a), b4 = tes3Set(b);
    a4.tes3 = b4.tes3 = false;
    const auto t4 = verdictFor(translation_coverage::judge(
        {{0, "a.esp", a4}, {1, "b.esp", b4}}, {}, names, "spanish"), 0);
    check("the same shape on TES4 does", t4.state == State::Partial);
    check("and it reports what it counted",
          t4.common == 30 && t4.identical == 25,
          QStringLiteral("%1/%2").arg(t4.identical).arg(t4.common));
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testTheDaedricMaulShape();
    testTheNearVerbatimRejectorStillGuards();
    testThePairingGate();
    testAModDoesNotTranslateItself();
    testTheLocalizedStringsRule();
    testAModThatIsItselfTheTranslation();
    testTes3AndSecondaryNeverGoPartial();

    std::cout << "\n";
    if (s_failed == 0)
        std::cout << "\033[32m" << s_passed << " / " << s_passed
                  << " tests passed\033[0m\n";
    else
        std::cout << s_passed << " passed, \033[31m" << s_failed
                  << " failed\033[0m\n";
    return s_failed == 0 ? 0 : 1;
}
