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
#include "translation_progress.h"
#include "translate_dialog.h"

#include <QPushButton>
#include <QTimer>
#include <QProgressBar>
#include <QCheckBox>
#include <QStandardPaths>
#include <QSettings>
#include <QDateTime>
#include "settings.h"
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
    // What the reply handler does when the endpoint answers 429, stamp included.
    static void blockNow(TranslateDialog *d)
    {
        d->m_mtStopped = true;
        d->m_mtQueue.clear();
        d->m_mtPending.clear();
        if (d->m_mtPace) d->m_mtPace->stop();
        Settings::setTranslateBlockedAt(QDateTime::currentDateTimeUtc());
    }
    static int  cooloffLeft(TranslateDialog *d) { return d->cooloffLeftSeconds(); }
    static void refreshCooloff(TranslateDialog *d) { d->updateCooloffDisplay(); }
    static QString barFormat(TranslateDialog *d) { return d->m_mtBar->format(); }

    // -- paging ------------------------------------------------------
    static void setPage(TranslateDialog *d, int p) { d->showPage(p); }
    static int  page(TranslateDialog *d)      { return d->m_page; }
    static int  pageCount(TranslateDialog *d) { return d->pageCount(); }
    static QList<int> visible(TranslateDialog *d) { return d->m_visible; }
    static void setFilter(TranslateDialog *d, bool on)
    {
        d->m_todoOnly->setChecked(on);   // its own toggled() rebuilds and repages
    }
    static void rebuild(TranslateDialog *d)   { d->rebuildVisible(); }
    static bool answered(TranslateDialog *d, int r) { return d->rowAnswered(r); }
    static void review(TranslateDialog *d, int r, bool v) { d->setReviewed(r, v); }
    static bool reviewed(TranslateDialog *d, int r)
    { return d->m_table->item(r, 1)->data(Qt::UserRole + 4).toBool(); }
    static int  nextVis(TranslateDialog *d, int r) { return d->nextVisible(r); }
    static int  prevVis(TranslateDialog *d, int r) { return d->prevVisible(r); }
    static QPair<int,int> pos(TranslateDialog *d, int r)
    { return d->visiblePosition(r); }
    static int  pageSize() { return TranslateDialog::kPageSize; }

    // -- saving and the memory gate ----------------------------------
    static TranslateDialog *makeAt(const QList<TranslatableString> &strings,
                                   translation_store::Memory *mem,
                                   const QString &progressPath)
    {
        return new TranslateDialog(QStringLiteral("Project Cyrodiil"),
                                   strings, QStringLiteral("spanish"), mem,
                                   QString(), progressPath);
    }
    static bool write(TranslateDialog *d) { return d->writeProgress(); }
    static TranslateDialog::AcceptPlan plan(TranslateDialog *d)
    { return d->planAccept(); }
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

static void testABlockArmsTheCooloff()
{
    std::cout << "\n[a block is remembered, and it runs out]\n";
    translation_store::Memory mem;
    Settings::setTranslateBlockedAt(QDateTime());        // clean slate
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);

    check("nothing is blocked to begin with",
          TranslateDialogTestHook::cooloffLeft(d) == 0);

    TranslateDialogTestHook::arm(d, {0}, {}, false);
    TranslateDialogTestHook::blockNow(d);
    const int left = TranslateDialogTestHook::cooloffLeft(d);
    check("a block starts the wait", left > 0, QString::number(left));
    check("and it is the fifteen minutes asked for, not more",
          left <= google_translate::kBlockCooloffMinutes * 60,
          QString::number(left));

    // The block outlives the dialog, so it is stored rather than held here.
    check("it survives into Settings",
          Settings::translateBlockedAt().isValid());

    // A block from before the window closed must not gate a fresh run.
    Settings::setTranslateBlockedAt(
        QDateTime::currentDateTimeUtc().addSecs(
            -(google_translate::kBlockCooloffMinutes * 60 + 60)));
    check("a lapsed block gates nothing",
          TranslateDialogTestHook::cooloffLeft(d) == 0);
    delete d;
}

