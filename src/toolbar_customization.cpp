#include "toolbar_customization.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMap>
#include <QPushButton>
#include <QVBoxLayout>
#include <Qt>

#include "settings.h"
#include "translator.h"

ToolbarCustomization::ToolbarCustomization(QObject *parent)
    : QObject(parent)
{
}

void ToolbarCustomization::registerAction(const QString &id, QAction *act,
                                           const QString &label, bool defaultVisible)
{
    if (!act) return;
    if (!act->property("nerev_profile_visible").isValid())
        act->setProperty("nerev_profile_visible", true);
    bool userVisible = Settings::toolbarActionVisible(id, defaultVisible);
    act->setProperty("nerev_user_visible", userVisible);
    m_actions[id] = act;
    m_labels[id]  = label;
    applyVisibility(act);
}

void ToolbarCustomization::applyVisibility(QAction *act)
{
    if (!act) return;
    bool prof = act->property("nerev_profile_visible").toBool();
    bool user = act->property("nerev_user_visible").toBool();
    act->setVisible(prof && user);
}

void ToolbarCustomization::applyAll()
{
    for (QAction *a : std::as_const(m_actions)) applyVisibility(a);
}

void ToolbarCustomization::showCustomizeDialog(QWidget *parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(T("toolbar_customize_title"));
    auto *lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(T("toolbar_customize_hint"), &dlg));

    auto *list = new QListWidget(&dlg);
    // Iterate in a stable order - use QMap to sort by ID for determinism.
    QMap<QString, QString> sorted;
    for (auto it = m_labels.constBegin(); it != m_labels.constEnd(); ++it)
        sorted.insert(it.key(), it.value());
    for (auto it = sorted.constBegin(); it != sorted.constEnd(); ++it) {
        QAction *act = m_actions.value(it.key());
        if (!act) continue;
        auto *row = new QListWidgetItem(it.value(), list);
        row->setData(Qt::UserRole, it.key());
        row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
        row->setCheckState(act->property("nerev_user_visible").toBool()
                           ? Qt::Checked : Qt::Unchecked);
    }
    lay->addWidget(list);

    auto *row = new QHBoxLayout;
    auto *allBtn  = new QPushButton(T("toolbar_customize_all"),  &dlg);
    auto *noneBtn = new QPushButton(T("toolbar_customize_none"), &dlg);
    row->addWidget(allBtn); row->addWidget(noneBtn); row->addStretch();
    lay->addLayout(row);
    connect(allBtn,  &QPushButton::clicked, &dlg, [list]{
        for (int i = 0; i < list->count(); ++i) list->item(i)->setCheckState(Qt::Checked);
    });
    connect(noneBtn, &QPushButton::clicked, &dlg, [list]{
        for (int i = 0; i < list->count(); ++i) list->item(i)->setCheckState(Qt::Unchecked);
    });

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);

    dlg.resize(380, 500);
    if (dlg.exec() != QDialog::Accepted) return;

    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *it = list->item(i);
        QString id = it->data(Qt::UserRole).toString();
        bool checked = (it->checkState() == Qt::Checked);
        Settings::setToolbarActionVisible(id, checked);
        if (QAction *act = m_actions.value(id)) {
            act->setProperty("nerev_user_visible", checked);
            applyVisibility(act);
        }
    }
}
