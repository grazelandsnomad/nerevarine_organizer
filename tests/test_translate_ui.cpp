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
#include "vanilla_gmst.h"
#include "google_translate.h"
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

    // The vouch click, exactly as the cellClicked lambda runs it.
    static void vouch(TranslateDialog *d, int row)
    {
        auto *cell = d->m_table->item(row, 1);
        if (!cell || cell->text().trimmed().isEmpty()) return;
        d->setReviewed(row, !cell->data(Qt::UserRole + 4).toBool());
        d->m_progressDirty = true;
    }
    static bool dirty(TranslateDialog *d) { return d->m_progressDirty; }
    static void setDirty(TranslateDialog *d, bool v) { d->m_progressDirty = v; }

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
    // The REAL write path a reply takes, now that a reply carries a batch.
    // deliver() above mirrors it; this one is the code that actually ships.
    static void apply(TranslateDialog *d, int item, int nameIdx, bool isName,
                      const QString &named, const QString &raw)
    {
        d->applyMachineAnswer(item, nameIdx, isName, named,
                              markup_protect::findSpans(named), raw);
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
    // The real slots, so what the tests pin down is what runs.
    static void build(TranslateDialog *d) { d->onAccept(); }
    static void saveProgress(TranslateDialog *d) { d->onSaveProgress(); }
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
        // Asserted rather than ignored: a rules file that never opened parses
        // as an empty rule set, which would fail the checks below for a
        // reason that has nothing to do with what they are testing.
        check("the rules file opens for writing",
              f.open(QIODevice::WriteOnly | QIODevice::Text), path);
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
    check("the written template opens for reading",
          tf.open(QIODevice::ReadOnly | QIODevice::Text), tpl);
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

// The Ashlanders: forty strings, nineteen of them one Ashlander's name each,
// every one appearing exactly ONCE - which repetition cannot see. All nineteen
// went to the translator and earned a rate-limit block before the rows with
// real sentences in them were ever reached.
static void testAKnownNameNeedsNoRepetition()
{
    std::cout << "\n[term_protect: the built-in names]\n";

    // Verbatim from the mod's own string list.
    const QStringList sources = {
        QStringLiteral("Shalapli"),
        QStringLiteral("Shalibi"),
        QStringLiteral("Shanbaal"),
        QStringLiteral("Shinat"),
        QStringLiteral("Shulhaz"),
        QStringLiteral("Yalit"),
        QStringLiteral("Chitin Quiver"),
        QStringLiteral("Boiling Pot"),
        QStringLiteral("Ashlander Tent"),
        QStringLiteral("Domesticated Guar"),
        QStringLiteral("Herder's Whip"),
        QStringLiteral("Sinnammu Mirpal says I can mix marshmerrow and "
                       "wickwheat to make a poultice."),
    };
    const QStringList names = term_protect::findNames(sources);

    for (const char *n : {"Shalapli", "Shalibi", "Shanbaal",
                          "Shinat", "Shulhaz", "Yalit"}) {
        const QString name = QString::fromLatin1(n);
        check("a name appearing once is protected anyway",
              names.contains(name), name);
        // The whole point: nothing in this row to translate, so it is never
        // sent. This is the request that was being spent on a proper noun.
        check("and a row that is only that name is held back",
              term_protect::looksLikeName(name, names), name);
    }

    check("a name inside a sentence is protected too",
          names.contains(QStringLiteral("Sinnammu Mirpal")),
          names.join(QStringLiteral(", ")));
    check("but the sentence around it still goes to the translator",
          !term_protect::looksLikeName(sources.last(), names));

    // The line the list must not cross. A quiver is a thing, not a person.
    check("an ordinary noun is not treated as a name",
          !names.contains(QStringLiteral("Quiver")),
          names.join(QStringLiteral(", ")));
    check("so it is still translated",
          !term_protect::looksLikeName(QStringLiteral("Chitin Quiver"), names));

    // The other half, and it was already broken before the names went in:
    // every one of these was read as somebody's name and never translated at
    // all, because "pot", "tent" and "domesticated" were not known to be
    // ordinary words.
    for (const char *thing : {"Boiling Pot", "Ashlander Tent",
                              "Domesticated Guar", "Herder's Whip"}) {
        const QString t = QString::fromLatin1(thing);
        check("a thing is not a person", !term_protect::looksLikeName(t, names), t);
    }
    // ...but the proper nouns inside them still are, so the translator sees a
    // token where the creature is and cannot invent a different animal.
    check("the people are still protected",
          names.contains(QStringLiteral("Ashlander")),
          names.join(QStringLiteral(", ")));
    check("and so are the creatures",
          names.contains(QStringLiteral("Guar")),
          names.join(QStringLiteral(", ")));

    // Only what the mod actually says: a term list carrying every known name
    // would cost a pass over every row for each one it never uses.
    const QStringList unrelated =
        term_protect::findNames({QStringLiteral("Iron Key"),
                                 QStringLiteral("Ancient Chest")});
    check("a mod that names nobody carries no names",
          unrelated.isEmpty(), unrelated.join(QStringLiteral(", ")));

    // [ordinary] is documented as the escape hatch when protection is too
    // eager, and a built-in has to obey it like anything else.
    QSet<QString> ordinary;
    ordinary.insert(QStringLiteral("shinat"));
    const QStringList relaxed = term_protect::findNames(sources, {}, ordinary);
    check("[ordinary] can release a built-in name",
          !relaxed.contains(QStringLiteral("Shinat")),
          relaxed.join(QStringLiteral(", ")));
}

// Daedric Maul is one weapon and 72 game settings its author's editor re-saved
// without meaning to. It offered 27 rows and earned a rate-limit block reaching
// for them - and a translated GMST does not stay in the mod, it replaces that
// setting for the whole game.
// A request used to carry one row, so a reply could not land on the wrong one.
// Now it carries up to twenty-five, and putting the right Spanish in the wrong
// row is the one failure here a user would never catch.
// The editor already knew which rows a human had stood behind - ReviewedRole
// gates the shared translation memory - but nothing on screen said so, and the
// only way to vouch without typing was "Mark this page read", all two hundred
// of them at once.
static void testVouchingForALineWithoutEditingIt()
{
    std::cout << "\n[a line you have read, and the tick that says so]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);
    auto *t = TranslateDialogTestHook::table(d);
    const int row = rowOf(t, QStringLiteral("Forfeoranna Heim Catacombs"));
    check("a row to work with", row >= 0);

    // A machine answer: written under the guard, so nobody has read it.
    TranslateDialogTestHook::apply(d, row, 0, false,
        QStringLiteral("Nrvaa Catacombs"), QStringLiteral("Catacumbas de Nrvaa"));
    check("a machine answer starts unvouched",
          !TranslateDialogTestHook::reviewed(d, row));

    TranslateDialogTestHook::vouch(d, row);
    check("clicking the tick vouches for it",
          TranslateDialogTestHook::reviewed(d, row));

    // A mis-click on a page of two hundred must not be a one-way door.
    TranslateDialogTestHook::vouch(d, row);
    check("and clicking again takes it back",
          !TranslateDialogTestHook::reviewed(d, row));

    // "Reviewed but empty" would read as answered to the counter and the
    // filter, hiding a row nobody has touched.
    const int blank = rowOf(t, QStringLiteral("Forfeoranna Heim Depths"));
    check("a blank row to try it on", blank >= 0);
    TranslateDialogTestHook::vouch(d, blank);
    check("a blank row cannot be vouched for",
          !TranslateDialogTestHook::reviewed(d, blank));

    // Paging only hides rows, so the flag has nothing to survive - but that is
    // the property the green depends on, so it is worth saying out loud.
    TranslateDialogTestHook::vouch(d, row);
    TranslateDialogTestHook::setPage(d, 0);
    check("a vouch survives a page change",
          TranslateDialogTestHook::reviewed(d, row));
    delete d;
}

// The other half of the same idea: a line the USER typed is vouched for by the
// act of typing it. That has always been true in the data - the green is what
// finally says so - and it rests entirely on the guard, so both directions are
// worth pinning down.
// Finishing a translation and building the mod left the progress file looking
// exactly like half-done work, and the scan that runs seconds later read it
// back and painted "Translation in progress..." over a translation that was
// finished and shipped - a caption which outranks the coverage verdict, so the
// row could never say anything else.
static void testBuildingMarksTheProgressFileFinished()
{
    std::cout << "\n[a built translation is not work in progress]\n";
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("p.json"));
    translation_store::Memory mem;

    {
        auto *d = TranslateDialogTestHook::makeAt(dungeonStrings(), &mem, path);
        auto *t = TranslateDialogTestHook::table(d);
        // Every row answered by hand, so onAccept has something to build and
        // nothing unreviewed to ask about - no dialog blocks a headless run.
        for (int r = 0; r < t->rowCount(); ++r)
            t->item(r, 1)->setText(QStringLiteral("respuesta %1").arg(r));
        TranslateDialogTestHook::build(d);
        delete d;
    }

    translation_progress::Progress p;
    check("the file is written", p.load(path));
    check("and it records that it was built", p.builtAt().isValid());
    check("and says so explicitly", p.hasBuildState());
    check("the answers are still there to reopen",
          p.size() == 3, QString::number(p.size()));

    // Reopened, edited, and SAVED rather than built: that is work which is not
    // in the mod yet, and the row should say so again.
    {
        auto *d = TranslateDialogTestHook::makeAt(dungeonStrings(), &mem, path);
        auto *t = TranslateDialogTestHook::table(d);
        t->item(0, 1)->setText(QStringLiteral("una palabra distinta"));
        TranslateDialogTestHook::write(d);
        delete d;
    }
    translation_progress::Progress after;
    check("an edit saved without rebuilding is in progress again",
          after.load(path) && !after.builtAt().isValid());
    check("but the file still knows the question was asked",
          after.hasBuildState());
}