static void testTheCountdownSaysHowLong()
{
    std::cout << "\n[the wait is shown, not just enforced]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);

    // Asserted against QProgressBar's own default rather than against wording:
    // Translator::init never runs here, so T() hands back the key itself and a
    // test that looked for "14:32" would only be testing the ini file.
    //
    // format(), not isVisible(): the dialog is never show()n in these tests, so
    // every child reports invisible and the check would pass vacuously.
    const QString kDefault = QStringLiteral("%p%");

    Settings::setTranslateBlockedAt(QDateTime::currentDateTimeUtc());
    TranslateDialogTestHook::refreshCooloff(d);
    const QString fmt = TranslateDialogTestHook::barFormat(d);
    check("the bar stops showing a percentage and says something else",
          !fmt.isEmpty() && fmt != kDefault, fmt);

    Settings::setTranslateBlockedAt(QDateTime());
    TranslateDialogTestHook::refreshCooloff(d);
    check("and goes back to normal once the block lapses",
          TranslateDialogTestHook::barFormat(d) == kDefault,
          TranslateDialogTestHook::barFormat(d));
    delete d;
}

// -- translation_progress: half-finished work that survives a month -----

static void testProgressRoundTripsBothFlags()
{
    std::cout << "\n[an answer, and whether anybody has read it]\n";
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("p.json"));

    translation_progress::Progress out;
    out.setMod(QStringLiteral("Project Cyrodiil"), QStringLiteral("spanish"));
    out.record(QStringLiteral("Bandit Chief"), QStringLiteral("Líder Bandido"), true);
    out.record(QStringLiteral("Nibenay Basin"), QStringLiteral("Cuenca"), false);
    check("it writes", out.save(path));

    translation_progress::Progress back;
    check("and reads", back.load(path));
    check("the reviewed answer survives",
          back.lookup(QStringLiteral("Bandit Chief")).translation
              == QStringLiteral("Líder Bandido"));
    check("still marked read",
          back.lookup(QStringLiteral("Bandit Chief")).reviewed);
    // The whole point of the flag: a machine guess must stay flagged across a
    // month of sessions, or it slips into the shared memory unread.
    check("and the unread one is still unread",
          !back.lookup(QStringLiteral("Nibenay Basin")).reviewed);
    check("the mod is named in the file",
          back.mod() == QStringLiteral("Project Cyrodiil")
              && back.language() == QStringLiteral("spanish"));
}

static void testProgressResumesByTextNotByRow()
{
    std::cout << "\n[the mod was updated and every row moved]\n";
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("p.json"));

    translation_progress::Progress out;
    out.record(QStringLiteral("Bandit Chief"), QStringLiteral("Líder"), true);
    out.record(QStringLiteral("Zealot"), QStringLiteral("Fanático"), true);
    out.save(path);

    // The mod gains "Alit", which sorts first - so every row index shifts by
    // one. Anything keyed on the row would now answer the wrong string.
    translation_progress::Progress back;
    back.load(path);
    check("the first answer still finds its string",
          back.lookup(QStringLiteral("Bandit Chief")).translation
              == QStringLiteral("Líder"));
    check("and so does the second",
          back.lookup(QStringLiteral("Zealot")).translation
              == QStringLiteral("Fanático"));
    check("the new string is simply unanswered",
          back.lookup(QStringLiteral("Alit")).translation.isEmpty());
}

static void testAnAnswerForAVanishedStringIsKept()
{
    std::cout << "\n[a string the mod no longer has]\n";
    translation_progress::Progress p;
    p.record(QStringLiteral("Bandit Chief"), QStringLiteral("Líder"), true);
    p.record(QStringLiteral("Cut Content"), QStringLiteral("Recortado"), true);

    const QStringList live = {QStringLiteral("Bandit Chief")};
    check("it is counted as stale", p.staleAgainst(live) == 1);
    // Never pruned: the next update may bring it back, and a month of typing
    // is worth more than the bytes.
    check("but the answer is still there",
          !p.lookup(QStringLiteral("Cut Content")).translation.isEmpty());
    check("nothing stale means nothing reported",
          p.staleAgainst({QStringLiteral("Bandit Chief"),
                          QStringLiteral("Cut Content")}) == 0);
}

