#include "forbidden_mods.h"

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

#include "translator.h"

ForbiddenModsRegistry::ForbiddenModsRegistry(const QString &filePath, QObject *parent)
    : QObject(parent), m_filePath(filePath)
{
}

void ForbiddenModsRegistry::load()
{
    m_list.clear();

    // Try the portable file first (syncs between machines).
    QFile f(m_filePath);
    if (f.exists() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (line.trimmed().isEmpty() || line.startsWith('#')) continue;
            const QStringList parts = line.split('\t');
            ForbiddenMod fm;
            fm.name       = parts.value(0);
            fm.url        = parts.value(1);
            fm.annotation = parts.value(2);
            if (!fm.name.isEmpty() || !fm.url.isEmpty())
                m_list.append(fm);
        }
        f.close();
        return;
    }

    // File doesn't exist yet - migrate from QSettings (one-time).
    QSettings s;
    int n = s.value("forbidden/count", 0).toInt();
    for (int i = 0; i < n; ++i) {
        ForbiddenMod fm;
        fm.name       = s.value(QString("forbidden/%1/name").arg(i)).toString();
        fm.url        = s.value(QString("forbidden/%1/url").arg(i)).toString();
        fm.annotation = s.value(QString("forbidden/%1/annotation").arg(i)).toString();
        m_list.append(fm);
    }

    // One-time seed of built-in forbidden entries on first run.
    if (!s.value("forbidden/seeded_v1", false).toBool()) {
        m_list.prepend({
            "The Wabbajack",
            "https://www.nexusmods.com/morrowind/mods/44653",
            "Very important -> THIS MOD REQUIRES MWSE 0.9.5-alpha.20151016 "
            "<- it is not yet openMW compatible!"
        });
        s.setValue("forbidden/seeded_v1", true);
    }

    save();
}

void ForbiddenModsRegistry::save()
{
    QFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&f);
    for (const auto &fm : m_list)
        out << fm.name << '\t' << fm.url << '\t' << fm.annotation << '\n';
}

const ForbiddenMod *ForbiddenModsRegistry::find(const QString &game, int modId) const
{
    for (const auto &f : m_list) {
        QStringList p = QUrl(f.url).path().split('/', Qt::SkipEmptyParts);
        // Nexus URL path: {game}/mods/{modId}
        if (p.size() >= 3
                && p[0].compare(game, Qt::CaseInsensitive) == 0
                && p[2] == QString::number(modId))
            return &f;
    }
    return nullptr;
}

void ForbiddenModsRegistry::showManageDialog(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(T("forbidden_dlg_title"));
    dlg.setMinimumWidth(720);
    dlg.setMinimumHeight(380);

    auto *vlay  = new QVBoxLayout(&dlg);
    auto *table = new QTableWidget(0, 3, &dlg);
    table->setHorizontalHeaderLabels({
        T("forbidden_col_name"), T("forbidden_col_url"), T("forbidden_col_reason")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->setAlternatingRowColors(true);
    vlay->addWidget(table);

    auto *countLabel = new QLabel(&dlg);
    countLabel->setStyleSheet("color: #666; padding: 4px 2px;");
    auto populate = [&]() {
        table->setRowCount(0);
        for (const auto &f : m_list) {
            int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(f.name));
            table->setItem(row, 1, new QTableWidgetItem(f.url));
            table->setItem(row, 2, new QTableWidgetItem(f.annotation));
        }
        countLabel->setText(T("forbidden_count").arg(m_list.size()));
    };
    populate();
    vlay->addWidget(countLabel);

    auto *btnRow  = new QHBoxLayout;
    auto *addBtn  = new QPushButton(T("forbidden_add"));
    auto *editBtn = new QPushButton(T("forbidden_edit"));
    auto *rmBtn   = new QPushButton(T("forbidden_remove"));
    rmBtn->setStyleSheet("color: #cc2222;");
    btnRow->addWidget(addBtn);
    btnRow->addWidget(editBtn);
    btnRow->addWidget(rmBtn);
    btnRow->addStretch();
    vlay->addLayout(btnRow);

    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close);
    vlay->addWidget(closeBtns);
    connect(closeBtns, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);

    auto openEntry = [&](int idx) {
        QDialog entry(&dlg);
        entry.setWindowTitle(idx < 0 ? T("forbidden_entry_title_add")
                                     : T("forbidden_entry_title_edit"));
        entry.setMinimumWidth(520);
        auto *evlay  = new QVBoxLayout(&entry);
        auto *form   = new QFormLayout;
        auto *nameE  = new QLineEdit; nameE->setPlaceholderText(T("forbidden_ph_name"));
        auto *urlE   = new QLineEdit; urlE->setPlaceholderText(T("forbidden_ph_url"));
        auto *annotE = new QLineEdit; annotE->setPlaceholderText(T("forbidden_ph_reason"));
        if (idx >= 0) {
            nameE->setText(m_list[idx].name);
            urlE->setText(m_list[idx].url);
            annotE->setText(m_list[idx].annotation);
        }
        form->addRow(T("forbidden_label_name"),   nameE);
        form->addRow(T("forbidden_label_url"),    urlE);
        form->addRow(T("forbidden_label_reason"), annotE);
        evlay->addLayout(form);
        auto *ebtns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        evlay->addWidget(ebtns);
        connect(ebtns, &QDialogButtonBox::accepted, &entry, &QDialog::accept);
        connect(ebtns, &QDialogButtonBox::rejected, &entry, &QDialog::reject);
        if (entry.exec() != QDialog::Accepted) return;
        ForbiddenMod f{ nameE->text().trimmed(), urlE->text().trimmed(),
                        annotE->text().trimmed() };
        if (idx < 0)
            m_list.append(f);
        else
            m_list[idx] = f;
        save();
        populate();
    };

    connect(addBtn,  &QPushButton::clicked, &dlg, [&]{ openEntry(-1); });
    connect(editBtn, &QPushButton::clicked, &dlg, [&]{
        int row = table->currentRow();
        if (row >= 0 && row < m_list.size()) openEntry(row);
    });
    connect(rmBtn, &QPushButton::clicked, &dlg, [&]{
        int row = table->currentRow();
        if (row < 0 || row >= m_list.size()) return;
        m_list.removeAt(row);
        save();
        populate();
    });
    connect(table, &QTableWidget::cellDoubleClicked, &dlg,
            [&](int row, int /*col*/){
        if (row >= 0 && row < m_list.size()) openEntry(row);
    });

    dlg.exec();
}
