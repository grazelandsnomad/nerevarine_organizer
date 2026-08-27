// TranslateDialog's table behaviour.
//
// The translate editor keeps a hidden "template" on each machine-translated
// row - the answer with the mod's proper nouns still masked - so that changing
// what a name should say re-renders every row that mentions it. That template
// is stored with QTableWidgetItem::setData on a custom role.
//
// QTableWidget emits cellChanged for a custom role exactly as it does for the
// visible text. So storing the template looked identical to the user typing,
// the row detached itself from its template, and every row but the name came
// back BLANK. Nothing outside the dialog could see it: the network calls all
// succeeded and the data was all correct.

#include "lore_overrides.h"
#include "term_protect.h"
#include "markup_protect.h"
#include "translate_dialog.h"

#include <QPushButton>
#include <QTimer>
#include "translation_rules.h"
#include "translation_store.h"

#include <QApplication>
#include <QFile>
#include <QTableWidget>
#include <QTemporaryDir>

#include <iostream>

#include "test_harness.h"

// Reaches the private table and the programmatic-write path.
struct TranslateDialogTestHook {
    static TranslateDialog *make(const QList<TranslatableString> &strings,
                                 translation_store::Memory *mem)
    {
        return new TranslateDialog(QStringLiteral("Forfeoranna Heim SSE"),
                                   strings, QStringLiteral("spanish"), mem);
    }
    static QTableWidget *table(TranslateDialog *d) { return d->m_table; }
    static QStringList  &names(TranslateDialog *d) { return d->m_mtNames; }
    static QStringList  &renderings(TranslateDialog *d) { return d->m_nameRendering; }
    static bool &expanding(TranslateDialog *d) { return d->m_expanding; }
    static void expand(TranslateDialog *d, int row) { d->expandRow(row); }
    static int  nameRow(TranslateDialog *d, int row) { return d->nameRowIndex(row); }
    static void pending(TranslateDialog *d, int row, int state)
    {
        // Exactly as pumpMachineTranslate does it, guard and all.
        const bool prev = d->m_expanding;
        d->m_expanding = true;
        d->setPending(row, state);
        d->m_expanding = prev;
    }
    static void pendingUnguarded(TranslateDialog *d, int row, int state)
    { d->setPending(row, state); }

    // Exactly what the network reply does when a row comes back.
    static void deliver(TranslateDialog *d, int row, const QString &masked)
    {
        {
            // Mirrors the guarded write in pumpMachineTranslate.
            const bool prev = d->m_expanding;
            d->m_expanding = true;
            d->m_table->item(row, 1)->setData(Qt::UserRole + 1, masked);
            d->m_expanding = prev;
        }
        d->expandRow(row);
    }
    // The same delivery WITHOUT the guard - the shape of the original bug.
    static void deliverUnguarded(TranslateDialog *d, int row, const QString &masked)
    {
        d->m_table->item(row, 1)->setData(Qt::UserRole + 1, masked);
        d->expandRow(row);
    }

    // -- the machine-translate run's own state ------------------------
    //
    // onMachineTranslate() itself is off limits: it ends in a blocking
    // QMessageBox, which in a headless run would simply hang. These reach the
    // pieces around it.
    static void arm(TranslateDialog *d, const QList<int> &queue,
                    const QList<int> &pending, bool namePhase)
    {
        d->m_mtQueue     = queue;
        d->m_mtPending   = pending;
        d->m_mtNamePhase = namePhase;
        d->m_mtTotal     = int(queue.size() + pending.size());
        d->m_mtDone      = 0;
        d->m_mtInFlight  = 0;
        d->m_mtStopped   = false;
        d->m_mtTally     = {};
        d->m_mtBtn->setEnabled(false);      // as a live run leaves it
    }
    // What the reply handler does when the endpoint answers 429.
    static void blockNow(TranslateDialog *d)
    {
        d->m_mtStopped = true;
        d->m_mtQueue.clear();
        d->m_mtPending.clear();
        if (d->m_mtPace) d->m_mtPace->stop();
    }
    static void advance(TranslateDialog *d) { d->advanceMachineTranslate(); }
    static void finish(TranslateDialog *d)  { d->finishMachineTranslate(); }
    static bool stopped(TranslateDialog *d) { return d->m_mtStopped; }
    static int  queued(TranslateDialog *d)  { return int(d->m_mtQueue.size()); }
    static int  pendingRows(TranslateDialog *d) { return int(d->m_mtPending.size()); }
    static bool buttonOn(TranslateDialog *d) { return d->m_mtBtn->isEnabled(); }
    static bool paceOn(TranslateDialog *d)
    { return d->m_mtPace && d->m_mtPace->isActive(); }
    static bool animOn(TranslateDialog *d)
    { return d->m_mtAnim && d->m_mtAnim->isActive(); }
    static int  pendingState(TranslateDialog *d, int row)
    { return d->m_table->item(row, 1)->data(Qt::UserRole + 3).toInt(); }
};