static void testProgressNormalisesLikeTheMemory()
{
    std::cout << "\n[case and stray spaces are the same string]\n";
    translation_progress::Progress p;
    p.record(QStringLiteral("Bandit Chief"), QStringLiteral("Líder"), true);
    check("case-folded", !p.lookup(QStringLiteral("bandit chief")).translation.isEmpty());
    check("and trimmed", !p.lookup(QStringLiteral("  Bandit Chief ")).translation.isEmpty());
    check("one entry, not three", p.size() == 1);

    // A blank answer is not an answer - storing one would make the row read as
    // done to the counter and the filter.
    p.record(QStringLiteral("Bandit Chief"), QString(), true);
    check("blanking an answer forgets it", p.empty());
}

static void testProgressFileNamesCannotCollide()
{
    std::cout << "\n[two mods whose names fold together]\n";
    const QString a = translation_progress::fileNameFor(
        QStringLiteral("Mod: A"), QStringLiteral("spanish"));
    const QString b = translation_progress::fileNameFor(
        QStringLiteral("Mod  A"), QStringLiteral("spanish"));
    check("they get different files", a != b, a + " vs " + b);
    check("and different languages do too",
          a != translation_progress::fileNameFor(QStringLiteral("Mod: A"),
                                                 QStringLiteral("french")));
    check("the name is filesystem-safe",
          !a.contains(QLatin1Char('/')) && !a.contains(QLatin1Char(':')), a);
    check("and the same input is stable",
          a == translation_progress::fileNameFor(QStringLiteral("Mod: A"),
                                                 QStringLiteral("spanish")));
}

static void testAProgressFileReadsAsAMemory()
{
    std::cout << "\n[a half-finished job is a memory with a to-do list]\n";
    // Deliberate schema compatibility - a later refactor must not quietly
    // break it, because renaming one file is how a memory is recovered from
    // an abandoned translation.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("p.json"));
    translation_progress::Progress p;
    p.record(QStringLiteral("Bandit Chief"), QStringLiteral("Líder"), true);
    p.save(path);

    translation_store::Memory mem;
    check("a memory can read it", mem.load(path));
    check("and finds the answer",
          mem.lookup(QStringLiteral("Bandit Chief")) == QStringLiteral("Líder"));
}

static void testMissingProgressIsNotAnError()
{
    std::cout << "\n[nothing saved yet]\n";
    translation_progress::Progress p;
    check("a missing file loads as empty progress",
          p.load(QStringLiteral("/nonexistent/nrv_progress_test.json")));
    check("and it is empty", p.empty());
}

// A mod big enough to need more than one page.
static QList<TranslatableString> manyStrings(int n)
{
    QList<TranslatableString> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        // Zero-padded so the case-insensitive sort the dialog applies keeps
        // the order this generates them in - the tests index by row.
        out.append({QStringLiteral("plugin.esp"),
                    QStringLiteral("NPC_:%1:FNAM:0").arg(i),
                    QStringLiteral("String %1").arg(i, 5, 10, QLatin1Char('0')),
                    false});
    }
    return out;
}

