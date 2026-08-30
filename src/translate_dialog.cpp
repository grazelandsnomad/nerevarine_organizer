#include "translate_dialog.h"
#include <QTime>
#include <QDateTime>
#include "settings.h"

#include "prompts.h"
#include "translator.h"
#include "target_language.h"
#include "google_translate.h"
#include "lore_overrides.h"
#include "markup_protect.h"
#include "term_protect.h"
#include "subprocess.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QDir>
#include <QFileDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMessageBox>
#include <QProgressBar>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QPainter>
#include <QPlainTextEdit>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {

// The ISO code the endpoint speaks, from the one language table. Was a private
// copy here, built from the app's UI-translation set - which listed languages
// Bethesda never ships and spelled Chinese as the app's own filename. See
// target_language.h.
QString isoFor(const QString &language)
{
    return target_language::isoCode(language);
}

enum Col { ColSource = 0, ColTranslation = 1, ColCount = 2 };

// The masked translation a row came back with ("Catacumbas de Nrvaa"), kept so
// the row can be re-rendered when the name's translation changes. Cleared when
// the user edits the row by hand, which detaches it.
constexpr int TemplateRole = Qt::UserRole + 1;
// Set when the translator did not give the markup back and the row had to be
// rebuilt from the source's own tags. The text is usable and it is a guess -
// which is worth a mark, because a rebuilt row is the one to read first.
constexpr int RepairedRole = Qt::UserRole + 2;
// 0 not in this run, 1 queued behind other rows, 2 being fetched right now.
// Only ever written under a ProgrammaticEdit guard: a write to this column
// reads as a hand edit and would detach the row from its template.
constexpr int PendingRole  = Qt::UserRole + 3;
// A human has read this answer and vouched for it.
//
// The gate on the shared per-language memory. An answer that came back from
// the machine translator and has not been looked at belongs to this mod's own
// progress file: put it in the memory and one bad guess about "Bandit Chief"
// pre-fills every other mod the user ever opens.
//
// The item is the single source of truth. It survives paging for free -
// nothing ever destroys a row - and there is no parallel bitset to drift.
constexpr int ReviewedRole = Qt::UserRole + 4;

// The per-row "still waiting" marker.
//
// The bar at the bottom counts the whole run, which on a 480-row mod tells
// somebody watching almost nothing: rows come back out of order, and a row
// that is simply empty looks the same whether it is queued, in flight, or
// finished blank. Five requests are outstanding at a time, so five rows spin
// and the rest say they are waiting - and that is the truth of it rather than
// 480 spinners implying 480 requests.
class PendingDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void setFrame(int f) { m_frame = f; }

    void paint(QPainter *painter, const QStyleOptionViewItem &opt,
               const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, opt, index);

        const int state = index.data(PendingRole).toInt();
        if (state <= 0) return;
        // A row that has already come back is not waiting for anything,
        // whatever the flag still says.
        if (!index.data(Qt::DisplayRole).toString().trimmed().isEmpty()) return;

        // The same braille spinner the mod list uses while a mod installs, so
        // "this is working" looks the same everywhere in the app.
        static const char *kSpinner[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
        const QString text = state >= 2
            ? QStringLiteral("%1 %2").arg(QString::fromUtf8(kSpinner[m_frame % 10]),
                                          T("translate_row_working"))
            : T("translate_row_waiting");

        painter->save();
        QColor fg = opt.palette.color(QPalette::Disabled, QPalette::Text);
        painter->setPen(fg);
        QFont f = opt.font;
        f.setItalic(true);
        painter->setFont(f);
        painter->drawText(opt.rect.adjusted(6, 0, -4, 0),
                          Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, text);
        painter->restore();
    }

private:
    int m_frame = 0;
};

// Marks a write to the table as OURS rather than the user's.
//
// QTableWidget emits cellChanged for a custom role just as it does for the
// visible text, so storing the hidden template looked exactly like the user
// typing: the row detached itself, expandRow then found no template, and every
// row but the name came back blank. RAII so a new write site cannot forget it.
class ProgrammaticEdit {
public:
    explicit ProgrammaticEdit(bool &flag) : m_flag(flag), m_prev(flag)
    { m_flag = true; }
    ~ProgrammaticEdit() { m_flag = m_prev; }
    ProgrammaticEdit(const ProgrammaticEdit &) = delete;
    ProgrammaticEdit &operator=(const ProgrammaticEdit &) = delete;
private:
    bool &m_flag;
    bool  m_prev;
};

} // namespace

TranslateDialog::TranslateDialog(const QString &modName,
                                 const QList<TranslatableString> &strings,
                                 const QString &language,
                                 translation_store::Memory *memory,
                                 const QString &rulesPath,
                                 const QString &progressPath,
                                 QWidget *parent)
    : QDialog(parent)
    , m_strings(strings)
    , m_language(language)
    , m_memory(memory)
    , m_rulesPath(rulesPath)
    , m_progressPath(progressPath)
{
    if (!m_rulesPath.isEmpty()) m_rules = translation_rules::load(m_rulesPath);

    // Group by source text: one row per distinct string, however many records
    // carry it. This is the whole point - twenty "Bandit Chief" records are one
    // question, not twenty.
    QSet<QString> seen;
    for (const TranslatableString &s : m_strings) {
        const QString norm = translation_store::normalize(s.source);
        if (seen.contains(norm)) continue;
        seen.insert(norm);
        m_rowSource << s.source;
    }
    m_rowSource.sort(Qt::CaseInsensitive);

    buildUi(modName);
    // Order matters. This mod's own half-finished work goes in first, and the
    // shared memory only fills what is still blank - see the guard in
    // fillFromMemory. The other way round, a hit from some other mod would
    // overwrite an answer the user typed here last week.
    if (!m_progressPath.isEmpty()) {
        m_progress.load(m_progressPath);
        m_progress.setMod(modName, m_language);
        fillFromProgress();
    }
    fillFromMemory();

    // First page, and land on the first thing still wanting an answer rather
    // than on row one - on a mod this size, row one is where you were a month
    // ago.
    rebuildVisible();
    showPage(0);
}

// -- Paging ------------------------------------------------------------
//
// The table always holds every row; paging only hides the ones that are not
// on offer. See the note in buildUi for why the alternative - a table that
// holds one page - is not on the table.

// Has this row been answered, and has somebody read the answer?
//
// "Answered" is not "has text": after a machine-translate run every row has
// text, and a filter that only asked about text would hide the whole mod at
// exactly the moment it is most needed. A row still needs the user until a
// human has vouched for it.
bool TranslateDialog::rowAnswered(int row) const
{
    auto *cell = m_table->item(row, ColTranslation);
    if (!cell) return false;
    if (cell->text().trimmed().isEmpty()) return false;
    return cell->data(ReviewedRole).toBool();
}

void TranslateDialog::rebuildVisible()
{
    const bool todoOnly = m_todoOnly && m_todoOnly->isChecked();
    m_visible.clear();
    m_visible.reserve(m_rowSource.size());
    for (int i = 0; i < m_rowSource.size(); ++i)
        if (!todoOnly || !rowAnswered(i)) m_visible.append(i);
}

int TranslateDialog::pageCount() const
{
    if (m_visible.isEmpty()) return 1;
    return (int(m_visible.size()) + kPageSize - 1) / kPageSize;
}