static QList<TranslatableString> dungeonStrings()
{
    // The real mod, minus the rows that do not matter here.
    return {
        {"a.esp", "CELL:1:FULL:0", "Forfeoranna Heim",           false},
        {"a.esp", "CELL:2:FULL:0", "Forfeoranna Heim Catacombs", false},
        {"a.esp", "CELL:3:FULL:0", "Forfeoranna Heim Depths",    false},
    };
}

// Rows are sorted case-insensitively by source, so find one by its text.
static int rowOf(QTableWidget *t, const QString &source)
{
    for (int r = 0; r < t->rowCount(); ++r)
        if (t->item(r, 0)->text() == source) return r;
    return -1;
}

static void testDeliveredRowIsNotBlank()
{
    std::cout << "\n[a machine-translated row actually shows its answer]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    TranslateDialogTestHook::names(d)      = {QStringLiteral("Forfeoranna Heim")};
    TranslateDialogTestHook::renderings(d) = {QString::fromUtf8("Hogar de los precursores")};

    const int row = rowOf(t, QStringLiteral("Forfeoranna Heim Catacombs"));
    check("the row exists", row >= 0);
    TranslateDialogTestHook::deliver(d, row, QStringLiteral("Catacumbas de Nrvaa"));

    check("the row is not blank", !t->item(row, 1)->text().isEmpty(),
          t->item(row, 1)->text());
    check("and carries the name's chosen rendering",
          t->item(row, 1)->text()
              == QString::fromUtf8("Catacumbas de Hogar de los precursores"),
          t->item(row, 1)->text());
    delete d;
}

// Pin the actual defect: without the guard the row blanks itself.
static void testUnguardedWriteIsWhatBlankedIt()
{
    std::cout << "\n[the bug: an unguarded write detaches the row]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    TranslateDialogTestHook::names(d)      = {QStringLiteral("Forfeoranna Heim")};
    TranslateDialogTestHook::renderings(d) = {QString::fromUtf8("Hogar de los precursores")};

    const int row = rowOf(t, QStringLiteral("Forfeoranna Heim Depths"));
    TranslateDialogTestHook::deliverUnguarded(d, row, QStringLiteral("Profundidades de Nrvaa"));
    check("an unguarded store leaves the row blank - this is what shipped",
          t->item(row, 1)->text().isEmpty(), t->item(row, 1)->text());
    delete d;
}

static void testEditingTheNameRerendersEveryRow()
{
    std::cout << "\n[editing the name updates every row that uses it]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    TranslateDialogTestHook::names(d)      = {QStringLiteral("Forfeoranna Heim")};
    TranslateDialogTestHook::renderings(d) = {QString::fromUtf8("Hogar de los precursores")};

    const int cat   = rowOf(t, QStringLiteral("Forfeoranna Heim Catacombs"));
    const int depth = rowOf(t, QStringLiteral("Forfeoranna Heim Depths"));
    const int name  = rowOf(t, QStringLiteral("Forfeoranna Heim"));
    TranslateDialogTestHook::deliver(d, cat,   QStringLiteral("Catacumbas de Nrvaa"));
    TranslateDialogTestHook::deliver(d, depth, QStringLiteral("Profundidades de Nrvaa"));

    check("the name row is recognised as the linked one",
          TranslateDialogTestHook::nameRow(d, name) == 0);

    // A real user edit, unguarded on purpose - that is what a user edit is.
    t->item(name, 1)->setText(QStringLiteral("Hogar Precursor"));

    check("the first row followed",
          t->item(cat, 1)->text() == QStringLiteral("Catacumbas de Hogar Precursor"),
          t->item(cat, 1)->text());
    check("so did the second",
          t->item(depth, 1)->text() == QStringLiteral("Profundidades de Hogar Precursor"),
          t->item(depth, 1)->text());
    delete d;
}

