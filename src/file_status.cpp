#include "file_status.h"

#include <QRegularExpression>

namespace file_status {
namespace {

// Authors retitle a file between uploads in small ways - "Tamriel Data (HD)"
// on one page, "Tamriel_Data (HD)" on the next - without meaning another file.
QString normalized(const QString &name)
{
    static const QRegularExpression kSpaces(QStringLiteral("[\\s_]+"));
    return name.toLower().replace(kSpaces, QStringLiteral(" ")).trimmed();
}

bool isOffered(const QString &category)
{
    return category == QLatin1String("MAIN") || category == QLatin1String("UPDATE");
}

// Off the page's current downloads. The v1 list spells the old-version
// category either way.
bool isRetired(const QString &category)
{
    const QString c = category.toUpper();
    return c == QLatin1String("OLD_VERSION") || c == QLatin1String("OLD VERSION")
        || c == QLatin1String("ARCHIVED")    || c == QLatin1String("REMOVED");
}

} // namespace

Verdict judge(qint64 installedFileId, const QList<PageFile> &pageFiles)
{
    Verdict v;
    if (installedFileId <= 0 || pageFiles.isEmpty()) return v;

    for (const PageFile &f : pageFiles)
        if (f.category == QLatin1String("MAIN")) v.current << f;
    if (v.current.isEmpty())
        for (const PageFile &f : pageFiles)
            if (f.category == QLatin1String("UPDATE")) v.current << f;

    const PageFile *installed = nullptr;
    for (const PageFile &f : pageFiles)
        if (f.fileId == installedFileId) { installed = &f; break; }

    if (installed) {
        v.installedName     = installed->name;
        v.installedVersion  = installed->version;
        v.installedCategory = installed->category;
        // Still offered, or an optional/miscellaneous file nobody retired.
        if (installed->category != QLatin1String("OLD_VERSION")
            && installed->category != QLatin1String("ARCHIVED")
            && installed->category != QLatin1String("REMOVED"))
            return v;
    } else {
        v.installedCategory = QStringLiteral("UNLISTED");
    }

    v.state = Verdict::State::Superseded;
    if (installed) {
        const QString want = normalized(installed->name);
        for (const PageFile &f : std::as_const(v.current))
            if (isOffered(f.category) && normalized(f.name) == want) {
                v.replacement = f;
                break;
            }
    }
    return v;
}

UpdateTarget updateTarget(const QList<PageFile> &pageFiles, qint64 installedFileId,
                          qint64 fallbackFileId, bool fallbackIsSafe)
{
    using K = UpdateTarget::Kind;
    const UpdateTarget pick{K::NeedsPick, 0};

    if (installedFileId <= 0)
        return fallbackIsSafe && fallbackFileId > 0
            ? UpdateTarget{K::Install, fallbackFileId} : pick;

    // The author's own record of which upload replaced which.
    for (const PageFile &f : pageFiles)
        if (f.replaces.contains(installedFileId))
            return isRetired(f.category) ? pick : UpdateTarget{K::Install, f.fileId};

    const PageFile *mine = nullptr;
    for (const PageFile &f : pageFiles)
        if (f.fileId == installedFileId) { mine = &f; break; }
    if (!mine) return pick;

    // A new upload under the same name. Nexus ids only grow, so the highest
    // is the newest.
    const QString want = normalized(mine->name);
    const bool mineRetired = isRetired(mine->category);
    const PageFile *next = nullptr;
    for (const PageFile &f : pageFiles) {
        if (f.fileId == installedFileId || isRetired(f.category)
            || normalized(f.name) != want)
            continue;
        if (!mineRetired && (f.category != mine->category || f.fileId < installedFileId))
            continue;
        if (!next || f.fileId > next->fileId) next = &f;
    }
    if (next) return {K::Install, next->fileId};
    if (mineRetired) return pick;

    if (!isOffered(mine->category) || fallbackFileId == installedFileId)
        return {K::AlreadyNewest, installedFileId};
    return pick;
}

} // namespace file_status
