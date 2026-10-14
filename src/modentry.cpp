// src/modentry.cpp
//
// The role table, the sort predicates and the other non-widget helpers for
// ModEntry. Kept free of <QListWidgetItem>: nothing here needs a widget.

#include "modentry.h"

#include <QHash>
#include <QMetaType>

#include <algorithm>
#include <limits>

namespace {

struct RoleField {
    int role;
    QVariant (*get)(const ModEntry &);
    void (*set)(ModEntry &, const QVariant &);
};

// One line per role. CONV is the QVariant accessor for the field's type; a
// null QVariant converts to that type's default, which is the field's default.
#define NRV_ROLE(ROLE, FIELD, CONV)                                            \
    RoleField{ROLE, [](const ModEntry &e) { return QVariant::fromValue(e.FIELD); }, \
              [](ModEntry &e, const QVariant &v) { e.FIELD = v.CONV(); }}
#define NRV_ROLE_T(ROLE, FIELD, TYPE)                                          \
    RoleField{ROLE, [](const ModEntry &e) { return QVariant::fromValue(e.FIELD); }, \
              [](ModEntry &e, const QVariant &v) { e.FIELD = v.value<TYPE>(); }}

const RoleField kRoleTable[] = {
    NRV_ROLE(Qt::DisplayRole, displayName, toString),
    NRV_ROLE(Qt::EditRole,    displayName, toString),
    NRV_ROLE(Qt::ToolTipRole, toolTip,     toString),
    RoleField{Qt::CheckStateRole,
              [](const ModEntry &e) {
                  return e.isSeparator() ? QVariant()
                                         : QVariant(int(e.checked ? Qt::Checked : Qt::Unchecked));
              },
              [](ModEntry &e, const QVariant &v) { e.checked = v.toInt() == int(Qt::Checked); }},

    // The default is a mod, not "no type": a null write goes back to it.
    RoleField{ModRole::ItemType,
              [](const ModEntry &e) { return QVariant(e.itemType); },
              [](ModEntry &e, const QVariant &v) {
                  e.itemType = v.isValid() ? v.toString() : QStringLiteral("mod");
              }},
    NRV_ROLE_T(ModRole::BgColor,   bgColor,     QColor),
    NRV_ROLE_T(ModRole::FgColor,   fgColor,     QColor),
    NRV_ROLE(ModRole::Collapsed,   collapsed,   toBool),
    NRV_ROLE(ModRole::ActiveCount, activeCount, toInt),
    NRV_ROLE(ModRole::TotalCount,  totalCount,  toInt),
    NRV_ROLE(ModRole::SepHasUpdate, sepHasUpdate, toBool),

    NRV_ROLE(ModRole::NexusId,     nexusId,     toInt),
    NRV_ROLE(ModRole::NexusUrl,    nexusUrl,    toString),
    NRV_ROLE(ModRole::NexusTitle,  nexusTitle,  toString),
    NRV_ROLE(ModRole::NexusFileId, nexusFileId, toLongLong),

    NRV_ROLE(ModRole::ModPath,         modPath,         toString),
    NRV_ROLE(ModRole::IntendedModPath, intendedModPath, toString),
    NRV_ROLE(ModRole::ModSize,         modSize,         toLongLong),

    NRV_ROLE(ModRole::CustomName,   customName,   toString),
    NRV_ROLE(ModRole::Annotation,   annotation,   toString),
    NRV_ROLE(ModRole::DateAdded,    dateAdded,    toDateTime),
    NRV_ROLE(ModRole::IsUtility,    isUtility,    toBool),
    NRV_ROLE(ModRole::IsFavorite,   isFavorite,   toBool),
    NRV_ROLE(ModRole::IsGeneratedTranslation, isGeneratedTranslation, toBool),
    NRV_ROLE(ModRole::FomodChoices,  fomodChoices,  toString),
    NRV_ROLE(ModRole::TranslationOf, translationOf, toString),
    NRV_ROLE(ModRole::BainChoices,   bainChoices,   toString),

    NRV_ROLE(ModRole::InstallStatus,    installStatus,    toInt),
    NRV_ROLE(ModRole::DownloadProgress, downloadProgress, toInt),
    NRV_ROLE(ModRole::UpdateAvailable,  updateAvailable,  toBool),
    NRV_ROLE(ModRole::ExpectedMd5,      expectedMd5,      toString),
    NRV_ROLE(ModRole::ExpectedSize,     expectedSize,     toLongLong),
    NRV_ROLE(ModRole::ExpectedFileId,   expectedFileId,   toLongLong),
    NRV_ROLE(ModRole::InstallToken,     installToken,     toUuid),
    NRV_ROLE(ModRole::RowId,            rowId,            toUuid),
    NRV_ROLE(ModRole::PendingArchive,   pendingArchive,   toString),
    NRV_ROLE(ModRole::PendingFileId,    pendingFileId,    toLongLong),
    NRV_ROLE(ModRole::AwaitingUpdateDownload, awaitingUpdateDownload, toBool),
    NRV_ROLE(ModRole::NexusFileName,    nexusFileName,    toString),
    NRV_ROLE(ModRole::RuntimeMismatchWarned, runtimeMismatchWarned, toBool),

    NRV_ROLE(ModRole::DependsOn,            dependsOn,            toStringList),
    NRV_ROLE(ModRole::HighlightRole,        highlightRole,        toInt),
    NRV_ROLE(ModRole::HasInListDependency,  hasInListDependency,  toBool),
    NRV_ROLE(ModRole::HasMissingDependency, hasMissingDependency, toBool),
    NRV_ROLE(ModRole::MissingDependencies,  missingDependencies,  toStringList),

    NRV_ROLE(ModRole::HasConflict,           hasConflict,           toBool),
    NRV_ROLE(ModRole::ConflictOverwrites,    conflictOverwrites,    toStringList),
    NRV_ROLE(ModRole::ConflictOverwrittenBy, conflictOverwrittenBy, toStringList),
    NRV_ROLE(ModRole::ConflictSameRecords,   conflictSameRecords,   toStringList),
    NRV_ROLE(ModRole::ConflictHighlight,     conflictHighlight,     toInt),
    NRV_ROLE(ModRole::HasMissingMaster,      hasMissingMaster,      toBool),
    NRV_ROLE(ModRole::MissingMasters,        missingMasters,        toStringList),

    NRV_ROLE(ModRole::VideoUrl,        videoUrl,        toString),
    NRV_ROLE(ModRole::SourceUrl,       sourceUrl,       toString),
    NRV_ROLE(ModRole::PrevModPath,     prevModPath,     toString),
    NRV_ROLE(ModRole::MergeTargetPath, mergeTargetPath, toString),

    NRV_ROLE(ModRole::TranslationPartner,    translationPartner,    toString),
    NRV_ROLE(ModRole::IsTranslationOfOther,  isTranslationOfOther,  toBool),
    NRV_ROLE(ModRole::TranslationState,      translationState,      toInt),
    NRV_ROLE(ModRole::TranslationDetail,     translationDetail,     toStringList),
    NRV_ROLE(ModRole::TranslationInProgress, translationInProgress, toInt),
    NRV_ROLE(ModRole::FileSuperseded,        fileSuperseded,        toString),
    NRV_ROLE(ModRole::ScriptExtenderStale,   scriptExtenderStale,   toBool),
    NRV_ROLE(ModRole::ScriptExtenderDetail,  scriptExtenderDetail,  toString),

    // Tri-state: the view sort asks whether a row was stamped at all, so an
    // unstamped row reads as no value, and a null write un-stamps it.
    RoleField{ModRole::SortAnchor,
              [](const ModEntry &e) {
                  return e.sortAnchor < 0 ? QVariant() : QVariant::fromValue(e.sortAnchor);
              },
              [](ModEntry &e, const QVariant &v) {
                  e.sortAnchor = v.isValid() ? v.toLongLong() : -1;
              }},
};

#undef NRV_ROLE
#undef NRV_ROLE_T

const QHash<int, const RoleField *> &roleIndex()
{
    static const QHash<int, const RoleField *> index = [] {
        QHash<int, const RoleField *> h;
        for (const RoleField &f : kRoleTable) h.insert(f.role, &f);
        return h;
    }();
    return index;
}

} // namespace

