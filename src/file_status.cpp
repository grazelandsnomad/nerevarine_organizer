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

} // namespace file_status
