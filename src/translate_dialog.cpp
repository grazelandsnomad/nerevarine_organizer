#include "translate_dialog.h"

#include "prompts.h"
#include "translator.h"

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

// MyMemory: free, keyless, and the only reason machine translation can be
// offered at all without asking the user for an account. Rate-limited, so the
// button translates only the rows that are still empty.
constexpr const char *kMtEndpoint = "https://api.mymemory.translated.net/get";

// Our lowercase language tokens -> ISO codes the endpoint speaks.
QString isoFor(const QString &language)
{
    static const QHash<QString, QString> kIso = {
        {QStringLiteral("spanish"),  QStringLiteral("es")},
        {QStringLiteral("french"),   QStringLiteral("fr")},
        {QStringLiteral("german"),   QStringLiteral("de")},
        {QStringLiteral("italian"),  QStringLiteral("it")},
        {QStringLiteral("russian"),  QStringLiteral("ru")},
        {QStringLiteral("japanese"), QStringLiteral("ja")},
        {QStringLiteral("catalan"),  QStringLiteral("ca")},
        {QStringLiteral("greek"),    QStringLiteral("el")},
        {QStringLiteral("arabic"),   QStringLiteral("ar")},
        {QStringLiteral("thai"),     QStringLiteral("th")},
        {QStringLiteral("ukrainian"),QStringLiteral("uk")},
        {QStringLiteral("chinese_simplified"), QStringLiteral("zh")},
    };
    return kIso.value(language.trimmed().toLower());
}

enum Col { ColSource = 0, ColTranslation = 1, ColCount = 2 };

} // namespace

TranslateDialog::TranslateDialog(const QString &modName,
                                 const QList<TranslatableString> &strings,
                                 const QString &language,
                                 translation_store::Memory *memory,
                                 QWidget *parent)
    : QDialog(parent)
    , m_strings(strings)
    , m_language(language)
    , m_memory(memory)
{
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
        T("translate_intro").arg(QString::number(m_rowSource.size()),
                                 QString::number(m_strings.size())), this);
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
    lay->addWidget(m_table, 1);

    auto *row = new QHBoxLayout;
    m_mtBtn = new QPushButton(T("translate_machine"), this);
    m_mtBtn->setToolTip(T("translate_machine_tip"));
    m_mtBtn->setEnabled(!isoFor(m_language).isEmpty());
    connect(m_mtBtn, &QPushButton::clicked, this, &TranslateDialog::onMachineTranslate);
    row->addWidget(m_mtBtn);

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

    // Only the rows still empty: never overwrite something the user typed or
    // the memory supplied, and keep the request count down on a free endpoint.
    QList<int> todo;
    for (int i = 0; i < m_rowSource.size(); ++i)
        if (m_table->item(i, ColTranslation)->text().trimmed().isEmpty())
            todo << i;

    if (todo.isEmpty()) {
        ui::info(this, T("translate_machine"), T("translate_machine_nothing"));
        return;
    }
    if (!ui::confirm(this, T("translate_machine"),
                     T("translate_machine_confirm").arg(todo.size())))
        return;

    if (!m_net) m_net = new QNetworkAccessManager(this);
    m_mtBtn->setEnabled(false);
    m_mtBar->setVisible(true);
    m_mtBar->setRange(0, todo.size());
    m_mtBar->setValue(0);
    m_mtPending = int(todo.size());

    for (int rowIdx : todo) {
        QUrl url(QString::fromLatin1(kMtEndpoint));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("q"), m_rowSource[rowIdx]);
        q.addQueryItem(QStringLiteral("langpair"),
                       QStringLiteral("en|") + iso);
        url.setQuery(q);

        QNetworkReply *reply = m_net->get(QNetworkRequest(url));
        connect(reply, &QNetworkReply::finished, this, [this, reply, rowIdx]() {
            reply->deleteLater();
            if (reply->error() == QNetworkReply::NoError) {
                const QJsonObject root =
                    QJsonDocument::fromJson(reply->readAll()).object();
                const QString text = root.value(QStringLiteral("responseData"))
                                         .toObject()
                                         .value(QStringLiteral("translatedText"))
                                         .toString();
                // Only fill if still empty: the user may have typed into the
                // row while the request was in flight, and they win.
                if (!text.isEmpty() && rowIdx < m_table->rowCount()
                    && m_table->item(rowIdx, ColTranslation)->text().trimmed().isEmpty())
                    m_table->item(rowIdx, ColTranslation)->setText(text);
            }
            m_mtBar->setValue(m_mtBar->value() + 1);
            if (--m_mtPending <= 0) {
                m_mtBar->setVisible(false);
                m_mtBtn->setEnabled(true);
            }
        });
    }
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
