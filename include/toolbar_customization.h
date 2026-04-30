#ifndef TOOLBAR_CUSTOMIZATION_H
#define TOOLBAR_CUSTOMIZATION_H

#include <QHash>
#include <QObject>
#include <QString>

class QAction;
class QWidget;

// Tracks user-customizable toolbar entries. Visibility for each registered
// QAction is the AND of two gates carried as dynamic properties on the
// action itself:
//   · "nerev_profile_visible" - set by callers who gate by game profile
//     (e.g. Sort with LOOT only on Morrowind/Skyrim/Fallout).
//   · "nerev_user_visible"    - toggled by the Customize Toolbar dialog,
//     persisted under the QSettings key "toolbar/<id>".
class ToolbarCustomization : public QObject {
    Q_OBJECT
public:
    explicit ToolbarCustomization(QObject *parent = nullptr);

    // Track an action as customizable. Seeds the two gate properties if they
    // haven't been set, reads the user preference (defaulting to defaultVisible
    // for first-run) and applies the combined visibility immediately.
    void registerAction(const QString &id, QAction *act,
                        const QString &label, bool defaultVisible = true);

    // Apply (profile_visible AND user_visible) to act->setVisible().
    void applyVisibility(QAction *act);

    // Apply the combined gate to every registered action. Safe after
    // QAction::setProperty changes done outside of registerAction.
    void applyAll();

    // Modal dialog with a checkbox per registered entry. On OK, persists the
    // toolbar/<id> keys and applies the new visibility.
    void showCustomizeDialog(QWidget *parent);

private:
    QHash<QString, QAction*> m_actions;
    QHash<QString, QString>  m_labels;
};

#endif // TOOLBAR_CUSTOMIZATION_H
