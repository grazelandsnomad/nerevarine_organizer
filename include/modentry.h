#ifndef MODENTRY_H
#define MODENTRY_H

// Value type for one mod-list row. Same payload as the QListWidgetItem in
// MainWindow::m_modList, split across Qt::UserRole slots (ModRole in
// modroles.h). fromItem/applyToItem cross the widget boundary; prefer
// ModEntry in logic so it stays QtCore-only.

#include <QColor>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QUuid>

class QListWidgetItem;

struct ModEntry {
    // "mod" or "separator". Default "mod" so a blank ModEntry isn't a separator.
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
    // Set only when repairEmptyModPaths rebinds a mod whose modPath is
    // missing on this machine (cross-machine modlist sync).
    QString intendedModPath;
    qint64  modSize = 0;               // bytes; 0 == unknown

    // User metadata.
    QString   customName;              // overrides displayName when non-empty
    QString   annotation;
    QDateTime dateAdded;
    bool      isUtility  = false;
    bool      isFavorite = false;
    QString   fomodChoices;            // serialized "si:gi:pi;..."
    QString   bainChoices;             // ";"-joined BAIN package names chosen at install

    // Install / download.
    int     installStatus    = 0;      // 0 not installed, 1 installed, 2 installing
    int     downloadProgress = 0;      // 0-100 while downloading, -1 while extracting
    bool    updateAvailable  = false;
    QString expectedMd5;               // lower-case hex, cleared after verify
    qint64  expectedSize     = 0;      // bytes, cleared after verify
    // Stable per-install id (ModRole::InstallToken). Only meaningful while
    // installStatus == 2; serialized so an interrupted install can be matched
    // back up after relaunch. Null QUuid otherwise.
    QUuid   installToken;

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

    // External URLs. videoUrl is a YouTube review; sourceUrl is a non-Nexus
    // download page (GitHub release, generic hosting). Persisted in the modlist.
    QString videoUrl;
    QString sourceUrl;
    // Set when handleNxmUrl reuses an existing INSTALLED row as the placeholder
    // for a re-install/update; holds the old ModPath so addModFromPath can drop
    // the stale folder once the new install lands. Transient, not persisted.
    QString prevModPath;
    // Set on "Merge into existing"; the existing mod folder the new files are
    // overlaid onto. Consumed by applyPendingMerge, transient, not persisted.
    QString mergeTargetPath;

    [[nodiscard]] static ModEntry fromItem(const QListWidgetItem *item);
    void applyToItem(QListWidgetItem *item) const;

    [[nodiscard]] bool    isSeparator()   const { return itemType == QStringLiteral("separator"); }
    [[nodiscard]] bool    isMod()         const { return itemType == QStringLiteral("mod"); }
    [[nodiscard]] QString effectiveName() const { return customName.isEmpty() ? displayName : customName; }

    friend bool operator==(const ModEntry &, const ModEntry &) = default;
};

// Sort predicates. Separators and missing/zero keys always sort last.

// Case-insensitive ascending on effectiveName().
bool lessByDisplayName(const ModEntry &a, const ModEntry &b);

// Ascending by dateAdded; invalid dates last.
bool lessByDateAdded(const ModEntry &a, const ModEntry &b);

// Ascending by modSize; zero/unknown last.
bool lessByModSize(const ModEntry &a, const ModEntry &b);

#endif // MODENTRY_H