void TranslateDialog::showPage(int page)
{
    if (!m_table) return;
    m_page = qBound(0, page, pageCount() - 1);

    const int from = m_page * kPageSize;
    const int to   = qMin(from + kPageSize, int(m_visible.size()));

    QSet<int> onPage;
    onPage.reserve(to - from);
    for (int i = from; i < to; ++i) onPage.insert(m_visible[i]);

    // Updates off for the sweep: setRowHidden is cheap per call, but the
    // repaint region maths behind it is not, and this touches every row.
    m_table->setUpdatesEnabled(false);
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->setRowHidden(r, !onPage.contains(r));
    m_table->setUpdatesEnabled(true);

    // Signals blocked: setValue would call back into showPage.
    if (m_pageSpin) {
        const QSignalBlocker block(m_pageSpin);
        m_pageSpin->setMaximum(pageCount());
        m_pageSpin->setValue(m_page + 1);
    }
    if (m_pageOf)   m_pageOf->setText(T("translate_page_of").arg(pageCount()));
    if (m_prevPage) m_prevPage->setEnabled(m_page > 0);
    if (m_nextPage) m_nextPage->setEnabled(m_page < pageCount() - 1);

    if (from < to) m_table->scrollToItem(m_table->item(m_visible[from], ColSource),
                                         QAbstractItemView::PositionAtTop);
    recountProgress();
}

int TranslateDialog::pageOfRow(int row) const
{
    const int at = int(m_visible.indexOf(row));
    return at < 0 ? -1 : at / kPageSize;
}

void TranslateDialog::jumpToFirstTodo()
{
    // Derived, not remembered: storing "the row I was on" would be wrong the
    // moment the mod gained a string and everything after it renumbered.
    int target = -1;
    for (int i = 0; i < m_rowSource.size() && target < 0; ++i)
        if (!rowAnswered(i)) target = i;
    if (target < 0) {
        ui::info(this, T("translate_machine"), T("translate_all_answered"));
        return;
    }
    // The row may be filtered out of the current view; the jump should still
    // land on it, so widen first if it is not on offer.
    if (!m_visible.contains(target)) {
        if (m_todoOnly) { const QSignalBlocker b(m_todoOnly); m_todoOnly->setChecked(false); }
        rebuildVisible();
    }
    const int page = pageOfRow(target);
    showPage(page < 0 ? 0 : page);
    m_table->setCurrentCell(target, ColTranslation);
}

void TranslateDialog::markPageRead()
{
    const int from = m_page * kPageSize;
    const int to   = qMin(from + kPageSize, int(m_visible.size()));
    QList<int> rows;
    for (int i = from; i < to; ++i) {
        const int r = m_visible[i];
        auto *cell = m_table->item(r, ColTranslation);
        if (cell && !cell->text().trimmed().isEmpty()
            && !cell->data(ReviewedRole).toBool())
            rows.append(r);
    }
    if (rows.isEmpty()) {
        ui::info(this, T("translate_machine"), T("translate_mark_page_none"));
        return;
    }
    if (!ui::confirm(this, T("translate_mark_page_read"),
                     T("translate_mark_page_confirm").arg(rows.size())))
        return;
    for (int r : rows) setReviewed(r, true);
    recountProgress();
}

// Marks a row as vouched for. Refuses on a blank row: "reviewed but empty"
// would read as answered to the counter and the filter, and hide a row that
// nobody has done anything to.
void TranslateDialog::setReviewed(int row, bool reviewed)
{
    auto *cell = m_table->item(row, ColTranslation);
    if (!cell) return;
    if (reviewed && cell->text().trimmed().isEmpty()) return;
    ProgrammaticEdit guard(m_expanding);
    cell->setData(ReviewedRole, reviewed);
}

// One pass over every row. Deliberately not a pair of counters kept up to
// date as things change: two sources of truth for "is this row done" is how a
// counter starts lying, and twenty thousand item lookups is about a
// millisecond.
void TranslateDialog::recountProgress()
{
    if (!m_countLabel) return;
    int done = 0, unread = 0;
    for (int i = 0; i < m_rowSource.size(); ++i) {
        auto *cell = m_table->item(i, ColTranslation);
        if (!cell || cell->text().trimmed().isEmpty()) continue;
        if (cell->data(ReviewedRole).toBool()) ++done;
        else                                   ++unread;
    }
    const int total = int(m_rowSource.size());
    m_countLabel->setText(T("translate_progress_counter")
                              .arg(total).arg(done).arg(unread)
                              .arg(total - done - unread));
}

void TranslateDialog::scheduleRecount()
{
    if (!m_recountTimer) {
        m_recountTimer = new QTimer(this);
        m_recountTimer->setSingleShot(true);
        m_recountTimer->setInterval(0);
        connect(m_recountTimer, &QTimer::timeout,
                this, &TranslateDialog::recountProgress);
    }
    m_recountTimer->start();
}

// -- What the row editor walks -----------------------------------------
//
// Prev/Next follow the rows on OFFER, not the raw table, or Next would step
// into a row the filter is hiding. Side-effect free so they can be tested;
// openRowEditor itself ends in exec() and cannot be.

int TranslateDialog::nextVisible(int row) const
{
    const int at = int(m_visible.indexOf(row));
    if (at < 0 || at + 1 >= m_visible.size()) return -1;
    return m_visible[at + 1];
}

int TranslateDialog::prevVisible(int row) const
{
    const int at = int(m_visible.indexOf(row));
    if (at <= 0) return -1;
    return m_visible[at - 1];
}

QPair<int, int> TranslateDialog::visiblePosition(int row) const
{
    const int at = int(m_visible.indexOf(row));
    return {at < 0 ? 0 : at + 1, int(m_visible.size())};
}

