#include "modlist_summary_dialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
#include <Qt>

#include "translator.h"

namespace modlist_summary {

void showDialog(QWidget *parent, const View &view,
                const std::function<void()> &onMoveMods,
                const std::function<void()> &onConsolidate)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(T("summary_title"));
    dlg.setMinimumWidth(540);

    auto *vlay = new QVBoxLayout(&dlg);

    auto addRow = [&](const QString &label, const QString &value) {
        auto *row = new QHBoxLayout;
        auto *l = new QLabel("<b>" + label + "</b>", &dlg);
        l->setMinimumWidth(190);
        auto *v = new QLabel(value, &dlg);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->setWordWrap(true);
        v->setStyleSheet("font-family: monospace;");
        row->addWidget(l);
        row->addWidget(v, 1);
        vlay->addLayout(row);
    };

    addRow(T("summary_profile"),  view.profileName);
    addRow(T("summary_platform"), view.platform);
    vlay->addSpacing(10);

    addRow(T("summary_total_mods"), QString::number(view.stats.modCount));
    addRow(T("summary_enabled_mods"),
           QString("%1 / %2").arg(view.stats.enabledCount).arg(view.stats.modCount));
    addRow(T("summary_separator_count"), QString::number(view.stats.sepCount));
    vlay->addSpacing(10);

    addRow(T("summary_total_size"),   formatBytes(view.stats.totalBytes));
    addRow(T("summary_enabled_size"), formatBytes(view.stats.enabledBytes));
    vlay->addSpacing(10);

    addRow(T("summary_mods_dir"),      view.modsDir);
    addRow(T("summary_openmw_binary"), view.openmwBinary);
    addRow(T("summary_openmw_cfg"),    view.openmwCfg);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    auto *moveBtn = new QPushButton(T("summary_move_mods_btn"), &dlg);
    moveBtn->setStyleSheet("color: #8a4a1a; font-weight: bold;");
    moveBtn->setToolTip(T("summary_move_mods_tooltip"));
    btns->addButton(moveBtn, QDialogButtonBox::ActionRole);

    // Consolidate button: only meaningful when at least one mod lives outside
    // the active profile's modsDir.  The concrete count + folder list is
    // reported by the injected action, in its own confirmation dialog.
    if (view.outsideCount > 0 && onConsolidate) {
        auto *consolidateBtn = new QPushButton(
            T("summary_consolidate_btn").arg(view.outsideCount), &dlg);
        consolidateBtn->setStyleSheet("color: #6a1b9a; font-weight: bold;");
        consolidateBtn->setToolTip(T("summary_consolidate_tooltip"));
        btns->addButton(consolidateBtn, QDialogButtonBox::ActionRole);
        QObject::connect(consolidateBtn, &QPushButton::clicked, &dlg,
                         [&dlg, onConsolidate] {
            dlg.accept();
            onConsolidate();
        });
    }

    vlay->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(moveBtn, &QPushButton::clicked, &dlg, [&dlg, onMoveMods] {
        dlg.accept();
        if (onMoveMods) onMoveMods();
    });

    dlg.exec();
}

} // namespace modlist_summary
