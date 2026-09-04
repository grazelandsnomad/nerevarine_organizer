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
#include <QSet>
#include <QString>

#include "google_translate.h"
#include "translation_mod.h"
#include "translation_store.h"
#include "translation_progress.h"
#include "translation_rules.h"

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QProgressBar;
class QTimer;
class QPushButton;
class QSpinBox;
class QCheckBox;
class QTableWidget;
class QStyledItemDelegate;

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
                    // Where this mod's half-finished work lives. Empty
                    // disables resuming, which is what a caller with nowhere
                    // to write gets.
                    const QString &progressPath = {},
                    // Source texts the base game already says for the very
                    // records that carry them - a mod that re-saved a name
                    // without changing it. Pre-answered with themselves rather
                    // than hidden, so nothing disappears and nothing is sent.
                    const QSet<QString> &vanillaSaysIt = {},
                    QWidget *parent = nullptr);

    // How the dialog was closed. Saved and Build both accept(), so the caller
    // cannot tell them apart from exec()'s return alone - and it must, since
    // only Build may go on to build the mod and touch the modlist.
    enum class Outcome { Cancelled, Saved, Build };
    Outcome outcome() const { return m_outcome; }

    // Valid after exec() returns Accepted. Only strings the user actually
    // filled in appear; an untouched row is left English rather than blanked.
    translation_mod::ByPlugin replacements() const { return m_result; }

    // True when the user changed at least one entry, so the caller knows
    // whether the memory is worth saving.
    bool memoryChanged() const { return m_memoryChanged; }

protected:
    // Covers Esc, the window-manager close and the Cancel button - Qt6's
    // QDialog::closeEvent calls reject(), so this one override catches all
    // three.
    void reject() override;

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
    // -- Paging -------------------------------------------------------
    //
    // The table always holds every row; these only decide which are on offer
    // and which of those are on screen. Row index stays the identity of a
    // source string everywhere else in this class.
    void rebuildVisible();          // recompute m_visible from the filter
    void showPage(int page);        // hide everything not on `page`
    int  pageCount() const;
    int  pageOfRow(int row) const;  // -1 when the row is filtered out
    bool rowAnswered(int row) const;// answered AND read
    void jumpToFirstTodo();
    void markPageRead();
    void setReviewed(int row, bool reviewed);
    void recountProgress();
    void scheduleRecount();
    // What the row editor's Previous/Next walk: the rows on offer, not the
    // raw table. Side-effect free so they can be tested - openRowEditor ends
    // in exec() and cannot be.
    int  nextVisible(int row) const;
    int  prevVisible(int row) const;
    QPair<int, int> visiblePosition(int row) const;

    // Half-finished work. writeProgress reports failure rather than swallowing
    // it: this is the call standing between the user and a month of retyping.
    void fillFromProgress();
    // Answers the rows the base game already answered. Runs last of the three
    // fills, so a saved answer or a memory hit always outranks it.
    void fillFromVanilla();
    // Writes the answers to the progress file. `built` records whether they
    // have just been turned into a translation mod: a build is a save point
    // too, but finished work and half-done work must not look the same on
    // disk, or the row goes on calling a shipped translation unfinished.
    //
    // An argument rather than a separate setter, because EVERY write has to
    // answer the question - "saved, not shipped" is exactly as much a fact as
    // a build is, and a caller that forgot would leave a stale flag behind.
    bool writeProgress(bool built = false);
    void onSaveProgress();
    // What accepting would do, minus every message box - so it can be tested.
    // onAccept itself ends in a QMessageBox and never can be.
    struct AcceptPlan {
        QHash<QString, QString> byText;   // every non-blank row, read or not
        int remembered = 0;               // read rows, bound for the memory
        int unreviewed = 0;               // shipped, but held back from it
    };
    AcceptPlan planAccept();

    void pumpMachineTranslate();
    // Advance to pass two, or end the run.
    void advanceMachineTranslate();
    // The one teardown; see the comment on the definition.
    void finishMachineTranslate();
    // Seconds left of a block, 0 when there is none. Side-effect free
    // so it can be tested; onMachineTranslate cannot be.
    int  cooloffLeftSeconds() const;
    // Paints the countdown onto the progress bar and the tooltip.
    void updateCooloffDisplay();

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
    // Paging widgets and state. m_visible is the source indices currently on
    // offer, in table order - equal to [0, rowCount) when the filter is off.
    // It is a SNAPSHOT: rebuilt on an explicit event, never on a keystroke, or
    // the row just answered would vanish from under the cursor mid-edit.
    QLabel       *m_countLabel = nullptr;
    QPushButton  *m_prevPage   = nullptr;
    QPushButton  *m_nextPage   = nullptr;
    QSpinBox     *m_pageSpin   = nullptr;
    QLabel       *m_pageOf     = nullptr;
    QCheckBox    *m_todoOnly   = nullptr;
    QTimer       *m_recountTimer = nullptr;
    QList<int>    m_visible;
    int           m_page = 0;
    static constexpr int kPageSize = 200;
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

    // The table's own delegate: paints the green wash for a row somebody has
    // vouched for, the vouch glyph, and the machine-translate spinner. Held
    // rather than fetched back off the view every animation frame.
    //
    // Typed as the base class on purpose: the real one is file-local to
    // translate_dialog.cpp, which is where it belongs, and naming it here
    // would mean either moving it out or forward-declaring a type that does
    // not exist at this scope.
    QStyledItemDelegate *m_rowDelegate = nullptr;
    // The user's own English->target rules, reloaded every time the editor
    // opens so tuning the file is a save-and-reopen away.
    translation_rules::Rules m_rules;
    QString                  m_rulesPath;
    QString                  m_progressPath;
    QSet<QString>            m_vanillaSaysIt;
    QLabel                  *m_vanillaNote = nullptr;
    int                      m_vanillaFilled = 0;
    translation_progress::Progress m_progress;
    Outcome                  m_outcome = Outcome::Cancelled;
    // Answers typed since the last write, so Cancel can ask rather than
    // discarding a month of work without a word.
    bool                     m_progressDirty = false;
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
    // One restartable timer paces dispatch, the way BulkInstallQueue drip-feeds
    // Nexus installs. Never singleShot-per-request: see notify_banner.h for why
    // one timer beats a fleet of them.
    QTimer       *m_mtPace  = nullptr;
    QTimer       *m_mtCooloffTick = nullptr;   // 1 Hz, drives the countdown
    // A block cleared the queues. Nothing restarts them until the run is torn
    // down, and finishMachineTranslate is what puts this back to false.
    bool          m_mtStopped = false;
    // This run follows a block, so it opens with ONE request and waits to see
    // whether Google has let go before committing the rest of the queue. A
    // wait that expires is only a guess that the block lapsed - the block is
    // Google's and it does not tell us - so the cheapest way to ask is to ask
    // once. Cleared by the first answer that comes back.
    bool          m_mtProbe = false;
    // A batch came back in a shape parseResponses could not verify, so the
    // rest of this run asks one string at a time. Sticky for the run rather
    // than retried: if the endpoint does not answer batches the way this reads
    // them, it will not start doing so halfway through.
    bool          m_mtNoBatch = false;

    // Write one machine answer into the table. Split out of the reply handler
    // because a reply now carries a whole batch, and every row in it needs the
    // same markup restore, after-rules and name bookkeeping.
    void applyMachineAnswer(int item, int nameIdx, bool isName,
                            const QString &named, const QStringList &spans,
                            const QString &raw);
    int           m_mtFrame = 0;
};

#endif // TRANSLATE_DIALOG_H
