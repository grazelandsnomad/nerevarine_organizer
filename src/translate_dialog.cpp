#include "translate_dialog.h"

#include "prompts.h"
#include "translator.h"
#include "target_language.h"
#include "google_translate.h"
#include "lore_overrides.h"
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
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
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
                                 QWidget *parent)
    : QDialog(parent)
    , m_strings(strings)
    , m_language(language)
    , m_memory(memory)
    , m_rulesPath(rulesPath)
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
    fillFromMemory();
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

    m_table = new QTableWidget(m_rowSource.size(), ColCount, this);
    m_table->setHorizontalHeaderLabels({T("translate_col_source"),
                                        T("translate_col_translation")});
    m_table->horizontalHeader()->setSectionResizeMode(ColSource, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColTranslation, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);

    for (int i = 0; i < m_rowSource.size(); ++i) {
        auto *src = new QTableWidgetItem(m_rowSource[i]);
        // The source is what the plugin says; editing it here would be a lie.
        src->setFlags(src->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, ColSource, src);
        m_table->setItem(i, ColTranslation, new QTableWidgetItem(QString()));
    }
    connect(m_table, &QTableWidget::cellChanged,
            this, &TranslateDialog::onCellChanged);
    lay->addWidget(m_table, 1);

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

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Ok)->setText(T("translate_apply"));
    connect(box, &QDialogButtonBox::accepted, this, &TranslateDialog::onAccept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(box);
}

void TranslateDialog::fillFromMemory()
{
    if (!m_memory) return;
    int hits = 0;
    for (int i = 0; i < m_rowSource.size(); ++i) {
        const QString known = m_memory->lookup(m_rowSource[i]);
        if (known.isEmpty()) continue;
        m_table->item(i, ColTranslation)->setText(known);
        ++hits;
    }
    if (hits > 0)
        setWindowTitle(windowTitle() + T("translate_memory_suffix").arg(hits));
}

void TranslateDialog::onMachineTranslate()
{
    const QString iso = isoFor(m_language);
    if (iso.isEmpty()) return;

    // Rows already answered are left alone; a lore term or a user rule is a
    // decision and never goes to a machine translator.
    QList<int> todo;
    int lore = 0;
    for (int i = 0; i < m_rowSource.size(); ++i) {
        if (!m_table->item(i, ColTranslation)->text().trimmed().isEmpty())
            continue;
        // The user's file first, then the built-in lore table: a rule the
        // user wrote is a decision, the table is a default.
        QString canonical = m_rules.terms.value(m_rowSource[i].trimmed().toLower());
        if (canonical.isEmpty())
            canonical = lore_overrides::lookup(m_rowSource[i], m_language);
        if (!canonical.isEmpty()) {
            m_table->item(i, ColTranslation)->setText(canonical);
            ++lore;
            continue;
        }
        todo << i;
    }

    if (todo.isEmpty()) {
        ui::info(this, T("translate_machine"),
                 lore > 0 ? T("translate_lore_only").arg(lore)
                          : T("translate_machine_nothing"));
        return;
    }
    if (!ui::confirm(this, T("translate_machine"),
                     T("translate_machine_confirm").arg(todo.size())))
        return;

    // The mod's recurring proper nouns. They are not frozen - they are
    // translated ONCE and that one answer is carried into every row that
    // mentions them, which is what stopped Forfeoranna Heim SSE from calling
    // its dungeon three different things.
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
        m_nameRendering << known;
    }

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

    if (!m_net) m_net = new QNetworkAccessManager(this);
    m_mtBtn->setEnabled(false);
    m_mtBar->setVisible(true);
    m_mtTotal  = int(m_mtQueue.size()) + int(m_mtPending.size());
    m_mtBar->setRange(0, m_mtTotal);
    m_mtBar->setValue(0);
    m_mtDone   = 0;
    m_mtFailed = 0;
    m_mtInFlight = 0;

    pumpMachineTranslate();
}

void TranslateDialog::pumpMachineTranslate()
{
    // A queue, not a fan-out. The free endpoint starts refusing when a whole
    // mod's worth of strings arrives at once, and 164 strings in one mod is an
    // ordinary size here.
    while (!m_mtQueue.isEmpty()
           && m_mtInFlight < google_translate::kMaxInFlight) {
        const int item = m_mtQueue.takeFirst();
        ++m_mtInFlight;

        // Negative encodes "this is a bare name with no row of its own".
        const int nameIdx = item < 0 ? (-1 - item) : nameRowIndex(item);
        const bool isName = m_mtNamePhase;
        const QString source = item < 0 ? m_mtNames[nameIdx] : m_rowSource[item];

        // A name is sent as itself: masking it would leave nothing to
        // translate, and asking about a bare token is what came back "Nrvaá".
        const QString sent = isName ? source
                                    : term_protect::mask(source, m_mtNames);

        QNetworkRequest req(google_translate::requestUrl(sent, isoFor(m_language)));
        req.setRawHeader("Accept", "application/json");
        req.setRawHeader("User-Agent", google_translate::userAgent());

        QNetworkReply *reply = m_net->get(req);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, item, nameIdx, isName]() {
            reply->deleteLater();

            QString text;
            if (reply->error() == QNetworkReply::NoError)
                text = google_translate::parseResponse(reply->readAll());
            if (!text.isEmpty())
                text = translation_rules::applyAfter(text, m_rules);

            if (text.isEmpty()) {
                ++m_mtFailed;
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
                    m_table->item(item, ColTranslation)->setData(TemplateRole, text);
                }
                expandRow(item);
            }

            --m_mtInFlight;
            m_mtBar->setValue(++m_mtDone);

            if (!m_mtQueue.isEmpty()) { pumpMachineTranslate(); return; }
            if (m_mtInFlight > 0) return;

            // Names are in; now the rows that need them.
            if (m_mtNamePhase && !m_mtPending.isEmpty()) {
                m_mtNamePhase = false;
                m_mtQueue = m_mtPending;
                m_mtPending.clear();
                pumpMachineTranslate();
                return;
            }
            m_mtNamePhase = false;

            m_mtBar->setVisible(false);
            m_mtBtn->setEnabled(true);
            restyleLinkedRows();
            if (m_mtFailed > 0)
                ui::warn(this, T("translate_machine"),
                         T("translate_machine_failed")
                             .arg(m_mtTotal - m_mtFailed).arg(m_mtFailed));
        });
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

void TranslateDialog::onAccept()
{
    // Source text -> what the user wants it to say. Empty rows are dropped
    // rather than written as blanks: an untranslated string stays English.
    QHash<QString, QString> byText;
    for (int i = 0; i < m_rowSource.size(); ++i) {
        const QString t = m_table->item(i, ColTranslation)->text().trimmed();
        if (t.isEmpty()) continue;
        if (t == m_rowSource[i]) continue;   // typed the English back; not a translation
        byText.insert(translation_store::normalize(m_rowSource[i]), t);

        if (m_memory && m_memory->lookup(m_rowSource[i]) != t) {
            m_memory->remember(m_rowSource[i], t);
            m_memoryChanged = true;
        }
    }

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

    accept();
}
