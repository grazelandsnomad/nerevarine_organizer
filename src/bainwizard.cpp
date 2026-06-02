#include "bainwizard.h"
#include "translator.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

void BainWizard::showAsync(
    const QString &modPath,
    QWidget *parent,
    std::function<void(const QString &, const QString &)> onDone)
{
    auto *dlg = new BainWizard(modPath, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    // No packages somehow (caller should have checked bain::looksLikeBain) -
    // treat as a no-op cancel so the caller falls back to a plain install.
    if (dlg->m_packages.isEmpty()) {
        delete dlg;
        onDone({}, {});
        return;
    }

    dlg->buildUi();
    dlg->setWindowModality(Qt::NonModal);
    dlg->setWindowFlag(Qt::Window, true);

    QObject::connect(dlg, &QDialog::accepted, dlg, [dlg, onDone]() {
        const QStringList chosen = dlg->chosenNames();
        const QString staged = bain::stage(dlg->m_modPath, chosen);
        onDone(staged, chosen.join(QLatin1Char(';')));
    });
    QObject::connect(dlg, &QDialog::rejected, dlg,
                     [onDone]() { onDone({}, {}); });

    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

BainWizard::BainWizard(const QString &modPath, QWidget *parent)
    : QDialog(parent), m_modPath(modPath), m_packages(bain::packages(modPath))
{
    setModal(true);
    setMinimumSize(480, 360);
}

void BainWizard::buildUi()
{
    setWindowTitle(T("bain_dialog_title"));

    auto *main = new QVBoxLayout(this);
    main->setSpacing(6);

    auto *header = new QLabel(T("bain_dialog_header"), this);
    header->setWordWrap(true);
    main->addWidget(header);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget;
    auto *innerLay = new QVBoxLayout(inner);
    innerLay->setSpacing(4);

    for (const bain::Package &p : m_packages) {
        auto *box = new QCheckBox(p.name, inner);
        box->setChecked(true);   // all packages selected by default
        innerLay->addWidget(box);
        m_boxes.append(box);
    }
    innerLay->addStretch();
    scroll->setWidget(inner);
    main->addWidget(scroll, 1);

    auto *btns = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btns->button(QDialogButtonBox::Ok)->setText(T("bain_install"));
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main->addWidget(btns);
}

QStringList BainWizard::chosenNames() const
{
    QStringList out;
    for (int i = 0; i < m_packages.size() && i < m_boxes.size(); ++i)
        if (m_boxes[i]->isChecked())
            out.append(m_packages[i].name);
    return out;
}
