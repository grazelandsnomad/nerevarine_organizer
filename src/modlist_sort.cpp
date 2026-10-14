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

#include "mod_row.h"
#include "modlist_model.h"
#include "modlistview.h"
#include "modroles.h"
#include "translator.h"
#include "prompts.h"

namespace modlist_sort {

QList<int> orderByDate(const QList<ModEntry> &rows, bool ascending)
{
    QList<int> order;
    order.reserve(rows.size());
    for (int i = 0; i < rows.size(); ++i) order << i;

    std::stable_sort(order.begin(), order.end(),
        [&rows, ascending](int ia, int ib) {
            const ModEntry &a = rows.at(ia);
            const ModEntry &b = rows.at(ib);
            // Separators go to the end, keeping their relative order.
            if (a.isSeparator() || b.isSeparator()) return !a.isSeparator() && b.isSeparator();

            const QDateTime &da = a.dateAdded;
            const QDateTime &db = b.dateAdded;
            // Mods without a date sort to the end.
            if (!da.isValid() || !db.isValid()) return da.isValid() && !db.isValid();
            return ascending ? (da < db) : (da > db);
        });
    return order;
}

QList<int> orderBySize(const QList<ModEntry> &rows, bool ascending)
{
    // Separators keep their slots; the mods are sorted globally and poured
    // back into the mod slots in order.
    QList<int> mods;
    for (int i = 0; i < rows.size(); ++i)
        if (!rows.at(i).isSeparator()) mods << i;

    std::stable_sort(mods.begin(), mods.end(),
        [&rows, ascending](int ia, int ib) {
            const qint64 sa = rows.at(ia).modSize;
            const qint64 sb = rows.at(ib).modSize;
            // Unknown-size mods go to the end either direction.
            if (sa <= 0 || sb <= 0) return sa > 0 && sb <= 0;
            return ascending ? (sa < sb) : (sa > sb);
        });

    QList<int> order;
    order.reserve(rows.size());
    int next = 0;
    for (int i = 0; i < rows.size(); ++i)
        order << (rows.at(i).isSeparator() ? i : mods.at(next++));
    return order;
}

namespace {

} // namespace

void byDate(ModlistModel *model, bool ascending)
{
    model->reorder(orderByDate(model->all(), ascending));
}

void bySize(ModlistModel *model, bool ascending)
{
    if (model->count() == 0) return;
    model->reorder(orderBySize(model->all(), ascending));
}

QList<ModRow> showReorderSeparatorsDialog(QWidget *parent, ModListView *list)
{
    // Split into sections:
    //   [0]  = orphans (mods before the first separator; stay put)
    //   [1+] = one section per separator
    struct Section {
        ModRow sep;
        QList<ModRow> mods;
    };
    QList<Section> sections;
    sections.append(Section{});                       // orphans
    for (int i = 0; i < list->count(); ++i) {
        const ModRow it = list->item(i);
        if (mod_row::isSeparator(it)) {
            sections.append(Section{ it, {} });
        } else {
            sections.last().mods.append(it);
        }
    }

    if (sections.size() <= 1) {
        ui::info(parent, T("sort_sep_title"), T("sort_sep_empty"));
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

    // One row per separator (orphans at index 0 are skipped).
    // UserRole holds the original index into `sections`.
    for (int i = 1; i < sections.size(); ++i) {
        const ModRow sep = sections[i].sep;
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

    // Rebuild order; orphans stay on top.
    QList<ModRow> newOrder = sections[0].mods;
    for (int i = 0; i < uiList->count(); ++i) {
        int origIdx = uiList->item(i)->data(Qt::UserRole).toInt();
        if (origIdx < 1 || origIdx >= sections.size()) continue;
        const Section &s = sections[origIdx];
        newOrder.append(s.sep);
        for (const ModRow &m : s.mods) newOrder.append(m);
    }
    return newOrder;
}

} // namespace modlist_sort
