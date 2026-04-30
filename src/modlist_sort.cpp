#include "modlist_sort.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QVector>
#include <Qt>

#include <algorithm>

#include "modroles.h"
#include "translator.h"

namespace modlist_sort {

void byDate(QListWidget *list, bool ascending)
{
    int n = list->count();
    QList<QListWidgetItem *> items;
    items.reserve(n);
    for (int i = 0; i < n; ++i)
        items << list->takeItem(0);

    std::stable_sort(items.begin(), items.end(),
        [ascending](QListWidgetItem *a, QListWidgetItem *b) {
            bool aIsSep = a->data(ModRole::ItemType).toString() == ItemType::Separator;
            bool bIsSep = b->data(ModRole::ItemType).toString() == ItemType::Separator;
            // Separators always sort to the end, preserving their relative order.
            if (aIsSep && bIsSep) return false;
            if (aIsSep)           return false;
            if (bIsSep)           return true;

            QDateTime da = a->data(ModRole::DateAdded).toDateTime();
            QDateTime db = b->data(ModRole::DateAdded).toDateTime();
            // Mods without a date sort to the end.
            if (!da.isValid() && !db.isValid()) return false;
            if (!da.isValid()) return false;
            if (!db.isValid()) return true;
            return ascending ? (da < db) : (da > db);
        });

    for (auto *item : items)
        list->addItem(item);
}

void bySize(QListWidget *list, bool ascending)
{
    int n = list->count();
    if (n == 0) return;

    QVector<bool> isSepAt(n);
    for (int i = 0; i < n; ++i)
        isSepAt[i] = list->item(i)->data(ModRole::ItemType).toString()
                     == ItemType::Separator;

    QList<QListWidgetItem *> seps, mods;
    for (int i = 0; i < n; ++i) {
        auto *it = list->takeItem(0);
        if (it->data(ModRole::ItemType).toString() == ItemType::Separator)
            seps.append(it);
        else
            mods.append(it);
    }

    std::stable_sort(mods.begin(), mods.end(),
        [ascending](QListWidgetItem *a, QListWidgetItem *b) {
            qint64 sa = a->data(ModRole::ModSize).toLongLong();
            qint64 sb = b->data(ModRole::ModSize).toLongLong();
            // Mods without a known size sort to the end in either direction.
            if (sa <= 0 && sb <= 0) return false;
            if (sa <= 0) return false;
            if (sb <= 0) return true;
            return ascending ? (sa < sb) : (sa > sb);
        });

    int sepIdx = 0, modIdx = 0;
    for (int i = 0; i < n; ++i) {
        if (isSepAt[i]) list->addItem(seps[sepIdx++]);
        else            list->addItem(mods[modIdx++]);
    }
}

QList<QListWidgetItem *> showReorderSeparatorsDialog(QWidget *parent,
                                                      QListWidget *list)
{
    // Break the modlist into "sections":
    //   [0]  = orphan section (mods that appear before any separator; stays in place)
    //   [1+] = real sections, each owned by one separator
    struct Section {
        QListWidgetItem *sep = nullptr;
        QList<QListWidgetItem *> mods;
    };
    QList<Section> sections;
    sections.append(Section{});                       // orphan section
    for (int i = 0; i < list->count(); ++i) {
        auto *it = list->item(i);
        if (it->data(ModRole::ItemType).toString() == ItemType::Separator) {
            sections.append(Section{ it, {} });
        } else {
            sections.last().mods.append(it);
        }
    }

    if (sections.size() <= 1) {
        QMessageBox::information(parent, T("sort_sep_title"), T("sort_sep_empty"));
        return {};
    }

    QDialog dlg(parent);
    dlg.setWindowTitle(T("sort_sep_title"));
    dlg.setMinimumWidth(520);
    dlg.setMinimumHeight(420);

    auto *vlay = new QVBoxLayout(&dlg);

    auto *explain = new QLabel(T("sort_sep_explain"), &dlg);
    explain->setWordWrap(true);
    explain->setStyleSheet("color: #444; padding: 6px; "
                            "background: #f4f1ee; border-radius: 4px;");
    vlay->addWidget(explain);

    auto *hbox = new QHBoxLayout;
    auto *uiList = new QListWidget(&dlg);
    uiList->setDragDropMode(QAbstractItemView::InternalMove);
    uiList->setDefaultDropAction(Qt::MoveAction);
    uiList->setSelectionMode(QAbstractItemView::SingleSelection);
    uiList->setAlternatingRowColors(true);
    uiList->setSpacing(1);

    // Populate: one row per separator (skip the orphan section at index 0).
    // UserRole = original section index into `sections`.
    for (int i = 1; i < sections.size(); ++i) {
        auto *sep = sections[i].sep;
        int mods  = sections[i].mods.size();
        auto *row = new QListWidgetItem(
            QString("%1   (%2)").arg(sep->text()).arg(mods), uiList);
        row->setBackground(sep->data(ModRole::BgColor).value<QColor>());
        row->setForeground(sep->data(ModRole::FgColor).value<QColor>());
        row->setData(Qt::UserRole, i);
        QFont bf = uiList->font(); bf.setBold(true); row->setFont(bf);
    }
    if (uiList->count() > 0) uiList->setCurrentRow(0);
    hbox->addWidget(uiList, 1);

    auto *btnCol = new QVBoxLayout;
    auto *upBtn = new QPushButton(QStringLiteral("▲"), &dlg);
    auto *dnBtn = new QPushButton(QStringLiteral("▼"), &dlg);
    upBtn->setFixedWidth(44);
    dnBtn->setFixedWidth(44);
    upBtn->setToolTip(T("sort_sep_up_tooltip"));
    dnBtn->setToolTip(T("sort_sep_down_tooltip"));
    btnCol->addWidget(upBtn);
    btnCol->addWidget(dnBtn);
    btnCol->addStretch();
    hbox->addLayout(btnCol);
    vlay->addLayout(hbox, 1);

    auto moveRow = [uiList](int delta) {
        int r = uiList->currentRow();
        int t = r + delta;
        if (r < 0 || t < 0 || t >= uiList->count()) return;
        auto *it = uiList->takeItem(r);
        uiList->insertItem(t, it);
        uiList->setCurrentRow(t);
    };
    QObject::connect(upBtn, &QPushButton::clicked, &dlg, [moveRow]{ moveRow(-1); });
    QObject::connect(dnBtn, &QPushButton::clicked, &dlg, [moveRow]{ moveRow(+1); });

    auto *btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    vlay->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return {};

    // Build the new order. Orphan section stays at the top.
    QList<QListWidgetItem *> newOrder = sections[0].mods;
    for (int i = 0; i < uiList->count(); ++i) {
        int origIdx = uiList->item(i)->data(Qt::UserRole).toInt();
        if (origIdx < 1 || origIdx >= sections.size()) continue;
        const Section &s = sections[origIdx];
        newOrder.append(s.sep);
        for (auto *m : s.mods) newOrder.append(m);
    }
    return newOrder;
}

} // namespace modlist_sort