QVariant ModEntry::roleData(int role) const
{
    const RoleField *f = roleIndex().value(role, nullptr);
    return f ? f->get(*this) : QVariant();
}

bool ModEntry::setRoleData(int role, const QVariant &value)
{
    const RoleField *f = roleIndex().value(role, nullptr);
    if (!f) return false;
    f->set(*this, value);
    return true;
}

const QList<int> &ModEntry::roles()
{
    static const QList<int> all = [] {
        QList<int> r;
        for (const RoleField &f : kRoleTable) r.append(f.role);
        return r;
    }();
    return all;
}

bool lessByDisplayName(const ModEntry &a, const ModEntry &b)
{
    // Separators always trail mods.  Relative order among separators is
    // preserved by stable_sort; for plain std::sort the pair is treated
    // as equivalent.
    if (a.isSeparator() && b.isSeparator()) return false;
    if (a.isSeparator()) return false;
    if (b.isSeparator()) return true;
    return a.effectiveName().compare(b.effectiveName(), Qt::CaseInsensitive) < 0;
}

bool lessByDateAdded(const ModEntry &a, const ModEntry &b)
{
    if (a.isSeparator() && b.isSeparator()) return false;
    if (a.isSeparator()) return false;
    if (b.isSeparator()) return true;

    const bool av = a.dateAdded.isValid();
    const bool bv = b.dateAdded.isValid();
    // Mods with no recorded date fall to the end in either direction.
    if (!av && !bv) return false;
    if (!av) return false;
    if (!bv) return true;
    return a.dateAdded < b.dateAdded;
}

bool lessByModSize(const ModEntry &a, const ModEntry &b)
{
    if (a.isSeparator() && b.isSeparator()) return false;
    if (a.isSeparator()) return false;
    if (b.isSeparator()) return true;

    // Size is populated lazily (Settings → compute sizes).  Rows without a
    // known size trail the sorted ones, matching the existing UI rule.
    if (a.modSize <= 0 && b.modSize <= 0) return false;
    if (a.modSize <= 0) return false;
    if (b.modSize <= 0) return true;
    return a.modSize < b.modSize;
}

QList<int> canonicalOrderFromAnchors(const QList<qint64> &anchors)
{
    QList<int> order;
    order.reserve(anchors.size());
    for (int i = 0; i < anchors.size(); ++i)
        order.append(i);

    // stable_sort so unstamped rows (key == max) keep their relative order at
    // the end, and equal anchors (shouldn't happen for stamped rows) are stable.
    constexpr qint64 kLast = std::numeric_limits<qint64>::max();
    std::stable_sort(order.begin(), order.end(), [&anchors](int a, int b) {
        const qint64 ka = anchors[a] < 0 ? kLast : anchors[a];
        const qint64 kb = anchors[b] < 0 ? kLast : anchors[b];
        return ka < kb;
    });
    return order;
}
