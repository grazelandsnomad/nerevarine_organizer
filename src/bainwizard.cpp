#include "bainwizard.h"
#include "translator.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

void BainWizard::showAsync(
    const QString &modPath,
    const QString &priorChoices,
    QWidget *parent,
    std::function<void(const QString &, const QString &)> onDone)
{
    auto *dlg = new BainWizard(modPath, priorChoices, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    // No packages (caller should have checked bain::looksLikeBain): no-op
    // cancel so the caller falls back to a plain install.
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

BainWizard::BainWizard(const QString &modPath, const QString &priorChoices,
                       QWidget *parent)
    : QDialog(parent), m_modPath(modPath),
      m_priorChoices(priorChoices.split(QLatin1Char(';'), Qt::SkipEmptyParts)),
      m_packages(bain::packages(modPath))
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

    // Use the remembered selection only if at least one remembered package
    // still exists (an updated archive may rename/drop packages); if none
    // match, fall back to first-install so the list isn't all-unchecked.
    const bool havePrior = std::any_of(
        m_packages.cbegin(), m_packages.cend(),
        [this](const bain::Package &p) { return m_priorChoices.contains(p.name); });

    // Checkbox list: remembered selection on re-install, else all-on so
    // "Choose packages..." starts from everything and the user prunes.
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    auto *inner = new QWidget;
    auto *innerLay = new QVBoxLayout(inner);
    innerLay->setSpacing(4);

    for (const bain::Package &p : m_packages) {
        auto *box = new QCheckBox(p.name, inner);
        box->setChecked(havePrior ? m_priorChoices.contains(p.name) : true);
        innerLay->addWidget(box);
        m_boxes.append(box);
    }
    innerLay->addStretch();
    m_scroll->setWidget(inner);
    main->addWidget(m_scroll, 1);

    m_btns = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    connect(m_btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    if (havePrior) {
        // Re-install: skip the chooser, show the pre-ticked list.
        auto *ok = m_btns->addButton(T("bain_install"), QDialogButtonBox::AcceptRole);
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        main->addWidget(m_btns);
        return;
    }

    // First install: compact chooser. "Install everything" is the one-click
    // path; "Choose packages..." reveals the list (hidden, with the Install
    // button, until then).
    m_scroll->hide();

    m_chooser = new QWidget(this);
    auto *chooserLay = new QVBoxLayout(m_chooser);
    chooserLay->setContentsMargins(0, 0, 0, 0);
    auto *allBtn = new QPushButton(T("bain_install_all"), m_chooser);
    auto *chooseBtn = new QPushButton(T("bain_choose"), m_chooser);
    allBtn->setDefault(true);
    chooserLay->addWidget(allBtn);
    chooserLay->addWidget(chooseBtn);
    main->addWidget(m_chooser);

    connect(allBtn, &QPushButton::clicked, this, [this]() {
        for (QCheckBox *b : m_boxes) b->setChecked(true);
        accept();
    });
    connect(chooseBtn, &QPushButton::clicked, this, &BainWizard::revealPicker);

    main->addWidget(m_btns);
}

void BainWizard::revealPicker()
{
    if (m_chooser) m_chooser->hide();
    if (m_scroll)  m_scroll->show();
    // User is pruning now: add Install alongside Cancel.
    auto *ok = m_btns->addButton(T("bain_install"), QDialogButtonBox::AcceptRole);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
}

QStringList BainWizard::chosenNames() const
{
    QStringList out;
    for (int i = 0; i < m_packages.size() && i < m_boxes.size(); ++i)
        if (m_boxes[i]->isChecked())
            out.append(m_packages[i].name);
    return out;
}
