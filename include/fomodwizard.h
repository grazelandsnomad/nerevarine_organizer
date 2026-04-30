#pragma once

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

#include <expected>
#include <functional>

// Lightweight FOMOD installer (ModuleConfig.xml reader + wizard UI).
// Handles required install files and optional step/group/plugin selection.

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
    // Flags the plugin raises when selected.  Many FOMODs use these in
    // combination with `conditionalFileInstalls` below instead of attaching
    // files directly to a plugin, so we must honour them during install or
    // the whole mod ends up empty on disk (real case: EKM Corkbulb Retexture).
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
// When the `<dependencies>` block's flag requirements are satisfied by the
// user's selections in the install steps, the pattern's files/folders get
// installed on top of whatever the plugins themselves provided.  Operator
// is "And" (all must match) or "Or" (any match is enough); default "And".
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
    // On success: the path to the installed mod directory.
    // On failure: a short machine-readable reason ("cancelled", "parse-error").
    // On XML parse failure, shows a notice and falls back to archiveRoot if the
    // user accepts the fallback - otherwise returns the "cancelled" error.
    //
    // priorChoices: serialized choices from a previous install ("si:gi:pi;...").
    //   When non-empty, the wizard pre-selects those plugins as defaults instead
    //   of the FOMOD-declared Recommended/Required defaults.
    // outChoices: if non-null, receives the serialized choices the user made
    //   (same "si:gi:pi;..." format) so they can be stored in the modlist.
    static std::expected<QString, QString>
    run(const QString &archiveRoot,
        const QString &priorChoices = {},
        QString *outChoices = nullptr,
        QWidget *parent = nullptr,
        const QStringList &installedModNames = {});

    // Non-modal variant: shows the wizard as an independent window and calls
    // onDone(fomodPath, choices) when the user finishes or cancels.
    // fomodPath is empty on cancellation.  The dialog deletes itself on close.
    static void showAsync(
        const QString &archiveRoot,
        const QString &priorChoices,
        QWidget *parent,
        const QStringList &installedModNames,
        std::function<void(const QString &fomodPath,
                           const QString &choices)> onDone);

    // Returns true when archiveRoot contains fomod/ModuleConfig.xml
    // (case-insensitive directory and filename match).
    static bool hasFomod(const QString &archiveRoot);

private:
    explicit FomodWizard(const QString &archiveRoot, QWidget *parent = nullptr);

    bool    parse();
    void    buildUi();
    void    updateButtons();
    QString applySelections();
    // Serializes the current button state as "si:gi:pi;..." where each
    // triplet identifies a checked plugin by step/group/plugin index.
    QString collectChoices() const;

    // Returns path to ModuleConfig.xml, or empty string if not found.
    static QString findModuleConfig(const QString &archiveRoot);

    // Copy every entry inside srcDir into dstDir (not the dir itself, only its contents).
    static void copyContents(const QString &srcDir, const QString &dstDir);
    // Copy srcDir as a named subdirectory under dstDir.
    static void copyDir(const QString &srcDir, const QString &dstDir);

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
