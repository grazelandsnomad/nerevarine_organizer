#ifndef MODENTRY_H
#define MODENTRY_H

// Pure value type that mirrors the per-row data stored in the main mod-list
// widget. Every QListWidgetItem in MainWindow::m_modList carries the same
// payload split across Qt::UserRole slots (see ModRole in modroles.h).
// fromItem / applyToItem cross the widget boundary; business logic should
// prefer ModEntry so it can compile and test against QtCore alone.

#include <QColor>
#include <QDateTime>
#include <QString>
#include <QStringList>

class QListWidgetItem;

struct ModEntry {
    // "mod" or "separator". Default "mod" so a default-constructed ModEntry
    // reads as a plausible row rather than an implicit separator.
    QString itemType = QStringLiteral("mod");
    QString displayName;
    bool    checked  = false;

    // Row-level overrides; invalid QColor means "fall back to palette".
    QColor  bgColor;
    QColor  fgColor;

    // Separator-only.
    bool collapsed   = false;
    int  activeCount = 0;
    int  totalCount  = 0;

    // Nexus identity.
    int     nexusId = 0;               // 0 == unknown / not from Nexus
    QString nexusUrl;
    QString nexusTitle;

    // Filesystem.
    QString modPath;
    // Set only when repairEmptyModPaths rebinds a mod whose canonical
    // modPath is missing on this machine (cross-machine modlist sync).
    QString intendedModPath;
    qint64  modSize = 0;               // bytes; 0 == unknown

    // User metadata.
    QString   customName;              // overrides displayName when non-empty
    QString   annotation;
    QDateTime dateAdded;
    bool      isUtility  = false;
    bool      isFavorite = false;
    QString   fomodChoices;            // serialized "si:gi:pi;..."

    // Install / download.
    int     installStatus    = 0;      // 0 not installed, 1 installed, 2 installing
    int     downloadProgress = 0;      // 0-100 while downloading, -1 while extracting
    bool    updateAvailable  = false;
    QString expectedMd5;               // lower-case hex, cleared after verify
    qint64  expectedSize     = 0;      // bytes, cleared after verify

    // Dependencies.
    QStringList dependsOn;             // list of Nexus URLs
    int         highlightRole        = 0;   // 0 none, 1 dep-of-selected, 2 uses-selected
    bool        hasInListDependency  = false;
    bool        hasMissingDependency = false;
    QStringList missingDependencies;

    // Conflicts / plugins.
    bool        hasConflict      = false;
    QStringList conflictsWith;
    bool        hasMissingMaster = false;
    // Tab-delimited per plugin: "plugin.esp\tmaster1.esm\tmaster2.esm".
    QStringList missingMasters;

    [[nodiscard]] static ModEntry fromItem(const QListWidgetItem *item);
    void applyToItem(QListWidgetItem *item) const;

    [[nodiscard]] bool    isSeparator()   const { return itemType == QStringLiteral("separator"); }
    [[nodiscard]] bool    isMod()         const { return itemType == QStringLiteral("mod"); }
    [[nodiscard]] QString effectiveName() const { return customName.isEmpty() ? displayName : customName; }

    friend bool operator==(const ModEntry &, const ModEntry &) = default;
};

// Sort predicates. Separators sort to the end; missing / zero keys sort to
// the end; otherwise ascending by value.

// Case-insensitive ascending sort on effectiveName() (customName when set,
// otherwise displayName).  Separators go last.
bool lessByDisplayName(const ModEntry &a, const ModEntry &b);

// Ascending by dateAdded.  Invalid dates and separators go last.
bool lessByDateAdded(const ModEntry &a, const ModEntry &b);

// Ascending by modSize.  Zero / unknown sizes and separators go last.
bool lessByModSize(const ModEntry &a, const ModEntry &b);

#endif // MODENTRY_H