static void testHandEditingARowBreaksItsLink()
{
    std::cout << "\n[a hand-edited row stops following the name]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    TranslateDialogTestHook::names(d)      = {QStringLiteral("Forfeoranna Heim")};
    TranslateDialogTestHook::renderings(d) = {QString::fromUtf8("Hogar de los precursores")};

    const int cat  = rowOf(t, QStringLiteral("Forfeoranna Heim Catacombs"));
    const int name = rowOf(t, QStringLiteral("Forfeoranna Heim"));
    TranslateDialogTestHook::deliver(d, cat, QStringLiteral("Catacumbas de Nrvaa"));

    // The user rewrites this row themselves.
    t->item(cat, 1)->setText(QStringLiteral("Las Catacumbas"));
    // Now changing the name must NOT overwrite what they wrote.
    t->item(name, 1)->setText(QStringLiteral("Hogar Precursor"));

    check("the hand-edited row kept what the user typed",
          t->item(cat, 1)->text() == QStringLiteral("Las Catacumbas"),
          t->item(cat, 1)->text());
    delete d;
}


// -- What must never reach the translator ---------------------------------
//
// A Morrowind record out of Master Trainers, verbatim. Sent whole, the
// endpoint translates ALIGN and LEFT, accents COLOR, moves the tag to where
// Spanish wants the noun it thinks it is, and renames %Name - which is not a
// name but the variable the game substitutes into.

static const char *kRecord =
    "<DIV ALIGN=\"LEFT\"><FONT COLOR=\"000000\" SIZE=\"3\">"
    "%Name heard there is someone in the Temple who is a master of Restoration.";

static void testMarkupIsLiftedOut()
{
    std::cout << "\n[markup_protect: what gets held back]\n";
    const QString rec = QString::fromUtf8(kRecord);
    const QStringList spans = markup_protect::findSpans(rec);

    check("both tags and the variable are found", spans.size() == 3,
          QString::number(spans.size()));
    check("the tags come out whole, attributes and all",
          spans[0] == QLatin1String("<DIV ALIGN=\"LEFT\">")
              && spans[1] == QLatin1String("<FONT COLOR=\"000000\" SIZE=\"3\">"),
          spans.join(QLatin1Char('|')));
    check("and so does the substitution",
          spans[2] == QLatin1String("%Name"));

    const QString sent = markup_protect::mask(rec, spans);
    check("what is sent is a sentence",
          sent == QLatin1String("{0}{1}{2} heard there is someone in the Temple "
                                "who is a master of Restoration."),
          sent);

    // A percentage is prose. Freezing "100%" would leave it in English inside
    // a translated sentence, which is the mirror of the bug being fixed.
    const QString pct = QStringLiteral("Deal 100% more damage with a 50% chance");
    check("a percentage is not a variable",
          markup_protect::findSpans(pct).isEmpty());
    // A stray comparison is not a tag either: "<" with no closing ">" must not
    // swallow the rest of the line.
    check("an unclosed angle bracket is not markup",
          markup_protect::findSpans(QStringLiteral("if a < b then run")).isEmpty());
    check("an escape is held back",
          markup_protect::findSpans(QStringLiteral("one\\ntwo"))
              == QStringList{QStringLiteral("\\n")});

    // Nothing but markup: there is no sentence in it, and asking anyway spends
    // a request to get back a mangled tag.
    check("a record of pure markup has nothing to translate",
          markup_protect::isOnlySpans(
              markup_protect::mask(QStringLiteral("<DIV ALIGN=\"LEFT\"><BR>"),
                                   markup_protect::findSpans(
                                       QStringLiteral("<DIV ALIGN=\"LEFT\"><BR>")))));
    check("a record with words in it does not",
          !markup_protect::isOnlySpans(sent));
}