static void testEveryRowStillExistsWhenPaged()
{
    std::cout << "\n[paging hides rows, it does not throw them away]\n";
    translation_store::Memory mem;
    const int n = TranslateDialogTestHook::pageSize() * 3;
    auto *d = TranslateDialogTestHook::make(manyStrings(n), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    TranslateDialogTestHook::setPage(d, 1);
    // The invariant the whole design rests on. onAccept walks m_rowSource and
    // dereferences item(i, 1) with no null check, and the machine-translate
    // queues hold row numbers - a table holding only the current page would
    // hand both of them a null pointer.
    check("every row is still in the table", t->rowCount() == n,
          QString::number(t->rowCount()));
    check("including one on the last page", t->item(n - 1, 1) != nullptr);
    check("and one on the first", t->item(0, 1) != nullptr);
    check("three pages", TranslateDialogTestHook::pageCount(d) == 3,
          QString::number(TranslateDialogTestHook::pageCount(d)));
    delete d;
}

static void testOnlyThisPageIsOnScreen()
{
    std::cout << "\n[what is on screen is one page of it]\n";
    translation_store::Memory mem;
    const int size = TranslateDialogTestHook::pageSize();
    auto *d = TranslateDialogTestHook::make(manyStrings(size * 2), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    TranslateDialogTestHook::setPage(d, 0);
    check("the first row of page one shows", !t->isRowHidden(0));
    check("the last row of page one shows", !t->isRowHidden(size - 1));
    check("page two is hidden", t->isRowHidden(size));

    TranslateDialogTestHook::setPage(d, 1);
    check("now page one is hidden", t->isRowHidden(0));
    check("and page two shows", !t->isRowHidden(size));

    TranslateDialogTestHook::setPage(d, 99);
    check("a page past the end clamps",
          TranslateDialogTestHook::page(d) == 1,
          QString::number(TranslateDialogTestHook::page(d)));
    delete d;
}

static void testAnAnswerLandsOnAnOffPageRow()
{
    std::cout << "\n[a two-hour run outlives the page you are looking at]\n";
    translation_store::Memory mem;
    const int size = TranslateDialogTestHook::pageSize();
    auto *d = TranslateDialogTestHook::make(manyStrings(size * 2), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    TranslateDialogTestHook::setPage(d, 1);          // looking at page two
    TranslateDialogTestHook::deliver(d, 0, QStringLiteral("Cadena cero"));
    check("the answer reached the row anyway",
          t->item(0, 1)->text() == QStringLiteral("Cadena cero"),
          t->item(0, 1)->text());
    check("and the row is still off-screen", t->isRowHidden(0));
    delete d;
}

static void testTheFilterOffersOnlyWhatStillNeedsWork()
{
    std::cout << "\n[show only the rows that still need me]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(manyStrings(10), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    t->item(0, 1)->setText(QStringLiteral("hecho"));   // unguarded = a hand edit
    t->item(1, 1)->setText(QStringLiteral("hecho"));
    TranslateDialogTestHook::setFilter(d, true);
    check("the answered rows drop out",
          TranslateDialogTestHook::visible(d).size() == 8,
          QString::number(TranslateDialogTestHook::visible(d).size()));
    check("and the ones left are the unanswered ones",
          !TranslateDialogTestHook::visible(d).contains(0)
              && TranslateDialogTestHook::visible(d).contains(2));

    // A machine answer nobody has read still needs the user - a filter that
    // only asked "has text" would hide the whole mod after an MT run, which
    // is exactly when it is most wanted.
    TranslateDialogTestHook::deliver(d, 2, QStringLiteral("adivinado"));
    TranslateDialogTestHook::rebuild(d);
    check("an unread machine answer still counts as needing me",
          TranslateDialogTestHook::visible(d).contains(2));

    TranslateDialogTestHook::review(d, 2, true);
    TranslateDialogTestHook::rebuild(d);
    check("once read, it drops out too",
          !TranslateDialogTestHook::visible(d).contains(2));

    TranslateDialogTestHook::setFilter(d, false);
    check("turning the filter off shows everything again",
          TranslateDialogTestHook::visible(d).size() == 10);
    delete d;
}

static void testTheFilterDoesNotYankARowFromUnderTheCursor()
{
    std::cout << "\n[answering a row does not make it vanish mid-edit]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(manyStrings(10), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    TranslateDialogTestHook::setFilter(d, true);
    const int before = int(TranslateDialogTestHook::visible(d).size());
    t->item(3, 1)->setText(QStringLiteral("escrito"));
    // m_visible is a snapshot on purpose. Someone will one day "fix" this to
    // update live because a row staying put after being answered looks like a
    // bug; it is not, and this is why.
    check("the row is still on offer until an explicit rebuild",
          TranslateDialogTestHook::visible(d).size() == before
              && TranslateDialogTestHook::visible(d).contains(3));
    check("and still on screen", !t->isRowHidden(3));

    TranslateDialogTestHook::rebuild(d);
    check("a rebuild is what drops it",
          !TranslateDialogTestHook::visible(d).contains(3));
    delete d;
}

static void testTheRowEditorWalksTheOfferedRows()
{
    std::cout << "\n[Previous and Next follow the filter, not the table]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(manyStrings(10), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    check("unfiltered, next is the next row",
          TranslateDialogTestHook::nextVis(d, 3) == 4);
    check("and previous the previous", TranslateDialogTestHook::prevVis(d, 3) == 2);
    check("the first row has no previous",
          TranslateDialogTestHook::prevVis(d, 0) == -1);
    check("the last has no next", TranslateDialogTestHook::nextVis(d, 9) == -1);
    check("and the caption counts the whole list",
          TranslateDialogTestHook::pos(d, 3) == qMakePair(4, 10));

    t->item(4, 1)->setText(QStringLiteral("hecho"));
    TranslateDialogTestHook::setFilter(d, true);
    check("Next steps over a row the filter is hiding",
          TranslateDialogTestHook::nextVis(d, 3) == 5,
          QString::number(TranslateDialogTestHook::nextVis(d, 3)));
    check("and the caption counts what is on offer",
          TranslateDialogTestHook::pos(d, 3) == qMakePair(4, 9));
    delete d;
}

static void testAMachineGuessNeverReachesTheMemory()
{
    std::cout << "\n[an unread guess ships, but is not remembered]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(manyStrings(4), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    TranslateDialogTestHook::deliver(d, 0, QStringLiteral("adivinado"));  // machine
    t->item(1, 1)->setText(QStringLiteral("escrito"));                    // typed

    const auto plan = TranslateDialogTestHook::plan(d);
    check("the typed answer is remembered",
          !mem.lookup(QStringLiteral("String 00001")).isEmpty());
    // The whole point: one bad guess about a common string must not pre-fill
    // every other mod the user ever opens.
    check("the machine guess is NOT",
          mem.lookup(QStringLiteral("String 00000")).isEmpty(),
          mem.lookup(QStringLiteral("String 00000")));
    // But it still goes into the mod - refusing it would hand back an empty
    // mod to somebody who just machine-translated the lot.
    check("both still ship into the mod", plan.byText.size() == 2,
          QString::number(plan.byText.size()));
    check("and the split is reported",
          plan.remembered == 1 && plan.unreviewed == 1);
    delete d;
}

static void testReadingAGuessLetsItIntoTheMemory()
{
    std::cout << "\n[once somebody has read it, it can be remembered]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(manyStrings(2), &mem);

    TranslateDialogTestHook::deliver(d, 0, QStringLiteral("adivinado"));
    check("unread to begin with", !TranslateDialogTestHook::reviewed(d, 0));
    TranslateDialogTestHook::review(d, 0, true);
    TranslateDialogTestHook::plan(d);
    check("now it is remembered",
          !mem.lookup(QStringLiteral("String 00000")).isEmpty());
    delete d;
}

static void testAGuardedWriteDoesNotVouchForARow()
{
    std::cout << "\n[the app writing is not the user reading]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(manyStrings(3), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    TranslateDialogTestHook::deliver(d, 0, QStringLiteral("del robot"));
    check("a guarded write leaves the row unread",
          !TranslateDialogTestHook::reviewed(d, 0));
    t->item(1, 1)->setText(QStringLiteral("a mano"));
    check("an unguarded one vouches for it",
          TranslateDialogTestHook::reviewed(d, 1));
    // A blank row can never be "read": it would count as done to the counter
    // and drop out of the filter with nothing in it.
    TranslateDialogTestHook::review(d, 2, true);
    check("and a blank row refuses to be marked read",
          !TranslateDialogTestHook::reviewed(d, 2));
    delete d;
}

static void testProgressSurvivesClosingTheDialog()
{
    std::cout << "\n[fifteen today, fifteen tomorrow]\n";
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("cyrodiil.json"));
    const auto strings = manyStrings(5);

    {   // Monday.
        translation_store::Memory mem;
        auto *d = TranslateDialogTestHook::makeAt(strings, &mem, path);
        auto *t = TranslateDialogTestHook::table(d);
        t->item(0, 1)->setText(QStringLiteral("lunes"));
        TranslateDialogTestHook::deliver(d, 1, QStringLiteral("del robot"));
        check("it writes", TranslateDialogTestHook::write(d));
        delete d;
    }
    {   // Tuesday, a fresh dialog on the same mod.
        translation_store::Memory mem;
        auto *d = TranslateDialogTestHook::makeAt(strings, &mem, path);
        auto *t = TranslateDialogTestHook::table(d);
        check("yesterday's typing is back",
              t->item(0, 1)->text() == QStringLiteral("lunes"),
              t->item(0, 1)->text());
        check("and it is still vouched for",
              TranslateDialogTestHook::reviewed(d, 0));
        check("the machine guess is back too",
              t->item(1, 1)->text() == QStringLiteral("del robot"));
        // If this flipped to read across a save, a month of unread guesses
        // would quietly join the shared memory.
        check("and still unread",
              !TranslateDialogTestHook::reviewed(d, 1));
        check("the untouched rows are still blank",
              t->item(2, 1)->text().isEmpty());
        delete d;
    }
}

static void testAMemoryHitDoesNotClobberRestoredWork()
{
    std::cout << "\n[the shared memory does not overwrite this mod's own work]\n";
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("p.json"));
    const auto strings = manyStrings(2);

    {
        translation_store::Memory mem;
        auto *d = TranslateDialogTestHook::makeAt(strings, &mem, path);
        TranslateDialogTestHook::table(d)->item(0, 1)
            ->setText(QStringLiteral("lo que escribi aqui"));
        TranslateDialogTestHook::write(d);
        delete d;
    }
    // Some other mod taught the shared memory a different answer for the same
    // English. This mod's own saved work must win.
    translation_store::Memory mem;
    mem.remember(QStringLiteral("String 00000"), QStringLiteral("de otro mod"));
    auto *d = TranslateDialogTestHook::makeAt(strings, &mem, path);
    check("the saved answer survives the memory fill",
          TranslateDialogTestHook::table(d)->item(0, 1)->text()
              == QStringLiteral("lo que escribi aqui"),
          TranslateDialogTestHook::table(d)->item(0, 1)->text());
    delete d;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    // The dialog reads and writes Settings now (the machine-translate cooloff).
    // Without this the suite would stamp a real block into the developer's own
    // config and lock their machine translation for fifteen minutes.
    QCoreApplication::setOrganizationName(QStringLiteral("nerevarine-test"));
    QCoreApplication::setApplicationName(QStringLiteral("translate-ui-test"));
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();

    std::cout << "=== translate dialog UI ===\n";
    testDeliveredRowIsNotBlank();
    testUnguardedWriteIsWhatBlankedIt();
    testABlockEmptiesBothQueues();
    testTeardownPutsEverythingBack();
    testARunThatSendsNothingStillEnds();
    testABlockArmsTheCooloff();
    testTheCountdownSaysHowLong();
    testProgressRoundTripsBothFlags();
    testProgressResumesByTextNotByRow();
    testAnAnswerForAVanishedStringIsKept();
    testProgressNormalisesLikeTheMemory();
    testProgressFileNamesCannotCollide();
    testAProgressFileReadsAsAMemory();
    testMissingProgressIsNotAnError();
    testEveryRowStillExistsWhenPaged();
    testOnlyThisPageIsOnScreen();
    testAnAnswerLandsOnAnOffPageRow();
    testTheFilterOffersOnlyWhatStillNeedsWork();
    testTheFilterDoesNotYankARowFromUnderTheCursor();
    testTheRowEditorWalksTheOfferedRows();
    testAMachineGuessNeverReachesTheMemory();
    testReadingAGuessLetsItIntoTheMemory();
    testAGuardedWriteDoesNotVouchForARow();
    testProgressSurvivesClosingTheDialog();
    testAMemoryHitDoesNotClobberRestoredWork();
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
