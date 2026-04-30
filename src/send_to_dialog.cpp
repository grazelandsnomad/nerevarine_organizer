#include "send_to_dialog.h"

#include <QColor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QString>
#include <QVBoxLayout>
#include <Qt>

#include "modroles.h"
#include "translator.h"

namespace send_to_dialog {

QListWidgetItem *pickSeparator(QWidget *parent, QListWidget *list)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(T("send_to_dialog_title"));
    dlg.setMinimumWidth(440);
    dlg.setMinimumHeight(360);

    auto *vlay   = new QVBoxLayout(&dlg);
    auto *search = new QLineEdit(&dlg);
    search->setPlaceholderText(T("send_to_search_placeholder"));
    search->setClearButtonEnabled(true);
    vlay->addWidget(search);

    auto *uiList = new QListWidget(&dlg);
    uiList->setAlternatingRowColors(true);

    // Populate with all separators, storing the source-list row index in
    // Qt::UserRole so we can resolve the real separator at Accept time.
    for (int i = 0; i < list->count(); ++i) {
        auto *cand = list->item(i);
        if (cand->data(ModRole::ItemType).toString() != ItemType::Separator) continue;
        auto *row = new QListWidgetItem(cand->text(), uiList);
        row->setData(Qt::UserRole, i);
        // Paint with the separator's own colors so it looks like the real one.
        row->setBackground(cand->data(ModRole::BgColor).value<QColor>());
        row->setForeground(cand->data(ModRole::FgColor).value<QColor>());
    }
    if (uiList->count() > 0) uiList->setCurrentRow(0);
    vlay->addWidget(uiList, 1);

    auto *btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    vlay->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(uiList, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);

    // Live filter: hide non-matching rows, auto-focus the first visible one
    // so the user can press Enter immediately ("Dung" → "Dungeons").
    QObject::connect(search, &QLineEdit::textChanged, &dlg,
            [uiList](const QString &q) {
        int firstVisible = -1;
        for (int i = 0; i < uiList->count(); ++i) {
            auto *row = uiList->item(i);
            bool match = q.isEmpty() ||
                         row->text().contains(q, Qt::CaseInsensitive);
            row->setHidden(!match);
            if (match && firstVisible < 0) firstVisible = i;
        }
        if (firstVisible >= 0) uiList->setCurrentRow(firstVisible);
    });

    QObject::connect(search, &QLineEdit::returnPressed, &dlg, [&dlg, uiList]{
        auto *cur = uiList->currentItem();
        if (cur && !cur->isHidden()) dlg.accept();
    });

    search->setFocus();

    if (dlg.exec() != QDialog::Accepted) return nullptr;
    auto *picked = uiList->currentItem();
    if (!picked || picked->isHidden()) return nullptr;
    int origRow = picked->data(Qt::UserRole).toInt();
    if (origRow < 0 || origRow >= list->count()) return nullptr;
    QListWidgetItem *sep = list->item(origRow);
    if (!sep || sep->data(ModRole::ItemType).toString() != ItemType::Separator)
        return nullptr;
    return sep;
}

} // namespace send_to_dialog