static void testTheSpansComeBack()
{
    std::cout << "\n[markup_protect: putting them back]\n";
    const QString rec = QString::fromUtf8(kRecord);
    const QStringList spans = markup_protect::findSpans(rec);

    {   // The ordinary case: every token came back where it was put.
        const auto r = markup_protect::restore(
            rec, QStringLiteral("{0}{1}{2} oyó que hay alguien en el Templo."), spans);
        check("a clean round trip needs no repair", !r.repaired && !r.lostInside);
        check("and the record opens exactly as it did",
              r.text.startsWith(QLatin1String("<DIV ALIGN=\"LEFT\">"
                                              "<FONT COLOR=\"000000\" SIZE=\"3\">%Name ")),
              r.text);
    }
    {   // Measured shapes the endpoint returns: a space inside the braces, or
        // the token spaced away from the words either side.
        const auto r = markup_protect::restore(
            rec, QStringLiteral("{ 0 }{1} {2} oyó que hay alguien."), spans);
        check("a spaced token is still ours", !r.repaired, r.text);
        check("and every tag is back",
              r.text.contains(QLatin1String("<DIV ALIGN=\"LEFT\">"))
                  && r.text.contains(QLatin1String("<FONT COLOR=\"000000\" SIZE=\"3\">"))
                  && r.text.contains(QLatin1String("%Name")));
    }
    {   // The failure this is really for: the tokens are simply gone.
        const auto r = markup_protect::restore(
            rec, QStringLiteral("oyó que hay alguien en el Templo."), spans);
        check("a lost token is noticed", r.repaired);
        // fromUtf8, not QLatin1String: the accent is two bytes in this file
        // and Latin-1 would read them as two characters.
        check("the record is rebuilt from its own tags",
              r.text == QString::fromUtf8("<DIV ALIGN=\"LEFT\"><FONT COLOR=\"000000\" "
                                          "SIZE=\"3\">%Name oyó que hay alguien en el Templo."),
              r.text);
        check("and the space the source had is not eaten",
              !r.text.contains(QLatin1String("%Nameoy")));
    }
    {   // A doubled token is as wrong as a missing one, and counting is what
        // catches it - looking only for presence would call this intact.
        const auto r = markup_protect::restore(
            rec, QStringLiteral("{0}{0}{1}{2} oyó que hay alguien."), spans);
        check("a doubled tag is caught too", r.repaired, r.text);
    }
    {   // A tag from the middle of a sentence has nowhere to go back to once
        // the words around it have been reordered. Say so rather than guess.
        const QString mid = QStringLiteral("Before <B>bold</B> after");
        const auto sp = markup_protect::findSpans(mid);
        const auto r  = markup_protect::restore(mid, QStringLiteral("Antes negrita despues"), sp);
        check("a tag lost from mid-sentence is reported, not invented",
              r.repaired && r.lostInside, r.text);
    }
    {   // Nothing to protect: the translation passes straight through.
        const auto r = markup_protect::restore(
            QStringLiteral("A great archer named Missun Akin."),
            QStringLiteral("Un gran arquero llamado Missun Akin."), {});
        check("a plain sentence is left alone",
              !r.repaired && r.text == QLatin1String("Un gran arquero llamado Missun Akin."));
    }
}


static void testTheSpinnerMarkDoesNotDetachTheRow()
{
    std::cout << "\n[the pending mark is not a user edit]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);
    auto *t = TranslateDialogTestHook::table(d);
    auto &names = TranslateDialogTestHook::names(d);
    auto &rend  = TranslateDialogTestHook::renderings(d);
    names = { QStringLiteral("Forfeoranna Heim") };
    rend  = { QStringLiteral("Hogar Forfeoranna") };

    const int cat = rowOf(t, QStringLiteral("Forfeoranna Heim Catacombs"));
    TranslateDialogTestHook::deliver(d, cat, QStringLiteral("Catacumbas de Nrvaa"));

    // The spinner writes to the same column the user types into. Marked
    // properly, the row keeps following its name.
    TranslateDialogTestHook::pending(d, cat, 2);
    TranslateDialogTestHook::pending(d, cat, 0);
    rend = { QStringLiteral("Hogar Precursor") };
    TranslateDialogTestHook::expand(d, cat);
    check("a guarded mark leaves the row following its name",
          t->item(cat, 1)->text() == QStringLiteral("Catacumbas de Hogar Precursor"),
          t->item(cat, 1)->text());

    // And unguarded it is the old bug again: the mark reads as the user
    // typing, the row detaches, and the next name change silently stops
    // reaching it. This is why setPending exists rather than a bare setData.
    TranslateDialogTestHook::pendingUnguarded(d, cat, 2);
    check("an unguarded one is what detaching looks like",
          t->item(cat, 1)->data(Qt::UserRole + 1).toString().isEmpty());
    delete d;
}


