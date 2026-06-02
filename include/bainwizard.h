#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QList>

#include <functional>

#include "bain.h"

// BainWizard - a small package picker for BAIN-style archives (see bain.h).
//
// Deliberately much thinner than FomodWizard: BAIN has no XML, no steps, no
// conditions - just a checkbox per numbered package, ALL checked by default
// (so a false-positive detection costs the user one click, never a wrong
// install). The user unticks packages they don't want; on accept the chosen
// package contents are merged into a staging dir via bain::stage, and the
// caller promotes that exactly like a FOMOD result.

class QCheckBox;

class BainWizard : public QDialog {
    Q_OBJECT
public:
    // Non-modal, mirrors FomodWizard::showAsync: shows the picker as an
    // independent window and calls onDone(stagedPath, choices) when finished.
    //   stagedPath  empty  -> the user cancelled (caller resets the install)
    //               else   -> bain::stage output dir, ready to promote()
    //   choices     ";"-joined chosen package names (for future re-install
    //               persistence; unused by the minimal first cut)
    // The dialog deletes itself on close.
    static void showAsync(
        const QString &modPath,
        QWidget *parent,
        std::function<void(const QString &stagedPath,
                           const QString &choices)> onDone);

private:
    explicit BainWizard(const QString &modPath, QWidget *parent = nullptr);

    void buildUi();
    QStringList chosenNames() const;

    QString                 m_modPath;
    QList<bain::Package>    m_packages;
    QList<QCheckBox *>      m_boxes;     // parallel to m_packages

    friend struct BainWizardTestHook;
};
