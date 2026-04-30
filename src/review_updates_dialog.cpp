#include "review_updates_dialog.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>

#include <algorithm>

#include "translator.h"

namespace ReviewUpdates {

QList<Candidate> showDialog(QWidget *parent, const QList<Candidate> &candidates)
{
    if (candidates.isEmpty()) return {};

    QDialog dlg(parent);
    dlg.setWindowTitle(T("review_updates_title"));
    dlg.setMinimumWidth(560);
    dlg.resize(560, std::min<int>(640, 180 + int(candidates.size()) * 22));

    auto *v = new QVBoxLayout(&dlg);

    auto *header = new QLabel(T("review_updates_body").arg(candidates.size()), &dlg);
    header->setWordWrap(true);
    v->addWidget(header);

    auto *shortcuts = new QHBoxLayout;
    auto *selAll  = new QPushButton(T("review_updates_select_all"),  &dlg);
    auto *selNone = new QPushButton(T("review_updates_select_none"), &dlg);
    shortcuts->addWidget(selAll);
    shortcuts->addWidget(selNone);
    shortcuts->addStretch();
    v->addLayout(shortcuts);

    auto *list = new QListWidget(&dlg);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    for (const auto &c : candidates) {
        auto *li = new QListWidgetItem(c.name, list);
        li->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        li->setCheckState(Qt::Checked);
        li->setToolTip(c.url);
    }
    v->addWidget(list, 1);

    auto *btns        = new QDialogButtonBox(&dlg);
    auto *cancelBtn   = btns->addButton(QDialogButtonBox::Cancel);
    auto *allBtn      = btns->addButton(T("review_updates_update_all"),
                                        QDialogButtonBox::ActionRole);
    auto *selectedBtn = btns->addButton(
        T("review_updates_update_selected").arg(candidates.size()),
        QDialogButtonBox::AcceptRole);
    selectedBtn->setDefault(true);
    v->addWidget(btns);

    auto countChecked = [list]() {
        int n = 0;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->checkState() == Qt::Checked) ++n;
        return n;
    };
    auto refreshSelectedLabel = [&]() {
        const int n = countChecked();
        selectedBtn->setText(T("review_updates_update_selected").arg(n));
        selectedBtn->setEnabled(n > 0);
    };

    QObject::connect(selAll, &QPushButton::clicked, &dlg, [list, &refreshSelectedLabel] {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setCheckState(Qt::Checked);
        refreshSelectedLabel();
    });
    QObject::connect(selNone, &QPushButton::clicked, &dlg, [list, &refreshSelectedLabel] {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setCheckState(Qt::Unchecked);
        refreshSelectedLabel();
    });
    QObject::connect(list, &QListWidget::itemChanged, &dlg,
            [&refreshSelectedLabel](QListWidgetItem *) { refreshSelectedLabel(); });

    enum class Choice { Cancel, Selected, All };
    Choice choice = Choice::Cancel;
    QObject::connect(cancelBtn,   &QAbstractButton::clicked, &dlg, [&] { choice = Choice::Cancel;   dlg.reject(); });
    QObject::connect(allBtn,      &QAbstractButton::clicked, &dlg, [&] { choice = Choice::All;      dlg.accept(); });
    QObject::connect(selectedBtn, &QAbstractButton::clicked, &dlg, [&] { choice = Choice::Selected; dlg.accept(); });

    dlg.exec();
    if (choice == Choice::Cancel) return {};

    // Walk the dialog's list in index-order so the user's seen order maps
    // 1-to-1 to the returned order - no surprise reshuffles.
    QList<Candidate> picked;
    for (int i = 0; i < list->count(); ++i) {
        if (choice == Choice::Selected && list->item(i)->checkState() != Qt::Checked)
            continue;
        picked.append(candidates[i]);
    }
    return picked;
}

} // namespace ReviewUpdates