// A file written before any of this existed says nothing either way, and that
// is NOT the same as saying "not built" - the caller tells them apart to avoid
// calling somebody's finished work unfinished forever.
static void testAnOlderProgressFileHasNoOpinion()
{
    std::cout << "\n[a progress file from before this existed]\n";
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("legacy.json"));
    QFile f(path);
    check("the fixture opens",
          f.open(QIODevice::WriteOnly | QIODevice::Text), path);
    f.write("{\"version\":1,\"mod\":\"Old\",\"language\":\"spanish\","
            "\"entries\":{\"Door\":\"Puerta\"}}");
    f.close();

    translation_progress::Progress p;
    check("it loads", p.load(path));
    check("with no opinion about being built", !p.hasBuildState());
    check("and nothing to report either way", !p.builtAt().isValid());
    check("its answers survive", p.size() == 1);
    check("and no idea how big the job was", p.total() == 0,
          QString::number(p.total()));

    // Rewriting it gives it an opinion, so the fallback stops mattering.
    check("saving gives it one", p.save(path));
    translation_progress::Progress again;
    check("which is now recorded",
          again.load(path) && again.hasBuildState() && !again.builtAt().isValid());
}

// The Edit Mod button said "(35 saved)" for days while the user worked through
// Uncharted Artifacts, because "saved" meant "this row has text in it" - and a
// machine-translate run puts text in every row at once. The count was pinned at
// its ceiling before the reading even started, and reading is the whole job.
//
// The number has to be the one the editor's own counter shows, from the same
// definition: a row is done when a human has vouched for it.
static void testTheCountShownOutsideCountsWhatWasRead()
{
    std::cout << "\n[the saved-work count follows the reading]\n";
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("p.json"));
    translation_store::Memory mem;

    auto *d = TranslateDialogTestHook::makeAt(dungeonStrings(), &mem, path);
    auto *t = TranslateDialogTestHook::table(d);

    // A machine run: every row answered, nobody has read a word. Guarded, or
    // onCellChanged would vouch for them on the way in - which is exactly what
    // must NOT happen for a machine answer.
    TranslateDialogTestHook::expanding(d) = true;
    for (int r = 0; r < t->rowCount(); ++r)
        t->item(r, 1)->setText(QStringLiteral("respuesta %1").arg(r));
    TranslateDialogTestHook::expanding(d) = false;

    check("the write succeeds", TranslateDialogTestHook::write(d));
    translation_progress::Progress filled;
    check("every row has an answer", filled.load(path) && filled.size() == 3,
          QString::number(filled.size()));
    check("but nothing is done yet", filled.doneCount() == 0,
          QString::number(filled.doneCount()));
    check("and the file knows how big the job is", filled.total() == 3,
          QString::number(filled.total()));

    // Now the user reads one. This is the step the old count could not see.
    TranslateDialogTestHook::review(d, 0, true);
    check("the write succeeds again", TranslateDialogTestHook::write(d));
    translation_progress::Progress read1;
    check("the answer count has not moved", read1.load(path) && read1.size() == 3,
          QString::number(read1.size()));
    check("the done count has", read1.doneCount() == 1,
          QString::number(read1.doneCount()));

    TranslateDialogTestHook::review(d, 1, true);
    TranslateDialogTestHook::write(d);
    translation_progress::Progress read2;
    check("and keeps moving, one line at a time",
          read2.load(path) && read2.doneCount() == 2,
          QString::number(read2.doneCount()));
    check("out of a total that stays put", read2.total() == 3,
          QString::number(read2.total()));

    // Unvouching is the honest reverse, so the number is a reading of the work
    // rather than a high-water mark.
    TranslateDialogTestHook::review(d, 1, false);
    TranslateDialogTestHook::write(d);
    translation_progress::Progress back;
    check("taking one back lowers it", back.load(path) && back.doneCount() == 1,
          QString::number(back.doneCount()));
    delete d;
}

