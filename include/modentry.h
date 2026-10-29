#ifndef MODENTRY_H
#define MODENTRY_H

// Value type for one mod-list row: what ModlistModel holds, what the
// modlist file serializes, what the view paints through the role table.
// QtCore (plus QColor) only, so logic over rows links without widgets.

#include "modroles.h"

#include <QColor>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariant>

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
    qint64  nexusFileId = 0;           // the installed FILE; 0 == unknown (ModRole::NexusFileId)

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
    // Built by the translate editor rather than downloaded. See
    // ModRole::IsGeneratedTranslation.
    bool      isGeneratedTranslation = false;
    QString   fomodChoices;            // serialized "si:gi:pi;..."
    // Mod path this entry translates, set by hand from the context menu.
    // Empty means "let the scan decide".
    QString   translationOf;
    QString   bainChoices;             // ";"-joined BAIN package names chosen at install

    // Install / download.
    int     installStatus    = 0;      // 0 not installed, 1 installed, 2 installing
    int     downloadProgress = 0;      // 0-100 while downloading, -1 while extracting
    bool    updateAvailable  = false;
    QDateTime confirmedCurrentAt;      // ModRole::ConfirmedCurrentAt; invalid == never
    QString expectedMd5;               // lower-case hex, cleared after verify
    qint64  expectedSize     = 0;      // bytes, cleared after verify
    qint64  expectedFileId   = 0;      // the Nexus file the two above describe
    // Stable per-install id (ModRole::InstallToken). Only meaningful while
    // installStatus == 2; serialized so an interrupted install can be matched
    // back up after relaunch. Null QUuid otherwise.
    QUuid   installToken;
    // The row's session identity (ModRole::RowId, mod_row::rowId): carried so
    // an undo that rebuilds every item keeps the rows a reply in flight was
    // addressed to. Null until something asked for one. Never serialized.
    QUuid   rowId;

    // Dependencies.
    QStringList dependsOn;             // list of Nexus URLs
    int         highlightRole        = 0;   // 0 none, 1 dep-of-selected, 2 uses-selected
    bool        hasInListDependency  = false;
    bool        hasMissingDependency = false;
    QStringList missingDependencies;

    // Conflicts / plugins. The two conflict lists are directed: this mod
    // shadows everything in conflictOverwrites and is shadowed by everything in
    // conflictOverwrittenBy. Both hold "DisplayName\tfile1\tfile2\t..." entries.
    bool        hasConflict      = false;
    QStringList conflictOverwrites;
    QStringList conflictOverwrittenBy;
    QStringList conflictSameRecords;
    int         conflictHighlight = 0;  // 0 none, 1 loses to selected, 2 beats selected
    bool        hasMissingMaster = false;
    // Tab-delimited per plugin: "plugin.esp\tmaster1.esm\tmaster2.esm".
    QStringList missingMasters;

    // External URLs. videoUrl is a YouTube review; sourceUrl is a non-Nexus
    // download page (GitHub release, generic hosting). Persisted in the modlist.
    QString videoUrl;
    QString sourceUrl;
    // Bethesda.net Creation adopted from the game's downloads: the catalog's
    // Version at adoption, so a re-download (new version) is noticed. Empty
    // for every other row. Persisted in the modlist.
    QString creationVersion;
    // Set when handleNxmUrl reuses an existing INSTALLED row as the placeholder
    // for a re-install/update; holds the old ModPath so addModFromPath can drop
    // the stale folder once the new install lands. Transient, not persisted.
    QString prevModPath;
    // Set on "Merge into existing"; the existing mod folder the new files are
    // overlaid onto. Consumed by MainWindow::prepareMerge; transient, not
    // persisted.
    QString mergeTargetPath;

    // --- Session-only state (never serialized) -------------------------
    // What the window used to keep on the QListWidgetItem in roles the entry
    // did not carry. Same meaning as the ModRole comments in modroles.h.
    QString     pendingArchive;            // ModRole::PendingArchive
    QString     translationPartner;        // ModRole::TranslationPartner
    bool        isTranslationOfOther = false;
    QString     fileSuperseded;            // ModRole::FileSuperseded
    qint64      pendingFileId = 0;         // ModRole::PendingFileId
    bool        awaitingUpdateDownload = false;
    bool        updateFoundThisSession = false; // ModRole::UpdateFoundThisSession
    bool        sepHasUpdate = false;      // separator only
    QString     nexusFileName;             // ModRole::NexusFileName
    qint64      sortAnchor = -1;           // ModRole::SortAnchor; -1 = never stamped
    int         translationState = 0;      // ModRole::TranslationState
    QStringList translationDetail;         // ModRole::TranslationDetail
    int         translationInProgress = 0; // ModRole::TranslationInProgress
    bool        runtimeMismatchWarned = false;
    bool        scriptExtenderStale = false;
    QString     scriptExtenderDetail;

    // --- Presentation the list used to keep on the item -----------------
    // QListWidgetItem's default flags minus ItemIsDropEnabled: a drag lands
    // between rows, never on one, so the view's move path is the only one.
    static constexpr Qt::ItemFlags kDefaultItemFlags =
        Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled
        | Qt::ItemIsDragEnabled;
    QString       toolTip;
    Qt::ItemFlags itemFlags = kDefaultItemFlags;
    bool          hidden = false;          // filtered out, or under a collapsed separator

    // --- The role table -------------------------------------------------
    // The one mapping between ModRole / Qt item roles and fields; the list
    // model answers data() and setData() through it. roleData() of a role the
    // table does not know is an invalid QVariant, and setRoleData() of one
    // returns false and changes nothing. A null QVariant resets the field to
    // its default - the way a cleared QListWidgetItem role read back as empty,
    // 0 or false. A separator has no check state (an invalid QVariant), so no
    // check box is painted on a section header.
    [[nodiscard]] QVariant roleData(int role) const;
    bool setRoleData(int role, const QVariant &value);
    // Every role the table maps, for tests and whole-row copies.
    [[nodiscard]] static const QList<int> &roles();

    // --- The item-shaped spelling ----------------------------------------
    // The role table under QListWidgetItem's names, so a row is built before
    // insertion the way the window always built one (through an arrow), and
    // the placeholder-state helpers run on an entry or on a live row alike.
    [[nodiscard]] static ModEntry named(const QString &displayName);
    [[nodiscard]] QVariant data(int role) const { return roleData(role); }
    void setData(int role, const QVariant &value) { setRoleData(role, value); }
    [[nodiscard]] QString text() const { return displayName; }
    void setText(const QString &t) { displayName = t; }
    [[nodiscard]] Qt::CheckState checkState() const { return checked ? Qt::Checked : Qt::Unchecked; }
    void setCheckState(Qt::CheckState s) { checked = (s == Qt::Checked); }
    [[nodiscard]] Qt::ItemFlags flags() const { return itemFlags; }
    void setFlags(Qt::ItemFlags f) { itemFlags = f; }
    void setToolTip(const QString &t) { toolTip = t; }
    ModEntry       *operator->() { return this; }
    const ModEntry *operator->() const { return this; }

    [[nodiscard]] bool    isSeparator()   const { return itemType == QStringLiteral("separator"); }
    [[nodiscard]] bool    isMod()         const { return itemType == QStringLiteral("mod"); }
    // installStatus stays an int for the serializer; these read it the way
    // mod_row::isInstalled / isInstalling read the item role.
    [[nodiscard]] bool    isInstalled()   const { return installStatus == int(InstallStatus::Installed); }
    [[nodiscard]] bool    isInstalling()  const { return installStatus == int(InstallStatus::Installing); }
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

// Row indices in canonical (saved) order given each row's SortAnchor stamp.
// `anchors[i]` is row i's stamped saved position, or < 0 if it was never
// stamped (e.g. a mod added while a temporary view sort was active). Returns a
// stable permutation: stamped rows by ascending anchor, unstamped rows last in
// their original relative order. Pure - lets the "view sort" persistence walks
// reproduce the saved order without touching any widget.
[[nodiscard]] QList<int> canonicalOrderFromAnchors(const QList<qint64> &anchors);

#endif // MODENTRY_H
