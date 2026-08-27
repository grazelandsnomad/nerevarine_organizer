#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QList>

#include <functional>

#include <QSet>

#include "bain.h"
#include "bain_hint.h"

// BainWizard - small package picker for BAIN-style archives (see bain.h).
// Much thinner than FomodWizard: BAIN has no XML, steps or conditions, just a
// checkbox per numbered package.
//
// First install: the usual answer (and every false-positive "install everything"
// mod like Tamriel Rebuilt) is all packages, so the dialog opens on a compact
// "Install everything" vs "Choose packages..." chooser; the checkbox list only
// appears for users who want to prune. Re-install/update: a remembered selection
// (ModRole::BainChoices) skips the chooser and opens the list pre-ticked to last
// time. On accept the chosen contents merge into a staging dir via bain::stage,
// which the caller promotes like a FOMOD result.

class QCheckBox;
class QScrollArea;
class QDialogButtonBox;

class BainWizard : public QDialog {
    Q_OBJECT
public:
    // Non-modal, like FomodWizard::showAsync: shows the picker as an independent
    // window and calls onDone(stagedPath, choices) when finished.
    //   stagedPath    empty -> user cancelled (caller resets the install)
    //                 else  -> bain::stage output dir, ready to promote()
    //   choices       ";"-joined chosen package names; caller persists them
    //                 (ModRole::BainChoices) so a re-install pre-ticks them.
    //   priorChoices  ";"-joined names from a previous install;
    //                 empty -> first install (offer "Install everything").
    //   installedModNames        what the user currently HAS, so a package
    //                            patching a mod that is not there can start
    //                            unticked and say so (see bain_hint.h)
    //   ownModName               the mod being installed, so a package naming
    //                            its own mod is not read as naming another
    //   availablePluginsLower    every plugin filename the modlist provides,
    //                            lower-cased. EMPTY or COMPLETE, never partial:
    //                            a half-filled set makes a satisfied master
    //                            look missing, which unticks something wanted.
    static void showAsync(
        const QString &modPath,
        const QString &priorChoices,
        QWidget *parent,
        const QStringList &installedModNames,
        std::function<void(const QString &stagedPath,
                           const QString &choices)> onDone,
        const QString &ownModName = {},
        const QSet<QString> &availablePluginsLower = {});

private:
    explicit BainWizard(const QString &modPath, const QString &priorChoices,
                        QWidget *parent = nullptr);

    void buildUi();
    void revealPicker();        // swap the compact chooser for the checkbox list
    // Adds Install beside Cancel and keeps it disabled while nothing is
    // ticked: an empty selection makes bain::stage() return "", which the
    // caller reads as a cancel and acts on by deleting the archive.
    void addInstallButton();
    QStringList chosenNames() const;

    QString                 m_modPath;
    QStringList             m_priorChoices;   // parsed prior selection (empty = none)
    QList<bain::Package>    m_packages;
    QStringList             m_installedModNames;
    QString                 m_ownModName;
    QSet<QString>           m_availablePluginsLower;
    QList<bain::PackageVerdict> m_verdicts;   // parallel to m_packages
    QList<QCheckBox *>      m_boxes;          // parallel to m_packages
    QWidget                *m_chooser = nullptr;  // compact "everything / choose" row
    QScrollArea            *m_scroll  = nullptr;  // checkbox list (hidden until revealed)
    QDialogButtonBox       *m_btns    = nullptr;  // Cancel always; Install once expanded

    friend struct BainWizardTestHook;
};