void TranslateDialog::buildUi(const QString &modName)
{
    setWindowTitle(T("translate_title").arg(modName));
    resize(760, 520);

    auto *lay = new QVBoxLayout(this);

    auto *intro = new QLabel(
        T("translate_intro")
            .arg(QString::number(m_rowSource.size()),
                 QString::number(m_strings.size()),
                 target_language::displayName(m_language)), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    // Its own label: translate_intro is a four-line paragraph and reflowing it
    // on every keystroke would make the whole dialog jump.
    m_countLabel = new QLabel(this);
    m_countLabel->setWordWrap(true);
    lay->addWidget(m_countLabel);

    m_table = new QTableWidget(m_rowSource.size(), ColCount, this);
    m_table->setHorizontalHeaderLabels({T("translate_col_source"),
                                        T("translate_col_translation")});
    m_table->horizontalHeader()->setSectionResizeMode(ColSource, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColTranslation, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);

    // Every row is built, and every row STAYS built - paging hides rows rather
    // than rebuilding the table. Row index is the identity of a source string
    // in a dozen places (the machine-translate queues, expandRow, setPending,
    // onAccept's unguarded item() deref), so a table that only holds the
    // current page would have to rewrite all of them and would hand onAccept a
    // null pointer the first time somebody saved from page two.
    //
    // Updates off for the bulk build: with them on, the repaint arithmetic
    // runs once per row, which is the difference between a blink and several
    // seconds on a mod with twenty thousand strings.
    m_table->setUpdatesEnabled(false);
    for (int i = 0; i < m_rowSource.size(); ++i) {
        auto *src = new QTableWidgetItem(m_rowSource[i]);
        // The source is what the plugin says; editing it here would be a lie.
        src->setFlags(src->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, ColSource, src);
        m_table->setItem(i, ColTranslation, new QTableWidgetItem(QString()));
    }
    m_table->setUpdatesEnabled(true);
    // Double-click is taken for the row editor, so it is off the list of
    // things that start the one-line inline editor. Typing and F2 still do,
    // which is what a short string wants.
    m_table->setEditTriggers(QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    connect(m_table, &QTableWidget::cellChanged,
            this, &TranslateDialog::onCellChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, &TranslateDialog::onRowDoubleClicked);
    m_table->setItemDelegateForColumn(ColTranslation, new PendingDelegate(m_table));
    lay->addWidget(m_table, 1);

    // Pager. A spin box as well as Prev/Next because at forty pages stepping
    // is not navigation, and the jump button because "where was I" is the
    // question somebody coming back after a day actually has.
    auto *pager = new QHBoxLayout;
    m_prevPage = new QPushButton(T("translate_page_prev"), this);
    m_nextPage = new QPushButton(T("translate_page_next"), this);
    m_pageSpin = new QSpinBox(this);
    m_pageSpin->setMinimum(1);
    m_pageOf   = new QLabel(this);
    m_todoOnly = new QCheckBox(T("translate_filter_todo"), this);
    m_todoOnly->setToolTip(T("translate_filter_todo_tip"));
    auto *jumpBtn     = new QPushButton(T("translate_jump_todo"), this);
    auto *markPageBtn = new QPushButton(T("translate_mark_page_read"), this);
    markPageBtn->setToolTip(T("translate_mark_page_read_tip"));

    pager->addWidget(m_prevPage);
    pager->addWidget(m_pageSpin);
    pager->addWidget(m_pageOf);
    pager->addWidget(m_nextPage);
    pager->addStretch(1);
    pager->addWidget(m_todoOnly);
    pager->addWidget(jumpBtn);
    pager->addWidget(markPageBtn);
    lay->addLayout(pager);

    connect(m_prevPage, &QPushButton::clicked, this, [this]{ showPage(m_page - 1); });
    connect(m_nextPage, &QPushButton::clicked, this, [this]{ showPage(m_page + 1); });
    connect(m_pageSpin, &QSpinBox::valueChanged,
            this, [this](int v) { if (v - 1 != m_page) showPage(v - 1); });
    connect(m_todoOnly, &QCheckBox::toggled, this, [this]{
        rebuildVisible();
        showPage(0);
    });
    connect(jumpBtn,     &QPushButton::clicked, this, &TranslateDialog::jumpToFirstTodo);
    connect(markPageBtn, &QPushButton::clicked, this, &TranslateDialog::markPageRead);

    auto *row = new QHBoxLayout;
    m_mtBtn = new QPushButton(T("translate_machine"), this);
    m_mtBtn->setToolTip(T("translate_machine_tip"));
    m_mtBtn->setEnabled(!isoFor(m_language).isEmpty());
    connect(m_mtBtn, &QPushButton::clicked, this, &TranslateDialog::onMachineTranslate);
    row->addWidget(m_mtBtn);

    auto *rulesBtn = new QPushButton(T("translate_edit_rules"), this);
    rulesBtn->setToolTip(T("translate_edit_rules_tip"));
    rulesBtn->setEnabled(!m_rulesPath.isEmpty());
    connect(rulesBtn, &QPushButton::clicked, this, &TranslateDialog::onEditRules);
    row->addWidget(rulesBtn);

    auto *importBtn = new QPushButton(T("translate_import_db"), this);
    importBtn->setToolTip(T("translate_import_db_tip"));
    connect(importBtn, &QPushButton::clicked, this, &TranslateDialog::onImportDatabase);
    row->addWidget(importBtn);

    m_mtBar = new QProgressBar(this);
    m_mtBar->setVisible(false);
    row->addWidget(m_mtBar, 1);
    lay->addLayout(row);

    // Reopening the dialog inside a cooloff should say so without a click.
    // After the bar exists, not beside the button: it paints onto both.
    updateCooloffDisplay();

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Ok)->setText(T("translate_apply"));
    // ActionRole, not AcceptRole: an accept role would make the button box
    // emit accepted() and run onAccept, which is the one thing saving must
    // not do.
    auto *saveBtn = box->addButton(T("translate_save_progress"),
                                   QDialogButtonBox::ActionRole);
    saveBtn->setToolTip(T("translate_save_progress_tip"));
    connect(saveBtn, &QPushButton::clicked, this, &TranslateDialog::onSaveProgress);
    connect(box, &QDialogButtonBox::accepted, this, &TranslateDialog::onAccept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(box);
}

void TranslateDialog::fillFromMemory()
{
    if (!m_memory) return;
    int hits = 0;
    for (int i = 0; i < m_rowSource.size(); ++i) {
        // Never over the top of an answer already in the row. This was safe
        // while the table was always blank here; it is not now that this
        // mod's own saved work is loaded first, and a memory hit from some
        // other mod would quietly replace it.
        if (!m_table->item(i, ColTranslation)->text().trimmed().isEmpty())
            continue;
        const QString known = m_memory->lookup(m_rowSource[i]);
        if (known.isEmpty()) continue;
        m_table->item(i, ColTranslation)->setText(known);
        // Somebody typed this once already, in some other mod. It is confirmed
        // work, not a guess, so it does not need reading again.
        setReviewed(i, true);
        ++hits;
    }
    if (hits > 0)
        setWindowTitle(windowTitle() + T("translate_memory_suffix").arg(hits));
}

namespace {

// "14:32" - a countdown, not a clock time. The cooloff is fifteen minutes, so
// hours never appear.
QString mmss(int seconds)
{
    return QTime(0, 0).addSecs(seconds).toString(QStringLiteral("m:ss"));
}

} // namespace

int TranslateDialog::cooloffLeftSeconds() const
{
    const QDateTime blocked = Settings::translateBlockedAt();
    if (!blocked.isValid()) return 0;
    return google_translate::cooloffSecondsLeft(
        blocked.toSecsSinceEpoch(), QDateTime::currentSecsSinceEpoch(),
        Settings::translateBlockStrikes());
}

// The bar is either a run's progress or a cooloff countdown, never both - a run
// cannot start during a cooloff, and the button being disabled is exactly the
// window in which the bar belongs to a run.
void TranslateDialog::updateCooloffDisplay()
{
    // buildUi calls this while it is still assembling the row, and the 1 Hz
    // tick can outlive a teardown; neither is worth a crash.
    if (!m_mtBtn || !m_mtBar) return;
    if (!m_mtBtn->isEnabled()) return;

    const int left = cooloffLeftSeconds();
    if (left <= 0) {
        if (m_mtCooloffTick) m_mtCooloffTick->stop();
        m_mtBar->setVisible(false);
        m_mtBar->resetFormat();
        m_mtBtn->setToolTip(T("translate_machine_tip"));
        return;
    }

    if (!m_mtCooloffTick) {
        m_mtCooloffTick = new QTimer(this);
        m_mtCooloffTick->setInterval(1000);
        connect(m_mtCooloffTick, &QTimer::timeout,
                this, &TranslateDialog::updateCooloffDisplay);
    }
    const int total = google_translate::cooloffMinutesFor(
                          Settings::translateBlockStrikes()) * 60;
    m_mtBar->setRange(0, total);
    m_mtBar->setValue(total - left);     // fills as the wait drains
    // No literal '%' in the format: QProgressBar reads %p, %v and %m out of it,
    // and m:ss has none.
    m_mtBar->setFormat(T("translate_mt_cooloff_bar").arg(mmss(left)));
    m_mtBar->setVisible(true);
    m_mtBtn->setToolTip(T("translate_mt_cooloff_tip").arg(mmss(left)));
    if (!m_mtCooloffTick->isActive()) m_mtCooloffTick->start();
}

void TranslateDialog::onMachineTranslate()
{
    const QString iso = isoFor(m_language);
    if (iso.isEmpty()) return;

    // Before anything is written. The lore/rule pass below fills answers into
    // the table as it goes, so a gate placed after it would leave half a run's
    // results on screen for a run that never happened.
    //
    // The arithmetic and the display live in helpers on purpose: ui::warn is a
    // blocking QMessageBox, so a test can never call this function, and those
    // two carry the half worth testing.
    if (const int wait = cooloffLeftSeconds(); wait > 0) {
        updateCooloffDisplay();
        ui::warn(this, T("translate_machine"),
                 T("translate_mt_cooloff").arg(mmss(wait)));
        return;
    }

    // The mod's recurring proper nouns, and what each one should say.
    //
    // Asked in order and answered by the first that has an opinion: what the
    // user typed, what their rules file says, what the lore table knows -
    // and failing all three, the name itself.
    //
    // That last one used to be "ask the translator". It is why nine rows of
    // Sixth House Obsidian Weapon came back saying "dagoth Balen": "Dagoth"
    // repeats across the mod, so it was found, nothing knew it, it was sent
    // on its own, and Google lowercased it into a common noun - which was
    // then carried into every row that mentions it. A proper noun nobody has
    // an opinion about is worth exactly itself, which is also what
    // term_protect.h says protection means. Consistency, which is what the
    // one-answer-everywhere rule was ever after, holds either way.
    m_mtNames = term_protect::findNames(m_rowSource, m_rules.protect,
                                        m_rules.ordinary);
    m_nameRendering = QStringList();
    for (int i = 0; i < m_mtNames.size(); ++i) {
        // A name the user has already decided about keeps that decision.
        QString known = m_memory ? m_memory->lookup(m_mtNames[i]) : QString();
        if (known.isEmpty())
            known = m_rules.terms.value(m_mtNames[i].trimmed().toLower());
        if (known.isEmpty())
            known = lore_overrides::lookup(m_mtNames[i], m_language);
        if (known.isEmpty())
            known = m_mtNames[i];      // nobody knows better: it is a name
        m_nameRendering << known;
    }

    // Rows already answered are left alone; a lore term or a user rule is a
    // decision and never goes to a machine translator.
    QList<int> todo;
    int lore  = 0;
    int names = 0;
    for (int i = 0; i < m_rowSource.size(); ++i) {
        if (!m_table->item(i, ColTranslation)->text().trimmed().isEmpty())
            continue;
        // The user's file first, then the built-in lore table: a rule the
        // user wrote is a decision, the table is a default.
        QString canonical = m_rules.terms.value(m_rowSource[i].trimmed().toLower());
        if (canonical.isEmpty())
            canonical = lore_overrides::lookup(m_rowSource[i], m_language);
        // Then the same two sources again as SHAPES, for the mod that names a
        // hundred things one way. Exact entries are tried first on purpose:
        // that is what lets a name the shape gets wrong keep its own answer.
        if (canonical.isEmpty())
            canonical = translation_rules::applyPatterns(m_rowSource[i],
                                                         m_rules.patterns);
        if (canonical.isEmpty())
            canonical = translation_rules::applyPatterns(
                m_rowSource[i], lore_overrides::patternsFor(m_language));
        if (!canonical.isEmpty()) {
            m_table->item(i, ColTranslation)->setText(canonical);
            ++lore;
            continue;
        }
        // A row that is somebody's name has nothing in it to translate, and
        // asking anyway is what returned "sin respirar" for "Dagoth Andas".
        // Left blank, which onAccept drops, so the string stays as it is.
        if (term_protect::looksLikeName(m_rowSource[i], m_mtNames, m_rules.ordinary)) {
            ++names;
            continue;
        }
        todo << i;
    }

    if (todo.isEmpty()) {
        ui::info(this, T("translate_machine"),
                 (lore + names) > 0
                     ? T("translate_lore_only").arg(lore + names)
                     : T("translate_machine_nothing"));
        return;
    }
    if (!ui::confirm(this, T("translate_machine"),
                     names > 0
                         ? T("translate_machine_confirm_names").arg(todo.size())
                                                               .arg(names)
                         : T("translate_machine_confirm").arg(todo.size())))
        return;

    // Split the work in two passes. The names have to be answered FIRST,
    // because every other row needs their rendering to substitute back in.
    m_mtQueue.clear();
    m_mtPending.clear();
    for (int rowIdx : todo) {
        const QString masked = term_protect::mask(m_rowSource[rowIdx], m_mtNames);
        if (term_protect::isOnlyNames(masked, int(m_mtNames.size())))
            m_mtQueue << rowIdx;      // the row IS a name: pass one
        else
            m_mtPending << rowIdx;    // pass two
    }
    // Any name without a row of its own still needs answering, so it rides
    // pass one as a negative index (-1 - nameIndex).
    for (int i = 0; i < m_mtNames.size(); ++i) {
        if (!m_nameRendering[i].isEmpty()) continue;
        bool hasRow = false;
        for (int rowIdx : m_mtQueue)
            if (nameRowIndex(rowIdx) == i) { hasRow = true; break; }
        if (!hasRow) m_mtQueue << (-1 - i);
    }

    m_mtNamePhase = !m_mtQueue.isEmpty();
    if (!m_mtNamePhase) { m_mtQueue = m_mtPending; m_mtPending.clear(); }

    // Every row of this run says so before a single request goes out: a row
    // that will be asked about in ten minutes should not look identical to one
    // that came back empty.
    {
        ProgrammaticEdit guard(m_expanding);
        for (int r : std::as_const(m_mtQueue))   if (r >= 0) setPending(r, 1);
        for (int r : std::as_const(m_mtPending)) if (r >= 0) setPending(r, 1);
    }

    if (!m_mtAnim) {
        m_mtAnim = new QTimer(this);
        m_mtAnim->setInterval(120);   // the mod list's spinner rate
        connect(m_mtAnim, &QTimer::timeout, this, [this] {
            m_mtFrame = (m_mtFrame + 1) % 10;
            // static_cast, not qobject_cast: this delegate has no Q_OBJECT
            // (it has no signals or slots), and it is the one buildUi set on
            // that column and the only thing that ever sets it.
            if (auto *d = static_cast<PendingDelegate *>(
                    m_table->itemDelegateForColumn(ColTranslation)))
                d->setFrame(m_mtFrame);
            m_table->viewport()->update();
        });
    }
    m_mtAnim->start();

    if (!m_net) m_net = new QNetworkAccessManager(this);
    if (!m_mtPace) {
        // One request per tick, at google_translate::kRequestSpacingMs. The
        // old dispatcher let every reply fire the next, which on a fast link
        // is tens of requests a second - and that is what earns the 429.
        m_mtPace = new QTimer(this);
        m_mtPace->setInterval(google_translate::kRequestSpacingMs);
        connect(m_mtPace, &QTimer::timeout,
                this, &TranslateDialog::pumpMachineTranslate);
    }
    m_mtStopped = false;
    // Coming back after a block: open with one request and wait for it. The
    // wait expiring only says our timer ran out, not that Google's did.
    m_mtProbe   = Settings::translateBlockStrikes() > 0;
    m_mtBtn->setEnabled(false);
    if (m_mtCooloffTick) m_mtCooloffTick->stop();
    m_mtBar->resetFormat();          // no stale "Blocked - 0:00" into a run
    m_mtBar->setVisible(true);
    m_mtTotal  = int(m_mtQueue.size()) + int(m_mtPending.size());
    m_mtBar->setRange(0, m_mtTotal);
    m_mtBar->setValue(0);
    m_mtDone   = 0;
    m_mtTally  = {};
    m_mtFirstError.clear();
    m_mtFirstHttpStatus = 0;
    m_mtInFlight = 0;

    pumpMachineTranslate();
}

void TranslateDialog::pumpMachineTranslate()
{
    // Stopped for a reason - DownloadQueue::processDownloadQueue opens the same
    // way. A block clears the queues, and a tick already in the loop must not
    // start them up again.
    if (m_mtStopped)         { if (m_mtPace) m_mtPace->stop(); return; }
    if (m_mtQueue.isEmpty()) { if (m_mtPace) m_mtPace->stop(); return; }

    // The cap no longer sets the pace, the timer does; this only stops replies
    // piling up when the endpoint is slow. The timer keeps ticking and the next
    // tick tries again.
    if (m_mtInFlight >= google_translate::kMaxInFlight) return;
    // Probing: one request, and nothing else until it answers. A stale block
    // then costs a single refusal instead of a handful, and the queue and the
    // rows are still there to resume from.
    if (m_mtProbe && m_mtInFlight >= 1) return;

    while (!m_mtQueue.isEmpty()) {
        const int item = m_mtQueue.takeFirst();

        // Negative encodes "this is a bare name with no row of its own".
        const int nameIdx = item < 0 ? (-1 - item) : nameRowIndex(item);
        const bool isName = m_mtNamePhase;
        const QString source = item < 0 ? m_mtNames[nameIdx] : m_rowSource[item];

        // A name is sent as itself: masking it would leave nothing to
        // translate, and asking about a bare token is what came back "Nrvaá".
        const QString named = isName ? source
                                     : term_protect::mask(source, m_mtNames);

        // Then the parts that are not prose at all - tags, %Name, escapes.
        // Held back from the translator entirely rather than hoped over: it
        // translates ALIGN, accents COLOR and moves a closing tag to wherever
        // Spanish wants the noun it thinks the tag is.
        const QStringList spans = markup_protect::findSpans(named);
        const QString     sent  = markup_protect::mask(named, spans);

        // A record that is nothing but markup has nothing to translate.
        // Blank leaves the original in place, which is exactly right for it,
        // and it costs no request.
        if (markup_protect::isOnlySpans(sent)) {
            if (item >= 0) {
                ProgrammaticEdit guard(m_expanding);
                setPending(item, 0);
            }
            m_mtBar->setValue(++m_mtDone);
            // Costs no request, so it costs no tick either: spending 350 ms on
            // a row nothing is asked about would be a wait for nothing.
            continue;
        }

        ++m_mtInFlight;                 // below the free skip, which sends nothing
        if (item >= 0) {
            ProgrammaticEdit guard(m_expanding);
            setPending(item, 2);        // on the wire now, not merely queued
        }

        QNetworkRequest req(google_translate::requestUrl(sent, isoFor(m_language)));
        req.setRawHeader("Accept", "application/json");
        req.setRawHeader("User-Agent", google_translate::userAgent());

        QNetworkReply *reply = m_net->get(req);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, item, nameIdx, isName, named, spans]() {
            reply->deleteLater();

            const int http = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();

            QString text;
            bool    repaired = false;
            // Left guarded: on a 429 the body is an HTML "Sorry..." page, and
            // there is nothing to gain from putting that through a JSON parser.
            if (reply->error() == QNetworkReply::NoError)
                text = google_translate::parseResponse(reply->readAll());

            // Classified on the RAW answer, before restore() and the user's
            // after-rules touch it: this is a statement about the endpoint, and
            // a rule of the user's own that blanks a row must not read as
            // Google having failed.
            const auto outcome = google_translate::classify(
                int(reply->error()), http, !text.isEmpty());
            m_mtTally.count(outcome);
            // An answer means the block has lifted: stop probing and let the
            // pace timer run the rest of the queue at full speed.
            if (outcome == google_translate::Failure::Ok) m_mtProbe = false;
            if (outcome != google_translate::Failure::Ok
                && m_mtFirstError.isEmpty()) {
                m_mtFirstError      = reply->errorString();
                m_mtFirstHttpStatus = http;
            }

            // A 429 means the endpoint has stopped answering this client, so
            // the other 163 requests will 429 too - and sending them is what
            // makes the block last longer. Only a 429: a 403 is a refusal, not
            // a rate limit, and the run is allowed to finish and report it.
            //
            // Requests already in flight are left alone. They are paid for and
            // may still answer, and cancelling would manufacture status-0
            // replies that classify() would read as Offline - the run would
            // then blame the user's network for our own abort.
            if (!m_mtStopped
                && outcome == google_translate::Failure::Blocked) {
                m_mtStopped = true;
                m_mtQueue.clear();
                m_mtPending.clear();
                if (m_mtPace) m_mtPace->stop();
                // Stamped here, not at teardown: closing the dialog between
                // the block and the last reply must not lose it.
                Settings::setTranslateBlockedAt(QDateTime::currentDateTimeUtc());
                // And each refusal in a row buys a longer wait. Fifteen
                // minutes was a guess that a twelve-hour block made a liar of,
                // and returning at full cadence is what renews it.
                //
                // Except when this was the probe: finding the same block still
                // in force is not a new offence, and counting it would let one
                // bad night ratchet the wait to a day all by itself.
                if (!(m_mtProbe && m_mtTally.ok == 0)) {
                    Settings::setTranslateBlockStrikes(
                        Settings::translateBlockStrikes() + 1);
                }
            }
            if (!text.isEmpty() && !spans.isEmpty()) {
                // Before the user's own after-rules, so those see the real
                // markup rather than a row of tokens.
                const auto restored = markup_protect::restore(named, text, spans);
                text     = restored.text;
                repaired = restored.repaired;
            }
            if (!text.isEmpty())
                text = translation_rules::applyAfter(text, m_rules);

            if (text.isEmpty()) {
                // Counted by the tally above; nothing to write.
            } else if (isName) {
                // The answer for this name, reused everywhere from here on.
                if (nameIdx >= 0 && nameIdx < m_nameRendering.size())
                    m_nameRendering[nameIdx] = text;
                if (item >= 0 && item < m_table->rowCount()) {
                    ProgrammaticEdit guard(m_expanding);
                    m_table->item(item, ColTranslation)->setText(text);
                }
            } else if (item >= 0 && item < m_table->rowCount()
                       && m_table->item(item, ColTranslation)
                              ->text().trimmed().isEmpty()) {
                // Keep the masked form: it is what lets this row follow the
                // name if the user changes their mind about it. Guarded, or
                // storing it reads as a hand edit and blanks the row.
                {
                    ProgrammaticEdit guard(m_expanding);
                    auto *cell = m_table->item(item, ColTranslation);
                    cell->setData(TemplateRole, text);
                    cell->setData(RepairedRole, repaired);
                }
                expandRow(item);
            }

            if (item >= 0) {
                ProgrammaticEdit guard(m_expanding);
                setPending(item, 0);    // answered, whatever the answer was
            }

            --m_mtInFlight;
            m_mtBar->setValue(++m_mtDone);

            // Dispatch belongs to the pace timer now. A reply firing the next
            // request itself is exactly what made this a burst.
            advanceMachineTranslate();
        });
        break;                          // one request per tick
    }

    // Whatever is left goes at the spacing, not as fast as replies land.
    if (!m_mtQueue.isEmpty() && m_mtPace && !m_mtPace->isActive())
        m_mtPace->start();
    // A tick that dispatched nothing - every remaining row was markup-only -
    // still has to be able to end the run.
    advanceMachineTranslate();
}