// A hand-written file lists no unreviewed rows, and that has always meant "the
// user's own work" - so all of it is done. The total is the one thing such a
// file cannot know, and guessing zero would show "(3 of 0 done)".
static void testAHandWrittenFileReadsAsDone()
{
    std::cout << "\n[a file somebody wrote themselves]\n";
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("hand.json"));
    QFile f(path);
    check("the fixture opens",
          f.open(QIODevice::WriteOnly | QIODevice::Text), path);
    f.write("{\"version\":1,\"mod\":\"Hand\",\"language\":\"spanish\","
            "\"entries\":{\"Door\":\"Puerta\",\"Key\":\"Llave\"}}");
    f.close();

    translation_progress::Progress p;
    check("it loads", p.load(path));
    check("and all of it counts as read", p.doneCount() == 2,
          QString::number(p.doneCount()));
    check("with no total to divide by", p.total() == 0);

    // The unreviewed list is what holds a machine answer back, and it still
    // does - the two rules have to coexist in one file.
    p.record(QStringLiteral("Chest"), QStringLiteral("Cofre"), false);
    check("an unvouched answer does not count", p.doneCount() == 2,
          QString::number(p.doneCount()));
    check("even though it is stored", p.size() == 3);

    p.setTotal(9);
    check("the total round-trips", p.save(path));
    translation_progress::Progress again;
    check("through save and load",
          again.load(path) && again.total() == 9, QString::number(again.total()));
    check("without disturbing the answers", again.size() == 3);
    check("or which of them were read", again.doneCount() == 2,
          QString::number(again.doneCount()));
}

