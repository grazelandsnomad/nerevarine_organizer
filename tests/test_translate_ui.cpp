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

#include "translate_dialog.h"
#include "translation_store.h"

#include <QApplication>
#include <QTableWidget>

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

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);

    std::cout << "=== translate dialog UI ===\n";
    testDeliveredRowIsNotBlank();
    testUnguardedWriteIsWhatBlankedIt();
    testEditingTheNameRerendersEveryRow();
    testHandEditingARowBreaksItsLink();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
