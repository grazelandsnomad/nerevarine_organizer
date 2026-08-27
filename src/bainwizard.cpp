#include "bainwizard.h"
#include "bain_hint.h"
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
    const QStringList &installedModNames,
    std::function<void(const QString &, const QString &)> onDone,
    const QString &ownModName,
    const QSet<QString> &availablePluginsLower)
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

    // Modlist context, set before buildUi() because the verdicts decide the
    // ticks the boxes are born with.
    dlg->m_installedModNames     = installedModNames;
    dlg->m_ownModName            = ownModName;
    dlg->m_availablePluginsLower = availablePluginsLower;

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
    using State  = bain::PackageVerdict::State;
    using Source = bain::PackageVerdict::Source;

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

    // What each package is for, and whether the user has it. Judged before the
    // boxes exist, because the verdict is what decides the tick.
    m_verdicts = bain::judgePackages(m_packages, m_installedModNames,
                                     m_availablePluginsLower, m_ownModName);
    const int recommendedOff = int(std::count_if(
        m_verdicts.cbegin(), m_verdicts.cend(),
        [](const bain::PackageVerdict &v) { return v.state == State::Missing; }));

    if (recommendedOff > 0) {
        auto *summary = new QLabel(
            T("bain_hygiene_summary").arg(recommendedOff), this);
        summary->setWordWrap(true);
        main->addWidget(summary);
    }

    // Checkbox list: the modlist verdict first, then the remembered selection
    // on re-install, else all-on so "Choose packages..." starts from
    // everything and the user prunes.
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    auto *inner = new QWidget;
    auto *innerLay = new QVBoxLayout(inner);
    innerLay->setSpacing(4);

    for (int i = 0; i < m_packages.size(); ++i) {
        const bain::Package &p = m_packages[i];
        auto *box = new QCheckBox(p.name, inner);
        const bain::PackageVerdict v = m_verdicts.value(i);

        // A stored choice does not put back a tick for a mod that is not
        // there: that choice was made against a modlist which is gone, and
        // the label beside it would contradict the tick.
        //
        // The reverse is NOT forced. An Installed verdict leaves a stored
        // untick alone - unlike the FOMOD wizard, whose positive comes from a
        // stated requirement, this one is an inference off a folder name, and
        // a prior untick is a deliberate prune the user made. So the positive
        // badge only ever states a fact, and claims nothing about the tick.
        if (v.state == State::Missing)  box->setChecked(false);
        else if (havePrior)             box->setChecked(m_priorChoices.contains(p.name));
        else                            box->setChecked(true);

        // Hardcoded English, like every annotation in fomodwizard.cpp - only
        // the dialog chrome goes through T().
        //
        // Never setEnabled(false): a mod kept outside the manager is absent
        // from the modlist and only the user knows that.
        if (v.state == State::Missing && v.source == Source::Master) {
            box->setText(box->text()
                + QStringLiteral(" ⚠️ needs %1, which nothing here provides")
                      .arg(v.master));
            box->setToolTip(QStringLiteral(
                "This package's plugin asks for %1 as a parent file. Nothing in "
                "this modlist provides it, so OpenMW would refuse to load it. "
                "Tick it back if you have that mod outside the manager, or are "
                "about to add it.").arg(v.master));
        } else if (v.state == State::Missing) {
            box->setText(box->text()
                + QStringLiteral(" ⚠️ %1 is not installed in this modlist")
                      .arg(v.target));
            // The same finding as the FOMOD wizard's, in the same words.
            box->setToolTip(QStringLiteral(
                "Unticked because that mod is not in this modlist, so these "
                "files would be installed for nothing. Tick it back if you have "
                "the mod outside the manager, or are about to add it."));
        } else if (v.state == State::Installed) {
            // Name the mod the PACKAGE is for, not the row that answered for
            // it. Those differ when the match came through an alias: "02 OAAB
            // Shipwrecks Patch" is answered by whichever OAAB mod the modlist
            // lists first, and a badge reading "OAAB Juniper's Twin Lamps" on
            // a Shipwrecks patch is a worse answer than no badge. The row that
            // matched goes in the tooltip, where it explains rather than
            // claims.
            box->setText(box->text()
                + QStringLiteral(" ✅ %1 ✓").arg(v.target));
            box->setToolTip(v.matched.compare(v.target, Qt::CaseInsensitive) == 0
                ? QStringLiteral("%1 is installed, so this package will work.")
                      .arg(v.target)
                : QStringLiteral("This package is for %1, which this modlist "
                                 "has as \"%2\". It will work.")
                      .arg(v.target, v.matched));
        }

        innerLay->addWidget(box);
        m_boxes.append(box);
    }
    innerLay->addStretch();
    m_scroll->setWidget(inner);
    main->addWidget(m_scroll, 1);

    m_btns = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    connect(m_btns, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Re-install, or a first install with something to recommend: skip the
    // chooser and show the pre-ticked list.
    //
    // A recommendation you cannot see is not one. "Install everything" force-
    // ticks every box, so leaving the compact chooser up would erase the
    // unticks under badges nobody had a chance to read - and the count alone
    // does not say which packages were dropped. It costs one extra click on
    // exactly the archives where one click was the wrong answer; archives with
    // nothing to flag are untouched.
    if (havePrior || recommendedOff > 0) {
        addInstallButton();
        main->addWidget(m_btns);
        return;
    }

    // First install with nothing to flag: compact chooser. "Install
    // everything" is the one-click path; "Choose packages..." reveals the list
    // (hidden, with the Install button, until then).
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

    // Safe to force every tick here: this branch is only reachable when the
    // hygiene pass unticked nothing.
    connect(allBtn, &QPushButton::clicked, this, [this]() {
        for (QCheckBox *b : m_boxes) b->setChecked(true);
        accept();
    });
    connect(chooseBtn, &QPushButton::clicked, this, &BainWizard::revealPicker);

    main->addWidget(m_btns);
}

void BainWizard::addInstallButton()
{
    if (!m_btns) return;
    auto *ok = m_btns->addButton(T("bain_install"), QDialogButtonBox::AcceptRole);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);

    // Nothing ticked means stage() hands the caller "", which it reads as a
    // cancel and acts on by deleting the archive. Better to not offer it.
    auto sync = [this, ok]() {
        ok->setEnabled(std::any_of(m_boxes.cbegin(), m_boxes.cend(),
                                   [](QCheckBox *b) { return b->isChecked(); }));
    };
    for (QCheckBox *b : m_boxes) connect(b, &QCheckBox::toggled, this, sync);
    sync();
}

void BainWizard::revealPicker()
{
    if (m_chooser) m_chooser->hide();
    if (m_scroll)  m_scroll->show();
    // User is pruning now: add Install alongside Cancel.
    addInstallButton();
}

QStringList BainWizard::chosenNames() const
{
    QStringList out;
    for (int i = 0; i < m_packages.size() && i < m_boxes.size(); ++i)
        if (m_boxes[i]->isChecked())
            out.append(m_packages[i].name);
    return out;
}