// The OTHER way in. The tick column sets the dirty flag itself, but the row
// editor's OK button only calls setReviewed - so walking a mod with Next/OK
// and vouching without retyping left the dialog looking clean, and Esc closed
// it without offering to save. The flag belongs where the green bit changes,
// which covers both routes and any future third.
//
// It matters more now than it did: the count on the modlist reads this state,
// so losing a pass would make the number visibly go backwards.
static void testVouchingWithoutTypingIsUnsavedWorkToo()
{
    std::cout << "\n[a line you vouched for without typing]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    TranslateDialogTestHook::expanding(d) = true;
    t->item(0, 1)->setText(QStringLiteral("una respuesta"));
    TranslateDialogTestHook::expanding(d) = false;
    TranslateDialogTestHook::setDirty(d, false);

    TranslateDialogTestHook::review(d, 0, true);
    check("vouching marks the dialog dirty", TranslateDialogTestHook::dirty(d));

    // A no-op must stay a no-op, or every repaint would claim unsaved work.
    TranslateDialogTestHook::setDirty(d, false);
    TranslateDialogTestHook::review(d, 0, true);
    check("vouching again changes nothing", !TranslateDialogTestHook::dirty(d));

    // Refused on a blank row, as it always was - and a refusal is not work.
    TranslateDialogTestHook::review(d, 1, true);
    check("a blank row still cannot be vouched for",
          !TranslateDialogTestHook::reviewed(d, 1));
    check("and asking did not count as work", !TranslateDialogTestHook::dirty(d));
    delete d;
}

static void testTypingInARowVouchesForIt()
{
    std::cout << "\n[a line you typed is a line you have read]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);
    auto *t = TranslateDialogTestHook::table(d);
    const int row = rowOf(t, QStringLiteral("Forfeoranna Heim Catacombs"));

    // Unguarded is exactly what the inline editor does when it commits.
    t->item(row, 1)->setText(QString::fromUtf8("Catacumbas de Forfeoranna Heim"));
    check("typing an answer vouches for it",
          TranslateDialogTestHook::reviewed(d, row));
    check("and it is unsaved work", TranslateDialogTestHook::dirty(d));

    // The app's own writes go through ProgrammaticEdit and must NOT count as
    // somebody having read the row - that guard is the whole discriminator.
    const int other = rowOf(t, QStringLiteral("Forfeoranna Heim Depths"));
    {
        bool &g = TranslateDialogTestHook::expanding(d);
        const bool prev = g;
        g = true;
        t->item(other, 1)->setText(QStringLiteral("Profundidades"));
        g = prev;
    }
    check("but a write the app made does not",
          !TranslateDialogTestHook::reviewed(d, other));
    delete d;
}