// -- Naming a hundred things the same way ---------------------------------
//
// Varieties of Faith calls its worship titles "<Deity> Devotee" nineteen
// times over. Google keeps the English word order and answers "Akatosh
// Devoto", which is not Spanish - it is "Devoto de Akatosh". A table of
// strings would have to list all nineteen and would still be wrong for the
// next mod's deity, so the rule is a shape.

static void testTheDevoteeShape()
{
    std::cout << "\n[translation_rules: whole-cell shapes]\n";
    const auto pats = lore_overrides::patternsFor(QStringLiteral("spanish"));
    check("Spanish has the shape", !pats.isEmpty());
    check("and a language with no table has none",
          lore_overrides::patternsFor(QStringLiteral("klingon")).isEmpty());

    auto shaped = [&pats](const char *src) {
        return translation_rules::applyPatterns(QString::fromUtf8(src), pats);
    };

    check("the reported case",
          shaped("Akatosh Devotee") == QString::fromUtf8("Devoto de Akatosh"),
          shaped("Akatosh Devotee"));
    check("and every other deity in the mod, unlisted",
          shaped("Cuhlecain Devotee") == QString::fromUtf8("Devoto de Cuhlecain")
              && shaped("Dibella Devotee") == QString::fromUtf8("Devoto de Dibella")
              && shaped("Zenithar Devotee") == QString::fromUtf8("Devoto de Zenithar"));

    // The capture is a name: it comes back exactly as written, punctuation,
    // spaces and capitals included.
    check("a name of two words survives whole",
          shaped("Tiber Septim Devotee") == QString::fromUtf8("Devoto de Tiber Septim"),
          shaped("Tiber Septim Devotee"));
    check("and one with a full stop in it",
          shaped("St. Pelinal Devotee") == QString::fromUtf8("Devoto de St. Pelinal"),
          shaped("St. Pelinal Devotee"));

    // The property that makes a shape safe to have at all. Prose needs the
    // translator that can see the grammar around it.
    check("a sentence that merely contains the words is left alone",
          shaped("He is an Akatosh Devotee, you know.").isEmpty());
    check("and so is one that ends elsewhere",
          shaped("The Akatosh Devotee said nothing.").isEmpty());
    check("a cell with nothing to capture does not match",
          shaped("Devotee").isEmpty());
}

static void testTheTwoThatTheShapeGetsWrong()
{
    std::cout << "\n[lore_overrides: exact entries beat the shape]\n";
    const QString sp = QStringLiteral("spanish");

    // "Talos Cult" is a faction, not a deity - the mod also has "Abandon the
    // Talos Cult" - so the shape would leave half of it in English.
    check("the faction gets its own answer",
          lore_overrides::lookup(QStringLiteral("Talos Cult Devotee"), sp)
              == QString::fromUtf8("Devoto del Culto de Talos"),
          lore_overrides::lookup(QStringLiteral("Talos Cult Devotee"), sp));
    // Written the other way round in the first place, so the shape never
    // matches it and it would have gone to the translator.
    check("and so does the one already in the of-form",
          lore_overrides::lookup(QStringLiteral("Devotee of The One"), sp)
              == QString::fromUtf8("Devoto del Único"));
    check("the shape does not match that one",
          translation_rules::applyPatterns(
              QStringLiteral("Devotee of The One"),
              lore_overrides::patternsFor(sp)).isEmpty());
}

static void testUserPatternsFromTheRulesFile()
{
    std::cout << "\n[translation_rules: patterns from the file]\n";
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("spanish.txt"));

    {
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write("[patterns]\n"
                "%1 Devotee = Fiel de %1\n"
                "Altar of %1=Altar de %1\n"
                "# a shape with no hole is a [terms] entry in the wrong place\n"
                "Chest = Cofre\n");
    }
    const auto rules = translation_rules::load(path);
    check("both shapes are read", rules.patterns.size() == 2,
          QString::number(rules.patterns.size()));
    check("a line with no hole in it is not taken as one",
          !rules.isEmpty() && rules.patterns.size() == 2);

    check("the user's shape answers",
          translation_rules::applyPatterns(QStringLiteral("Akatosh Devotee"),
                                           rules.patterns)
              == QStringLiteral("Fiel de Akatosh"));
    check("and so does a second one",
          translation_rules::applyPatterns(QStringLiteral("Altar of Mara"),
                                           rules.patterns)
              == QStringLiteral("Altar de Mara"));
    // The dialog tries the user's list before the built-in one, so a user who
    // prefers "Fiel" gets it everywhere without touching the binary.
    check("the user's wording differs from the built-in on purpose",
          translation_rules::applyPatterns(
              QStringLiteral("Akatosh Devotee"),
              lore_overrides::patternsFor(QStringLiteral("spanish")))
              == QString::fromUtf8("Devoto de Akatosh"));

    // The template the "Edit rules..." button writes has to parse back, or
    // the section it advertises does not exist as far as load() is concerned.
    const QString tpl = dir.filePath(QStringLiteral("template.txt"));
    check("the written template mentions the section",
          translation_rules::ensureTemplate(tpl, QStringLiteral("spanish")));
    QFile tf(tpl);
    tf.open(QIODevice::ReadOnly | QIODevice::Text);
    check("and it is spelled the way load() looks for it",
          QString::fromUtf8(tf.readAll()).contains(QLatin1String("[patterns]")));
}