// Called whenever the run might be over: after a reply lands, and after a tick
// that dispatched nothing.
//
// Split out because the markup-only skip can empty the queue with nothing in
// flight, and the completion path used to live only inside the reply handler -
// so a run whose last rows were all markup left the button disabled and the
// spinner turning, with no reply left to come and finish it.
void TranslateDialog::advanceMachineTranslate()
{
    if (!m_mtQueue.isEmpty()) return;   // the pace timer has more to send
    if (m_mtInFlight > 0)     return;   // waiting on what is already out

    if (!m_mtStopped && m_mtNamePhase && !m_mtPending.isEmpty()) {
        // Names are in; now the rows that need them. The first goes at once
        // and the rest are paced - BulkInstallQueue::enqueue's shape.
        m_mtNamePhase = false;
        m_mtQueue     = m_mtPending;
        m_mtPending.clear();
        pumpMachineTranslate();
        return;
    }
    m_mtNamePhase = false;
    finishMachineTranslate();
}

// The one teardown, with three callers. Every one of them needs all of it: a
// half-torn-down run leaves the button disabled for good.
void TranslateDialog::finishMachineTranslate()
{
    m_mtQueue.clear();              // both, always - a surviving m_mtPending
    m_mtPending.clear();            // would start pass two after an abort
    m_mtNamePhase = false;
    if (m_mtPace) m_mtPace->stop();
    if (m_mtAnim) m_mtAnim->stop();
    m_mtBtn->setEnabled(true);      // still the only place this happens
    {
        // Nothing is waiting any more, including rows a stopped run never
        // reached.
        ProgrammaticEdit guard(m_expanding);
        for (int r = 0; r < m_table->rowCount(); ++r) setPending(r, 0);
    }
    m_table->viewport()->update();
    restyleLinkedRows();
    // Forget this and pumpMachineTranslate returns immediately for the rest of
    // the dialog's life, with nothing on screen to say why.
    m_mtProbe   = false;
    m_mtStopped = false;
    m_mtBar->setVisible(false);
    // A run that got answers and was not turned away means the block has
    // genuinely lapsed, so the ladder starts over. Without this the wait would
    // only ever grow, and one bad night would cost a day.
    if (m_mtTally.ok > 0 && m_mtTally.blocked == 0
        && Settings::translateBlockStrikes() != 0) {
        Settings::setTranslateBlockStrikes(0);
    }

    updateCooloffDisplay();

    // One message, naming what actually happened. This used to be a
    // single string that blamed rate-limiting for every failure -
    // right for a 429, and advice to "try again in a moment" that
    // could be repeated forever on a machine with no network.
    if (m_mtTally.failed() > 0) {
        const int done = m_mtTally.ok;
        QString body;
        switch (google_translate::worstOf(m_mtTally)) {
            case google_translate::Failure::Blocked:
                body = T("translate_mt_blocked")
                           .arg(done)
                           .arg(google_translate::cooloffMinutesFor(
                                    Settings::translateBlockStrikes()));
                break;
            case google_translate::Failure::Refused:
                body = T("translate_mt_refused").arg(done);
                break;
            case google_translate::Failure::Offline:
                body = T("translate_mt_offline")
                           .arg(done).arg(m_mtFirstError);
                break;
            case google_translate::Failure::HttpError:
                body = T("translate_mt_http_error")
                           .arg(m_mtFirstHttpStatus)
                           .arg(m_mtTally.failed()).arg(done);
                break;
            default:
                body = T("translate_machine_failed")
                           .arg(done).arg(m_mtTally.failed());
                break;
        }
        // Rows the run never reached - a stopped run leaves some
        // untouched, and they are blank for a different reason than
        // the ones that were asked about and came back unusable.
        const int neverSent = m_mtTotal - m_mtDone;
        if (neverSent > 0)
            body += T("translate_mt_not_sent").arg(neverSent);
        ui::warn(this, T("translate_machine"), body);
    }
}