// Vouching is unsaved work. Only a keystroke used to say so, so vouching and
// then closing threw the lot away in silence - reject() decides whether to
// offer a save from that one flag.
//
// markPageRead itself cannot be driven from here: it ends in a blocking
// ui::confirm, which headless would simply hang. The same fix is applied
// there, and this covers the flag it turns on.
static void testVouchingCountsAsUnsavedWork()
{
    std::cout << "\n[vouching is something to save]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);
    auto *t = TranslateDialogTestHook::table(d);
    const int row = rowOf(t, QStringLiteral("Forfeoranna Heim Catacombs"));

    TranslateDialogTestHook::apply(d, row, 0, false,
        QStringLiteral("Nrvaa Catacombs"), QStringLiteral("Catacumbas de Nrvaa"));
    check("a machine answer alone is not unsaved work of the user's",
          !TranslateDialogTestHook::reviewed(d, row));
    TranslateDialogTestHook::setDirty(d, false);

    TranslateDialogTestHook::vouch(d, row);
    check("vouching leaves work to save", TranslateDialogTestHook::dirty(d));
    check("and it really did vouch",
          TranslateDialogTestHook::reviewed(d, row));
    delete d;
}

static void testABatchLandsOnTheRightRows()
{
    std::cout << "\n[a batched reply is written row by row]\n";
    translation_store::Memory mem;
    auto *d = TranslateDialogTestHook::make(dungeonStrings(), &mem);
    auto *t = TranslateDialogTestHook::table(d);

    TranslateDialogTestHook::names(d)      = {QStringLiteral("Forfeoranna Heim")};
    TranslateDialogTestHook::renderings(d) = {QString::fromUtf8("Hogar de los precursores")};

    const int a = rowOf(t, QStringLiteral("Forfeoranna Heim Catacombs"));
    const int b = rowOf(t, QStringLiteral("Forfeoranna Heim Depths"));
    check("two distinct rows to answer", a >= 0 && b >= 0 && a != b);

    // Answered together, in the order the batch was built - which is the order
    // parseResponses returns and the order the pump indexes.
    TranslateDialogTestHook::apply(d, a, 0, false,
        QStringLiteral("Nrvaa Catacombs"), QStringLiteral("Catacumbas de Nrvaa"));
    TranslateDialogTestHook::apply(d, b, 0, false,
        QStringLiteral("Nrvaa Depths"), QStringLiteral("Profundidades de Nrvaa"));

    check("the first row got its own answer",
          t->item(a, 1)->text()
              == QString::fromUtf8("Catacumbas de Hogar de los precursores"),
          t->item(a, 1)->text());
    check("and the second got its own, not the first's",
          t->item(b, 1)->text()
              == QString::fromUtf8("Profundidades de Hogar de los precursores"),
          t->item(b, 1)->text());

    // A row nothing came back for is left blank on purpose: blank keeps the
    // original string in the plugin, and the tally has already said why. This
    // is the path a batch takes when parseResponses refuses to verify it.
    const int c = rowOf(t, QStringLiteral("Forfeoranna Heim"));
    check("a third row to leave unanswered", c >= 0 && c != a && c != b);
    TranslateDialogTestHook::apply(d, c, 0, false,
        QStringLiteral("Nrvaa"), QString());
    check("an unanswered row stays blank rather than guessing",
          t->item(c, 1)->text().isEmpty(), t->item(c, 1)->text());
    delete d;
}

