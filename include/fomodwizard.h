#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

#include <expected>
#include <functional>

// FOMOD installer: ModuleConfig.xml reader + wizard UI.
// Required files plus optional step/group/plugin selection.

struct FomodFile {
    QString source;       // path relative to archive root
    QString destination;  // path relative to mod root (empty = same as source filename)
    int     priority = 0;
};

struct FomodFlagValue {
    QString name;    // flag name (e.g. "Default")
    QString value;   // flag value when this plugin is picked (usually "Active")
};

struct FomodPlugin {
    QString name;
    QString description;
    QString type;   // "Required" | "Recommended" | "Optional" | "NotUsable" | "CouldBeUsable"
    QList<FomodFile> files;    // individual files to install
    QList<FomodFile> folders;  // directories to install (contents copied)
    // Flags the plugin raises when selected. Many FOMODs use these with
    // conditionalFileInstalls below instead of attaching files to a plugin;
    // honour them at install or the mod ends up empty (EKM Corkbulb Retexture).
    QList<FomodFlagValue> conditionFlags;
};

struct FomodGroup {
    QString name;
    // "SelectExactlyOne" | "SelectAtMostOne" | "SelectAtLeastOne" | "SelectAny" | "SelectAll"
    QString type;
    QList<FomodPlugin> plugins;
};

struct FomodStep {
    QString name;
    QList<FomodGroup> groups;
};

// Module-level <conditionalFileInstalls>/<patterns>/<pattern>.
// When the <dependencies> flag requirements are met by the user's step
// selections, the pattern's files/folders install on top of what the plugins
// provided. Operator "And" (all match) or "Or" (any match); default "And".
struct FomodPattern {
    QString op = QStringLiteral("And");   // "And" | "Or"
    QList<FomodFlagValue> flagDeps;       // required (flag, value) pairs
    QList<FomodFile> files;
    QList<FomodFile> folders;
};

class QAbstractButton;
class QLabel;
class QPushButton;
class QStackedWidget;

class FomodWizard : public QDialog {
    Q_OBJECT
public:
    // Success: installed mod dir path. Failure: a short reason ("cancelled",
    // "parse-error"). On XML parse failure, offers a fallback to archiveRoot;
    // declining returns "cancelled".
    //
    // priorChoices: serialized choices from a previous install ("si:gi:pi;...");
    //   when non-empty, pre-selects those plugins instead of the FOMOD defaults.
    // outChoices: if non-null, receives the user's choices in the same format
    //   for storage in the modlist.
    static std::expected<QString, QString>
    run(const QString &archiveRoot,
        const QString &priorChoices = {},
        QString *outChoices = nullptr,
        QWidget *parent = nullptr,
        const QStringList &installedModNames = {});

    // Non-modal: shows as an independent window, calls onDone(fomodPath,
    // choices) on finish/cancel. fomodPath empty on cancel. Self-deletes on close.
    static void showAsync(
        const QString &archiveRoot,
        const QString &priorChoices,
        QWidget *parent,
        const QStringList &installedModNames,
        std::function<void(const QString &fomodPath,
                           const QString &choices)> onDone);

    // True when archiveRoot has fomod/ModuleConfig.xml (case-insensitive).
    static bool hasFomod(const QString &archiveRoot);

private:
    // Test hook: test_fomod_wizard_ui.cpp drives buildUi() with a hand-built
    // m_steps and inspects m_buttons plus the synthetic "None" radio that
    // SelectAtMostOne groups grow.
    friend struct FomodWizardTestHook;

    explicit FomodWizard(const QString &archiveRoot, QWidget *parent = nullptr);

    bool    parse();
    void    buildUi();
    void    updateButtons();
    QString applySelections();
    // Serializes button state as "si:gi:pi;..." (step/group/plugin index per
    // checked plugin).
    QString collectChoices() const;

    // Returns path to ModuleConfig.xml, or empty string if not found.
    static QString findModuleConfig(const QString &archiveRoot);

    // Shallowest dir at/under archiveRoot that directly holds
    // fomod/ModuleConfig.xml (handles Nexus wrapper folders the post-extraction
    // dive heuristic doesn't unwrap), or "" if no FOMOD anywhere in the tree.
    static QString findFomodRoot(const QString &archiveRoot);

    QString              m_archiveRoot;
    QString              m_modName;
    QString              m_priorChoices;      // serialized prior choices, set before buildUi()
    QStringList          m_installedModNames; // display names of all mods in the modlist
    QList<FomodFile>     m_requiredFiles;
    QList<FomodFile>     m_requiredFolders;
    QList<FomodStep>     m_steps;
    QList<FomodPattern>  m_conditionalInstalls;

    // UI
    QLabel            *m_titleLbl  = nullptr;
    QStackedWidget    *m_stack     = nullptr;
    QPushButton       *m_prevBtn   = nullptr;
    QPushButton       *m_nextBtn   = nullptr;
    QPushButton       *m_installBtn = nullptr;
    int                m_curPage   = 0;

    // m_buttons[stepIdx][groupIdx][pluginIdx]
    QList<QList<QList<QAbstractButton *>>> m_buttons;
};