// -- A name is not a phrase -----------------------------------------------
//
// Sixth House Obsidian Weapon names nine creatures "Dagoth <something>".
// "Dagoth" repeats, so it was found and masked; the second word appears once
// and was not, so the row went to the translator anyway. Measured against the
// live endpoint, that is what it does with them:
//
//   sl=auto  "Dagoth Andas" -> "Dagoth respira"
//   sl=auto  "Nrvaa Andas"  -> "Relajate Respira"
//   sl=en    "Dagoth Andas" -> "Dagoth Andas"
//   sl=en    "Nrvaa Andas"  -> "Nrvaa Andas"

static void testWhatReadsAsAName()
{
    std::cout << "\n[term_protect::looksLikeName]\n";
    const QStringList names = { QStringLiteral("Dagoth") };

    check("a found name beside an unknown word",
          term_protect::looksLikeName(QStringLiteral("Dagoth Andas"), names));
    check("two unknown words with no found name between them",
          term_protect::looksLikeName(QStringLiteral("Akin Benammu"), {}));
    check("and a faction nobody has heard of",
          term_protect::looksLikeName(QStringLiteral("Camonna Tong"), {}));

    // The other half, and the half that matters more: these have to keep
    // going to the translator.
    check("a description made of known words does not",
          !term_protect::looksLikeName(QStringLiteral("Chest Key"), names));
    check("nor a longer one",
          !term_protect::looksLikeName(QStringLiteral("Common Hooded Robe"), names));
    check("nor a creature type",
          !term_protect::looksLikeName(QStringLiteral("Ash Slave"), names));
    check("nor a rank",
          !term_protect::looksLikeName(QStringLiteral("House Brother"), names));
    check("nor a race",
          !term_protect::looksLikeName(QStringLiteral("Dark Elf"), names));
    check("nor a sentence",
          !term_protect::looksLikeName(
              QStringLiteral("Dagoth Andas guards the shrine."), names));

    // One unknown word on its own is not evidence, and it must not become
    // evidence merely because some OTHER row in the mod held a name.
    check("a lone unknown word is not a name",
          !term_protect::looksLikeName(QStringLiteral("Abinabi"), {}));
    check("and having found a name elsewhere does not change that",
          !term_protect::looksLikeName(QStringLiteral("Abinabi"), names));

    // The escape hatch when the judgement is wrong for a language.
    QSet<QString> ordinary;
    ordinary.insert(QStringLiteral("andas"));
    check("[ordinary] turns a held-back word back into a word",
          !term_protect::looksLikeName(QStringLiteral("Dagoth Andas"), names, ordinary));
}