static void testVanillaGameSettingsAreNotTheMods()
{
    std::cout << "\n[vanilla_gmst: what the mod actually changed]\n";

    vanilla_gmst::Table v;
    v.insert(QStringLiteral("sTeleportDisabled"),
             QStringLiteral("Teleportation magic does not work here."));
    v.insert(QStringLiteral("sEffectSummonCreature01"), QStringLiteral("Call Wolf"));
    v.insert(QStringLiteral("sEffectSummonCreature04"), QStringLiteral("Summon Winged Twilight"));
    v.insert(QStringLiteral("sTrapImpossible"),
             QStringLiteral("Trap too complex; your chance to disarm it is zero."));

    // 1. Re-saved untouched. Verbatim from Daedric_Maul.ESP.
    check("a setting re-saved unchanged is the game talking",
          vanilla_gmst::isDirty(QStringLiteral("sTeleportDisabled"),
                                QStringLiteral("Teleportation magic does not work here."), v));

    // 2. The setting's own name as its value - an editor opened without the
    // expansion that defines it. Bloodmoon says "Call Wolf".
    check("a setting named after itself is not prose",
          vanilla_gmst::isDirty(QStringLiteral("sEffectSummonCreature01"),
                                QStringLiteral("sEffectSummonCreature01"), v));

    // 3. Nothing to translate.
    check("a blank value is not translatable",
          vanilla_gmst::isDirty(QStringLiteral("sNotifyMessage60"), QString(), v));
    check("nor is one that is only spaces",
          vanilla_gmst::isDirty(QStringLiteral("sNotifyMessage61"), QStringLiteral("   "), v));

    // The other half, and the half that decides whether this is safe to ship:
    // mods that edit game settings ON PURPOSE must keep every one of them.
    check("MultiMark keeps the spell it repurposed",
          !vanilla_gmst::isDirty(QStringLiteral("sEffectSummonCreature04"),
                                 QStringLiteral("Greater Mark"), v));
    check("Patch for Purists keeps its correction",
          !vanilla_gmst::isDirty(QStringLiteral("sTrapImpossible"),
                                 QStringLiteral("Trap too complex; your chance to disarm it is nil."), v));
    check("a setting the game does not define at all is the mod's",
          !vanilla_gmst::isDirty(QStringLiteral("sSomeModAddedThis"),
                                 QStringLiteral("A brand new message"), v));

    // No game files - a Bethesda-engine profile, or openmw.cfg missing. Rules
    // 2 and 3 still answer, which is what keeps that a degraded answer rather
    // than no answer at all.
    const vanilla_gmst::Table none;
    check("without the base game, a self-named setting is still caught",
          vanilla_gmst::isDirty(QStringLiteral("sMagicCreature01ID"),
                                QStringLiteral("sMagicCreature01ID"), none));
    check("and a blank one is still caught",
          vanilla_gmst::isDirty(QStringLiteral("sAnything"), QString(), none));
    check("but real text is left alone, because nothing can vouch against it",
          !vanilla_gmst::isDirty(QStringLiteral("sTeleportDisabled"),
                                 QStringLiteral("Teleportation magic does not work here."), none));

    // The key format, kept in one place so the call site cannot spell it wrong.
    check("the setting is read out of a plugin_strings key",
          vanilla_gmst::settingOfKey(QStringLiteral("GMST:sWerewolfPopup:STRV:0"))
              == QStringLiteral("sWerewolfPopup"));
    check("and another record type is not a GMST",
          vanilla_gmst::settingOfKey(QStringLiteral("WEAP:DV_daedric_maul:FNAM:0")).isEmpty());
    check("nor is a GMST subrecord that is not its text",
          vanilla_gmst::settingOfKey(QStringLiteral("GMST:sFoo:INTV:0")).isEmpty());
}

// Some game settings do not hold text at all - their value is an object id the
// engine looks up. Translating one means it looks for a creature that is not
// there, and the spell then does nothing: no error, no crash, just silence.
static void testASettingThatHoldsAnObjectIdIsNotText()
{
    std::cout << "\n[vanilla_gmst: settings whose value is an id]\n";

    check("a summon's creature id is not text",
          vanilla_gmst::holdsObjectId(QStringLiteral("sMagicCreature01ID")));
    check("nor is a bound item's",
          vanilla_gmst::holdsObjectId(QStringLiteral("sMagicBoundBattleAxeID")));

    // The one a value-shape rule would miss: it has a space in it and is still
    // an id, which is why this is keyed on the setting name.
    check("even when the id contains a space",
          vanilla_gmst::holdsObjectId(QStringLiteral("sMagicWingedTwilightID")));

    // The adjacent setting in the very same feature IS the effect's display
    // name, and translating that one is right. One character apart, opposite
    // answers - which is the whole reason this is a rule about names.
    check("but the effect's own name is text",
          !vanilla_gmst::holdsObjectId(QStringLiteral("sEffectSummonCreature04")));
    check("and so is ordinary prose",
          !vanilla_gmst::holdsObjectId(QStringLiteral("sTeleportDisabled")));
    // Bethesda's convention is capital ID; a word merely ending in those
    // letters is not one of them.
    check("a lowercase ending is not the convention",
          !vanilla_gmst::holdsObjectId(QStringLiteral("sSomethingid")));

    // The live case this was found through. MultiMark repoints two of these at
    // its own summons, so isDirty - which asks "did the mod change it" -
    // answers KEEP, and only the id rule refuses.
    vanilla_gmst::Table v;
    v.insert(QStringLiteral("sMagicCreature04ID"),
             QStringLiteral("BM_bear_black_summon"));
    v.insert(QStringLiteral("sEffectSummonCreature04"),
             QStringLiteral("Summon Winged Twilight"));

    check("a repointed id does not read as the game talking",
          !vanilla_gmst::isDirty(QStringLiteral("sMagicCreature04ID"),
                                 QStringLiteral("Teleport_summonMark"), v));
    check("so the id rule is the only thing that catches it",
          vanilla_gmst::holdsObjectId(QStringLiteral("sMagicCreature04ID")));
    check("while the effect name beside it stays offered",
          !vanilla_gmst::isDirty(QStringLiteral("sEffectSummonCreature04"),
                                 QStringLiteral("Greater Mark"), v)
       && !vanilla_gmst::holdsObjectId(QStringLiteral("sEffectSummonCreature04")));
}