int TranslateDialog::nameRowIndex(int row) const
{
    if (row < 0 || row >= m_rowSource.size()) return -1;
    const QString src = m_rowSource[row].trimmed();
    for (int i = 0; i < m_mtNames.size(); ++i)
        if (src.compare(m_mtNames[i].trimmed(), Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

void TranslateDialog::expandRow(int row)
{
    auto *cell = m_table->item(row, ColTranslation);
    if (!cell) return;
    const QString tmpl = cell->data(TemplateRole).toString();
    if (tmpl.isEmpty()) return;

    // Substitute each name's CURRENT rendering; a name still undecided falls
    // back to its original text rather than leaving a token on screen.
    QStringList subs;
    subs.reserve(m_mtNames.size());
    for (int i = 0; i < m_mtNames.size(); ++i)
        subs << (m_nameRendering.value(i).isEmpty() ? m_mtNames[i]
                                                    : m_nameRendering[i]);

    ProgrammaticEdit guard(m_expanding);
    cell->setText(term_protect::unmask(tmpl, subs));
}

// The pending mark, always through here so the guard is never forgotten: a
// write to this column that is not guarded reads as the user typing, and the
// row silently detaches from the template its answer is coming back as.
void TranslateDialog::setPending(int row, int state)
{
    if (row < 0 || row >= m_table->rowCount()) return;
    if (auto *cell = m_table->item(row, ColTranslation))
        cell->setData(PendingRole, state);
}

void TranslateDialog::onRowDoubleClicked(int row, int column)
{
    Q_UNUSED(column);   // either column opens it: the original is worth reading too
    openRowEditor(row);
}

// One row, big enough to read.
//
// These are not cells. A Morrowind book record is a paragraph inside two
// tags, and the table shows the first forty characters of it - which for the
// rows that need checking most is the markup and nothing else. So: the
// original in full, the translation editable under it, and Previous/Next to
// walk the list without closing and reopening the window for each one.
void TranslateDialog::openRowEditor(int row)
{
    if (row < 0 || row >= m_table->rowCount()) return;

    QDialog dlg(this);
    // The parent's title already names the mod and the language; carrying it
    // here means the window says what it is when it is the only one on screen.
    dlg.setWindowTitle(windowTitle());
    dlg.resize(760, 560);

    auto *lay = new QVBoxLayout(&dlg);
    auto *where = new QLabel(&dlg);
    where->setStyleSheet(QStringLiteral("font-weight: bold;"));
    lay->addWidget(where);

    lay->addWidget(new QLabel(T("translate_col_source"), &dlg));
    auto *srcBox = new QPlainTextEdit(&dlg);
    srcBox->setReadOnly(true);
    // The source is what the plugin says; this window reads it, never edits it.
    srcBox->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    lay->addWidget(srcBox, 1);

    lay->addWidget(new QLabel(T("translate_col_translation"), &dlg));
    auto *dstBox = new QPlainTextEdit(&dlg);
    dstBox->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    lay->addWidget(dstBox, 1);

    // Walking the list is not accepting or rejecting anything, so the two
    // navigation buttons get their own row on the left rather than being
    // sorted in among OK and Cancel by button role.
    auto *navRow = new QHBoxLayout;
    auto *prev = new QPushButton(T("translate_row_prev"), &dlg);
    auto *next = new QPushButton(T("translate_row_next"), &dlg);
    navRow->addWidget(prev);
    navRow->addWidget(next);
    navRow->addStretch(1);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                      &dlg);
    navRow->addWidget(btns);
    lay->addLayout(navRow);

    int current = row;

    // Writing through the table rather than holding the text here: an edit
    // made in this window has to take the same path as one typed into the
    // cell, or it would not detach the row from its template and the next
    // change to a linked name would silently overwrite it.
    auto commit = [this, dstBox, &current] {
        auto *cell = m_table->item(current, ColTranslation);
        if (!cell) return;
        const QString typed = dstBox->toPlainText();
        if (typed != cell->text()) cell->setText(typed);
        // Reaching this means the user pressed OK, Previous or Next - not
        // Cancel. They have read the row, which is exactly what turns this
        // window into a review loop for a page of machine answers.
        setReviewed(current, true);
    };

    auto load = [this, srcBox, dstBox, where, prev, next, &current](int r) {
        current = r;
        srcBox->setPlainText(m_table->item(r, ColSource)->text());
        dstBox->setPlainText(m_table->item(r, ColTranslation)->text());
        // Counted against the rows on OFFER, not the raw table: with the
        // filter on, "row 3 of 50" is the truth and "row 4102 of 20000" is
        // not an answer to anything.
        const auto at = visiblePosition(r);
        where->setText(T("translate_row_of").arg(QString::number(at.first),
                                                 QString::number(at.second)));
        prev->setEnabled(prevVisible(r) >= 0);
        next->setEnabled(nextVisible(r) >= 0);
        dstBox->setFocus();
    };

    // Walking off the end of the page follows the row rather than stopping at
    // it: the editor is the natural way to read three hundred machine answers
    // in a row, and having to close it every two hundred would be absurd.
    auto go = [this, &load](int r) {
        if (r < 0) return;
        const int page = pageOfRow(r);
        if (page >= 0 && page != m_page) showPage(page);
        load(r);
    };

    QObject::connect(prev, &QPushButton::clicked, &dlg, [&] {
        commit();
        go(prevVisible(current));
    });
    QObject::connect(next, &QPushButton::clicked, &dlg, [&] {
        commit();
        go(nextVisible(current));
    });
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    load(row);
    if (dlg.exec() == QDialog::Accepted) commit();

    // Land the table on whatever row was last looked at, so closing the
    // window leaves the list where the eye already is.
    m_table->setCurrentCell(current, ColTranslation);
}

void TranslateDialog::restyleLinkedRows()
{
    // A tint drawn from the palette so it survives both themes, and light
    // enough to read through.
    QColor tint = palette().color(QPalette::Highlight);
    tint.setAlpha(48);
    const QBrush none(Qt::NoBrush);

    ProgrammaticEdit guard(m_expanding);
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto *cell = m_table->item(row, ColTranslation);
        auto *srcCell = m_table->item(row, ColSource);
        if (!cell || !srcCell) continue;

        const bool isName = nameRowIndex(row) >= 0;
        const bool linked = isName || !cell->data(TemplateRole).toString().isEmpty();

        // A rebuilt row outranks a linked one: the tint is the only thing
        // saying "the markup here was put back by us, not by the translator",
        // and that is the row to read before saving.
        if (cell->data(RepairedRole).toBool()) {
            QColor warn(210, 130, 0);
            warn.setAlpha(60);
            cell->setBackground(QBrush(warn));
            srcCell->setBackground(QBrush(warn));
            cell->setToolTip(T("translate_repaired_tip"));
            srcCell->setToolTip(T("translate_repaired_tip"));
            continue;
        }

        cell->setBackground(linked ? QBrush(tint) : none);
        srcCell->setBackground(linked ? QBrush(tint) : none);

        if (isName) {
            srcCell->setToolTip(T("translate_linked_name_tip"));
            cell->setToolTip(T("translate_linked_name_tip"));
        } else if (linked) {
            srcCell->setToolTip(T("translate_linked_row_tip"));
            cell->setToolTip(T("translate_linked_row_tip"));
        } else {
            srcCell->setToolTip(QString());
            cell->setToolTip(QString());
        }
    }
}

void TranslateDialog::onCellChanged(int row, int column)
{
    if (m_expanding || column != ColTranslation) return;

    // Not guarded means the user typed it. That is the whole test for "has a
    // human read this": everything the app writes for itself goes through a
    // ProgrammaticEdit guard and lands above this line.
    setReviewed(row, true);
    scheduleRecount();
    m_progressDirty = true;

    const int nameIdx = nameRowIndex(row);
    if (nameIdx >= 0) {
        // Editing the name here is editing it everywhere - that is the point
        // of the tint.
        m_nameRendering[nameIdx] = m_table->item(row, ColTranslation)->text().trimmed();
        for (int r = 0; r < m_table->rowCount(); ++r)
            if (r != row) expandRow(r);
        return;
    }

    // A hand-edited row stops following the name: the user has said what this
    // row should read, and quietly overwriting that later would be worse than
    // losing the link.
    auto *cell = m_table->item(row, ColTranslation);
    if (cell && !cell->data(TemplateRole).toString().isEmpty()) {
        {
            ProgrammaticEdit guard(m_expanding);
            cell->setData(TemplateRole, QString());
        }
        restyleLinkedRows();
    }
}

void TranslateDialog::onEditRules()
{
    if (m_rulesPath.isEmpty()) return;
    // Written on demand rather than at startup, so a user who never opens it
    // never gets a file they did not ask for. The template explains itself.
    if (!translation_rules::ensureTemplate(m_rulesPath, m_language)) {
        ui::warn(this, T("translate_edit_rules"),
                 T("translate_edit_rules_failed").arg(m_rulesPath));
        return;
    }
    subprocess::startDetached(QStringLiteral("xdg-open"), {m_rulesPath});
    ui::info(this, T("translate_edit_rules"),
             T("translate_edit_rules_opened").arg(QDir::toNativeSeparators(m_rulesPath)));
}

void TranslateDialog::onImportDatabase()
{
    // Nerevarine Scribe's translation database: UTF-8 lines of
    // "source=translation", "#" comments. Reusing it here is what carries
    // years of Scribe work (the user has seven in-progress projects) into
    // this editor without retyping a word. Exact matches only - Scribe's
    // fuzzy matching deliberately stays behind, because a near-miss written
    // into a plugin is worse than an empty field.
    const QString path = QFileDialog::getOpenFileName(
        this, T("translate_import_db"), QDir::homePath(),
        T("translate_import_db_filter"));
    if (path.isEmpty()) return;

    if (!m_memory) return;
    const auto imported = m_memory->importScribeDb(path);
    if (imported.read < 0) {
        ui::warn(this, T("translate_import_db"),
                 T("translate_import_db_unreadable").arg(path));
        return;
    }
    if (imported.added > 0) m_memoryChanged = true;
    const int read = imported.read, added = imported.added;

    // Light up rows the import can now answer - empty ones only, so nothing
    // the user typed is clobbered.
    int matched = 0;
    for (int i = 0; i < m_rowSource.size(); ++i) {
        if (!m_table->item(i, ColTranslation)->text().trimmed().isEmpty())
            continue;
        const QString known = m_memory ? m_memory->lookup(m_rowSource[i])
                                       : QString();
        if (known.isEmpty()) continue;
        m_table->item(i, ColTranslation)->setText(known);
        ++matched;
    }

    ui::info(this, T("translate_import_db"),
             T("translate_import_db_done")
                 .arg(read).arg(added).arg(matched));
}

// -- Half-finished work -------------------------------------------------

void TranslateDialog::fillFromProgress()
{
    int restored = 0;
    for (int i = 0; i < m_rowSource.size(); ++i) {
        const auto e = m_progress.lookup(m_rowSource[i]);
        if (e.translation.isEmpty()) continue;
        {
            ProgrammaticEdit guard(m_expanding);
            m_table->item(i, ColTranslation)->setText(e.translation);
            m_table->item(i, ColTranslation)->setData(ReviewedRole, e.reviewed);
        }
        ++restored;
    }
    if (restored > 0)
        setWindowTitle(windowTitle()
                       + T("translate_resume_suffix")
                             .arg(restored).arg(m_rowSource.size()));

    // A mod that has been updated since the last sitting will have dropped
    // some strings. Their answers are kept - the next update may bring them
    // back - but say so once rather than leaving the count looking wrong.
    const int stale = m_progress.staleAgainst(m_rowSource);
    if (stale > 0)
        ui::info(this, T("translate_machine"),
                 T("translate_resume_stale").arg(stale));
}

bool TranslateDialog::writeProgress()
{
    if (m_progressPath.isEmpty()) return true;   // nowhere to write, not a failure

    for (int i = 0; i < m_rowSource.size(); ++i) {
        auto *cell = m_table->item(i, ColTranslation);
        if (!cell) continue;
        const QString t = cell->text().trimmed();
        if (t.isEmpty()) { m_progress.forget(m_rowSource[i]); continue; }
        m_progress.record(m_rowSource[i], t, cell->data(ReviewedRole).toBool());
    }
    if (!m_progress.save(m_progressPath)) return false;
    m_progressDirty = false;
    return true;
}

void TranslateDialog::onSaveProgress()
{
    if (!writeProgress()) {
        // Stay open. Closing on a failed write is how a month disappears.
        ui::warn(this, T("translate_machine"),
                 T("translate_save_progress_failed").arg(m_progressPath));
        return;
    }
    int done = 0;
    for (int i = 0; i < m_rowSource.size(); ++i)
        if (!m_table->item(i, ColTranslation)->text().trimmed().isEmpty()) ++done;

    m_outcome = Outcome::Saved;
    ui::info(this, T("translate_machine"),
             T("translate_save_progress_done").arg(done).arg(m_rowSource.size()));
    accept();
}

void TranslateDialog::reject()
{
    if (!m_progressDirty || m_progressPath.isEmpty()) {
        m_outcome = Outcome::Cancelled;
        QDialog::reject();
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle(T("translate_machine"));
    box.setIcon(QMessageBox::Warning);
    box.setText(T("translate_close_unsaved"));
    auto *saveBtn  = box.addButton(T("translate_save_progress"),
                                   QMessageBox::AcceptRole);
    auto *dropBtn  = box.addButton(T("translate_close_discard"),
                                   QMessageBox::DestructiveRole);
    auto *stayBtn  = box.addButton(T("translate_close_keep"),
                                   QMessageBox::RejectRole);
    box.setDefaultButton(saveBtn);
    box.exec();

    if (box.clickedButton() == stayBtn) return;          // not closing after all
    if (box.clickedButton() == saveBtn) {
        if (!writeProgress()) {
            ui::warn(this, T("translate_machine"),
                     T("translate_save_progress_failed").arg(m_progressPath));
            return;                                       // still not closing
        }
        m_outcome = Outcome::Saved;
        QDialog::reject();
        return;
    }
    (void)dropBtn;
    m_outcome = Outcome::Cancelled;
    QDialog::reject();
}

TranslateDialog::AcceptPlan TranslateDialog::planAccept()
{
    AcceptPlan plan;
    for (int i = 0; i < m_rowSource.size(); ++i) {
        const QString t = m_table->item(i, ColTranslation)->text().trimmed();
        if (t.isEmpty()) continue;
        if (t == m_rowSource[i]) continue;   // typed the English back
        plan.byText.insert(translation_store::normalize(m_rowSource[i]), t);

        // The gate. An answer nobody has read ships into the mod - that is
        // what was asked for - but it does not join the shared memory, where
        // one bad guess would pre-fill every other mod the user opens.
        if (!m_table->item(i, ColTranslation)->data(ReviewedRole).toBool()) {
            ++plan.unreviewed;
            continue;
        }
        ++plan.remembered;
        if (m_memory && m_memory->lookup(m_rowSource[i]) != t) {
            m_memory->remember(m_rowSource[i], t);
            m_memoryChanged = true;
        }
    }
    return plan;
}

void TranslateDialog::onAccept()
{
    // Source text -> what the user wants it to say. Empty rows are dropped
    // rather than written as blanks: an untranslated string stays English.
    // The gate on the shared memory lives in planAccept, which is also the
    // half a test can reach.
    const AcceptPlan plan = planAccept();
    const QHash<QString, QString> &byText = plan.byText;

    // Unread machine answers DO go into the mod - refusing them would hand
    // back an empty mod to somebody who just machine-translated twenty
    // thousand rows. They are only held back from the shared memory. Say so,
    // once, rather than letting it be a surprise later.
    if (plan.unreviewed > 0
        && !ui::confirm(this, T("translate_machine"),
                        T("translate_unreviewed_warning").arg(plan.unreviewed)))
        return;

    if (byText.isEmpty()) {
        ui::info(this, T("translate_title").arg(QString()), T("translate_nothing_typed"));
        return;   // stay open; rejecting would throw away their place
    }

    // Fan one answer back out to every record that carries that source text.
    m_result.clear();
    for (const TranslatableString &s : m_strings) {
        const auto it = byText.constFind(translation_store::normalize(s.source));
        if (it == byText.constEnd()) continue;
        m_result[s.pluginRel].insert(s.key, it.value());
    }

    // A build is a save point too: the same answers, kept where a later
    // sitting will find them.
    writeProgress();
    m_outcome = Outcome::Build;
    accept();
}