static void testTheMorrowindNamingFamilies()
{
    std::cout << "\n[lore_overrides: naming families]\n";
    const auto pats = lore_overrides::patternsFor(QStringLiteral("spanish"));
    auto shaped = [&pats](const char *src) {
        return translation_rules::applyPatterns(QString::fromUtf8(src), pats);
    };

    // Each maps to itself: there is no Spanish in these to get wrong, and
    // that is the point. Counted across the author's mods, the second word is
    // different nearly every time - Dagoth 17, Tel 24, Ald 15, Clan 9.
    check("the reported rows are left alone",
          shaped("Dagoth Andas") == QLatin1String("Dagoth Andas")
              && shaped("Dagoth Balen") == QLatin1String("Dagoth Balen")
              && shaped("Dagoth Ilet") == QLatin1String("Dagoth Ilet"));
    check("and the other three families",
          shaped("Tel Fyr") == QLatin1String("Tel Fyr")
              && shaped("Ald Velothi") == QLatin1String("Ald Velothi")
              && shaped("Clan Aundae") == QLatin1String("Clan Aundae"),
          shaped("Tel Fyr"));

    // Deliberately not a family: "House" carries both places and ranks, so a
    // shape would freeze the half that has to be translated.
    check("House is not a shape", shaped("House Brother").isEmpty());
    check("but the great houses are entries",
          lore_overrides::lookup(QStringLiteral("House Hlaalu"),
                                 QStringLiteral("spanish"))
              == QLatin1String("Casa Hlaalu"));
    check("and House Brother is not one of them",
          lore_overrides::lookup(QStringLiteral("House Brother"),
                                 QStringLiteral("spanish")).isEmpty());

    // A sentence is still a sentence.
    check("prose is untouched by any of them",
          shaped("Dagoth Ur waits in the heart of Red Mountain.").isEmpty());
}

// -- the run's own bookkeeping ----------------------------------------

static void testABlockEmptiesBothQueues()
{
    std::cout << "\n[a block stops the run, not just the current pass]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);

    TranslateDialogTestHook::arm(d, {0, 1}, {2, 3}, /*namePhase=*/true);
    TranslateDialogTestHook::blockNow(d);
    check("the queue is empty", TranslateDialogTestHook::queued(d) == 0);
    // The one that bites: a surviving m_mtPending lets the phase switch start
    // pass two straight after pass one was abandoned.
    check("and so is the second pass", TranslateDialogTestHook::pendingRows(d) == 0);
    check("the run is marked stopped", TranslateDialogTestHook::stopped(d));

    TranslateDialogTestHook::advance(d);
    check("advancing does not start pass two",
          TranslateDialogTestHook::queued(d) == 0);
    check("it ends the run instead", TranslateDialogTestHook::buttonOn(d));
    delete d;
}

static void testTeardownPutsEverythingBack()
{
    std::cout << "\n[the teardown leaves nothing running]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);

    TranslateDialogTestHook::arm(d, {0, 1}, {}, false);
    TranslateDialogTestHook::pending(d, 0, 1);
    TranslateDialogTestHook::pending(d, 1, 2);
    TranslateDialogTestHook::blockNow(d);
    TranslateDialogTestHook::finish(d);

    check("the button works again", TranslateDialogTestHook::buttonOn(d));
    check("the pacer is stopped", !TranslateDialogTestHook::paceOn(d));
    check("so is the spinner", !TranslateDialogTestHook::animOn(d));
    // Rows a stopped run never reached would otherwise spin forever.
    check("no row is left waiting",
          TranslateDialogTestHook::pendingState(d, 0) == 0
              && TranslateDialogTestHook::pendingState(d, 1) == 0);
    // Forget this one and the button never does anything again for the rest
    // of the dialog's life.
    check("and the stopped flag is cleared for the next run",
          !TranslateDialogTestHook::stopped(d));
    delete d;
}

static void testARunThatSendsNothingStillEnds()
{
    std::cout << "\n[a run with nothing left to send still finishes]\n";
    // The latent bug: markup-only rows are skipped without a request, so a run
    // whose remaining rows are all markup emptied the queue with nothing in
    // flight - and the completion path lived only in the reply handler, which
    // no longer had a reply coming. Button disabled, spinner turning, forever.
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);

    TranslateDialogTestHook::arm(d, {}, {}, false);
    TranslateDialogTestHook::advance(d);
    check("the run ends", TranslateDialogTestHook::buttonOn(d));
    check("with nothing left ticking", !TranslateDialogTestHook::paceOn(d));
    delete d;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    std::cout << "=== translate dialog UI ===\n";
    testDeliveredRowIsNotBlank();
    testUnguardedWriteIsWhatBlankedIt();
    testABlockEmptiesBothQueues();
    testTeardownPutsEverythingBack();
    testARunThatSendsNothingStillEnds();
    testEditingTheNameRerendersEveryRow();
    testHandEditingARowBreaksItsLink();
    testMarkupIsLiftedOut();
    testTheSpansComeBack();
    testTheSpinnerMarkDoesNotDetachTheRow();
    testTheDevoteeShape();
    testTheTwoThatTheShapeGetsWrong();
    testUserPatternsFromTheRulesFile();
    testWhatReadsAsAName();
    testTheMorrowindNamingFamilies();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