// Fifteen minutes was a guess. A block was measured still refusing the very
// first request of a fresh run more than twelve hours later, so a repeat has to
// buy a longer wait than the last one.
static void testABlockThatOutlivesTheGuess()
{
    std::cout << "\n[google_translate: the cooloff ladder]\n";
    using namespace google_translate;

    check("the first block is the old fifteen minutes",
          cooloffMinutesFor(1) == kBlockCooloffMinutes,
          QString::number(cooloffMinutesFor(1)));
    check("an unblocked user is treated as the first too",
          cooloffMinutesFor(0) == 15);
    check("the second is an hour",  cooloffMinutesFor(2) == 60);
    check("the third is six hours", cooloffMinutesFor(3) == 360);
    check("the fourth is a day",    cooloffMinutesFor(4) == 1440);
    check("and it stops there rather than growing without end",
          cooloffMinutesFor(99) == 1440);
    check("a nonsense count cannot shorten it either",
          cooloffMinutesFor(-5) == 15);

    constexpr qint64 t = 1'700'000'000;
    check("a second block waits the full hour",
          cooloffSecondsLeft(t, t, 2) == 60 * 60);
    check("and is over when the hour is up",
          cooloffSecondsLeft(t, t + 3600, 2) == 0);
    check("while the first was already over by then",
          cooloffSecondsLeft(t, t + 3600, 1) == 0);
    check("a clock that jumped forward still fails open",
          cooloffSecondsLeft(t + 500, t, 4) == 0);
}

