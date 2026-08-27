#ifndef TRANSLATE_DIALOG_H
#define TRANSLATE_DIALOG_H

// The editor behind "Translate mod...": every player-visible string the mod
// carries, with a field for what it should say instead.
//
// Rows are grouped by source text, not by record. A mod that names twenty
// bandits "Bandit Chief" asks once and writes twenty records, because retyping
// the same words is exactly the work this is meant to remove. The translation
// memory (translation_store) then carries that answer to every OTHER mod, so
// the second mod to mention a Bandit Chief arrives pre-filled.
//
// Nothing is written until the user accepts. On accept the caller gets
// replacements keyed per plugin, ready for translation_mod::build.

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>

#include "google_translate.h"
#include "translation_mod.h"
#include "translation_store.h"
#include "translation_rules.h"

class QLineEdit;
class QNetworkAccessManager;
class QProgressBar;
class QTimer;
class QPushButton;
class QTableWidget;

// One translatable string, as extracted from a plugin.
struct TranslatableString {
    QString pluginRel;   // path relative to the mod folder
    QString key;         // plugin_strings key, "TYPE:formid:SUB:index"
    QString source;      // the English text
    bool    secondary = false;  // came from the secondary tier (an NPC_ name)
};

class TranslateDialog : public QDialog {
    Q_OBJECT
public:
    // `language` is the lowercase token ("spanish"); `memory` is pre-filled
    // into empty rows and updated in place when the user accepts.
    TranslateDialog(const QString &modName,
                    const QList<TranslatableString> &strings,
                    const QString &language,
                    translation_store::Memory *memory,
                    const QString &rulesPath = {},
                    QWidget *parent = nullptr);

    // Valid after exec() returns Accepted. Only strings the user actually
    // filled in appear; an untouched row is left English rather than blanked.
    translation_mod::ByPlugin replacements() const { return m_result; }

    // True when the user changed at least one entry, so the caller knows
    // whether the memory is worth saving.
    bool memoryChanged() const { return m_memoryChanged; }

private:
    // Test hook: tests/test_translate_ui.cpp drives the table directly. The
    // bug it exists to catch is invisible from outside - a programmatic write
    // reads as a user edit and silently blanks the row.
    friend struct TranslateDialogTestHook;

private slots:
    void onAccept();
    void onMachineTranslate();
    void onImportDatabase();
    void onEditRules();
    // A linked term was edited: re-expand every row that uses it.
    void onCellChanged(int row, int column);
    // A row's own window. These strings are whole paragraphs wrapped in
    // markup, and a one-line table cell is no place to read one, let alone
    // check a machine translation against its original.
    void onRowDoubleClicked(int row, int column);

private:
    // Drains m_mtQueue while fewer than kMaxInFlight requests are outstanding;
    // each reply calls back in to keep the queue moving.
    void pumpMachineTranslate();

private:
    void buildUi(const QString &modName);
    // Which name (index into m_mtNames) a row's SOURCE is entirely, or -1.
    int  nameRowIndex(int row) const;
    // Re-render one row from its stored masked template.
    void expandRow(int row);
    // Tint the rows that carry a linked name, and drop the tint from rows the
    // user has hand-edited away from their template.
    void restyleLinkedRows();
    void fillFromMemory();
    // Open the row editor on `row` and stay open while the user walks the
    // list with Previous/Next. Writes through the table, so an edit made here
    // takes the same path as one typed into the cell.
    void openRowEditor(int row);
    // Mark a row queued (1), in flight (2) or done (0) for the spinner.
    // Callers must hold a ProgrammaticEdit guard.
    void setPending(int row, int state);

    QList<TranslatableString> m_strings;
    // Row -> the source text it edits. Rows are unique source strings.
    QStringList               m_rowSource;
    QString                   m_language;
    translation_store::Memory *m_memory = nullptr;
    translation_mod::ByPlugin m_result;
    bool                      m_memoryChanged = false;

    QTableWidget *m_table   = nullptr;
    QPushButton  *m_mtBtn   = nullptr;
    QProgressBar *m_mtBar   = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    QList<int>    m_mtQueue;        // rows still waiting to be sent
    // The mod's proper nouns, masked out of every request so they come back
    // identical in every row (term_protect.h).
    QStringList   m_mtNames;
    // Rendering chosen for each name in m_mtNames - the translation carried
    // into every row that mentions it. Index-parallel with m_mtNames; an
    // empty entry means "not decided yet".
    QStringList   m_nameRendering;
    // Rows still queued for the SECOND pass, which cannot start until every
    // name has a rendering to substitute.
    QList<int>    m_mtPending;
    bool          m_mtNamePhase = false;
    // Guards the programmatic writes that re-expand linked rows, so they do
    // not read as user edits and detach the row they just updated.
    bool          m_expanding = false;
    // The user's own English->target rules, reloaded every time the editor
    // opens so tuning the file is a save-and-reopen away.
    translation_rules::Rules m_rules;
    QString                  m_rulesPath;
    int           m_mtInFlight = 0;
    int           m_mtDone     = 0;
    int           m_mtTotal    = 0;
    // What went wrong, per outcome, rather than one "it was empty" counter.
    // Every failure used to look identical, so the summary had to guess - and
    // it guessed rate-limiting, which is wrong for a machine with no network.
    google_translate::FailureTally m_mtTally;
    // The first failure's own words. errorString() is the only thing that
    // separates DNS from TLS from no-route.
    QString       m_mtFirstError;
    int           m_mtFirstHttpStatus = 0;
    // Drives the per-row spinner while a run is on. The bar at the bottom
    // says how far along the whole run is; this says which rows are actually
    // being fetched, which is the question somebody watching a 480-row list
    // is really asking.
    QTimer       *m_mtAnim  = nullptr;
    int           m_mtFrame = 0;
};

#endif // TRANSLATE_DIALOG_H