// One row was one HTTP GET. Project Cyrodiil is 8435 rows.
static void testSeveralStringsInOneRequest()
{
    std::cout << "\n[google_translate: batching]\n";
    using namespace google_translate;

    const QStringList three = { QStringLiteral("Werewolf"),
                                QStringLiteral("Ash Slave"),
                                QStringLiteral("Bonemold Set") };
    const QString url = QString::fromUtf8(
        requestUrl(three, QStringLiteral("es")).toEncoded());
    check("every string gets its own q=", url.count(QStringLiteral("&q=")) == 3, url);
    check("in the order they were asked",
          url.indexOf(QStringLiteral("Werewolf")) < url.indexOf(QStringLiteral("Ash"))
       && url.indexOf(QStringLiteral("Ash")) < url.indexOf(QStringLiteral("Bonemold")), url);

    check("a small batch fits whole",
          fitBatch(three, 0, QStringLiteral("es")) == 3);
    check("and the tail of it does too",
          fitBatch(three, 2, QStringLiteral("es")) == 1);
    check("past the end asks for nothing",
          fitBatch(three, 3, QStringLiteral("es")) == 0);

    QStringList many;
    for (int i = 0; i < 200; ++i) many << QStringLiteral("A reasonably long line of dialogue %1").arg(i);
    const int fit = fitBatch(many, 0, QStringLiteral("es"));
    check("a long list is capped", fit > 0 && fit <= kMaxBatch, QString::number(fit));

    // A single string over the limit still goes, alone. Returning 0 would
    // leave the pump with nothing to send and a queue it can never drain.
    const QStringList huge = { QString(4000, QLatin1Char('x')) };
    check("one oversized string does not stall the run",
          fitBatch(huge, 0, QStringLiteral("es")) == 1);

    // -- and the part that makes batching safe to ship unverified ---------
    //
    // Google echoes each source back beside its translation, so the mapping is
    // CHECKED rather than trusted. Guessing here would write the wrong Spanish
    // into the wrong row, which no user would ever catch.
    const QByteArray good =
        "[[[\"Hombre lobo\",\"Werewolf\",null,null,1],"
          "[\"Esclavo de ceniza\",\"Ash Slave\",null,null,1]],null,\"en\"]";
    const QStringList two = { QStringLiteral("Werewolf"), QStringLiteral("Ash Slave") };
    const QStringList got = parseResponses(good, two);
    check("a verified batch comes back in order",
          got.size() == 2 && got[0] == QString::fromUtf8("Hombre lobo")
                          && got[1] == QString::fromUtf8("Esclavo de ceniza"),
          got.join(QStringLiteral(" | ")));

    // A long row arrives split across segments; the boundary is the echoed
    // source, not the segment count.
    const QByteArray split =
        "[[[\"Hombre \",\"Were\",null,null,1],"
          "[\"lobo\",\"wolf\",null,null,1]],null,\"en\"]";
    const QStringList one = { QStringLiteral("Werewolf") };
    check("segments are rejoined by what they answer",
          parseResponses(split, one) == QStringList{QString::fromUtf8("Hombre lobo")},
          parseResponses(split, one).join(QStringLiteral("|")));

    check("an answer to something else is refused outright",
          parseResponses(good, {QStringLiteral("Werewolf"),
                                QStringLiteral("Something we never sent")}).isEmpty());
    check("so is a short answer",
          parseResponses(good, {QStringLiteral("Werewolf"), QStringLiteral("Ash Slave"),
                                QStringLiteral("Bonemold Set")}).isEmpty());
    check("and a body of an unexpected shape",
          parseResponses(QByteArray("{\"error\":429}"), two).isEmpty());
    check("and the HTML block page",
          parseResponses(QByteArray("<html><title>Sorry...</title></html>"), two).isEmpty());
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

static void testAStableKeySurvivesARename()
{
    std::cout << "\n[renaming a mod does not lose a month of work]\n";
    const QString id   = QStringLiteral("morrowind-59192");
    const QString lang = QStringLiteral("spanish");

    // The bug this fixes: the file was named after the display name, so
    // renaming the mod orphaned it - the editor reopened blank and the row
    // stopped saying anything.
    const QString before = translation_progress::fileNameFor(
        QStringLiteral("Kawaiijiit - A Khajiit Encutification Mod"), lang, id);
    const QString after  = translation_progress::fileNameFor(
        QStringLiteral("Kawaiijiit (HD) - renamed by me"), lang, id);
    check("the same mod page gives the same file whatever it is called",
          before == after, before + " vs " + after);
    // A reinstall lands in a new timestamped folder, so a path-based key would
    // have missed this case too; the mod page is what survives both.
    check("and it carries the page id, not the name",
          before.contains(QStringLiteral("morrowind-59192")), before);

    check("a different mod page gets a different file",
          before != translation_progress::fileNameFor(
              QStringLiteral("Kawaiijiit"), lang,
              QStringLiteral("morrowind-12345")));
    check("and so does a different language",
          before != translation_progress::fileNameFor(
              QStringLiteral("Kawaiijiit"), QStringLiteral("french"), id));
    check("the name is filesystem-safe",
          !before.contains(QLatin1Char('/')) && !before.contains(QLatin1Char(':')),
          before);
}

static void testNoModPageFallsBackToTheName()
{
    std::cout << "\n[a hand-added mod has only its name]\n";
    const QString lang = QStringLiteral("spanish");
    // Unchanged from before the key moved, so a file written under the old
    // scheme is still the file this asks for.
    check("an empty id gives exactly the name-keyed shape",
          translation_progress::fileNameFor(QStringLiteral("Some Mod"), lang, {})
              == translation_progress::fileNameFor(QStringLiteral("Some Mod"), lang),
          translation_progress::fileNameFor(QStringLiteral("Some Mod"), lang, {}));
    check("and two such mods still cannot collide",
          translation_progress::fileNameFor(QStringLiteral("Mod: A"), lang, {})
              != translation_progress::fileNameFor(QStringLiteral("Mod  A"), lang, {}));
    // Without this a renamed hand-added mod would be found by neither key.
    check("renaming one DOES move its file, which is why the fallback is a fallback",
          translation_progress::fileNameFor(QStringLiteral("Old Name"), lang, {})
              != translation_progress::fileNameFor(QStringLiteral("New Name"), lang, {}));
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
    testAStableKeySurvivesARename();
    testNoModPageFallsBackToTheName();
    testAProgressFileReadsAsAMemory();
    testMissingProgressIsNotAnError();
    testEveryRowStillExistsWhenPaged();
    testAKnownNameNeedsNoRepetition();
    testBuildingMarksTheProgressFileFinished();
    testAnOlderProgressFileHasNoOpinion();
    testTheCountShownOutsideCountsWhatWasRead();
    testAHandWrittenFileReadsAsDone();
    testVouchingWithoutTypingIsUnsavedWorkToo();
    testTypingInARowVouchesForIt();
    testVouchingForALineWithoutEditingIt();
    testVouchingCountsAsUnsavedWork();
    testABatchLandsOnTheRightRows();
    testVanillaGameSettingsAreNotTheMods();
    testASettingThatHoldsAnObjectIdIsNotText();
    testABlockThatOutlivesTheGuess();
    testSeveralStringsInOneRequest();
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
