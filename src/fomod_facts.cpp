#include "fomod_facts.h"

#include "fomod_path.h"

#include <QDir>
#include <QHash>

#include <memory>

namespace fomod_facts {
namespace {

using fomod::FileState;

// Installers write Windows paths: "F4SE\Plugins\x.dll", now and then with a
// leading separator.
QString normalized(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (path.startsWith(QLatin1Char('/'))) path.remove(0, 1);
    return path.trimmed();
}

bool isDataPlugin(const QString &lower)
{
    return lower.endsWith(QLatin1String(".esp")) || lower.endsWith(QLatin1String(".esm"))
        || lower.endsWith(QLatin1String(".esl"));
}

bool isOpenMwContent(const QString &lower)
{
    return lower.endsWith(QLatin1String(".esp")) || lower.endsWith(QLatin1String(".esm"))
        || lower.endsWith(QLatin1String(".omwaddon"))
        || lower.endsWith(QLatin1String(".omwscripts"));
}

struct State {
    Profile              profile;
    bool                 listed = false;
    QSet<QString>        plugins;   // DataFolder: active plugin names, lower-cased
    QHash<QString, bool> paths;     // DataFolder: other paths asked, found or not
};

void listPlugins(State &s)
{
    s.listed = true;
    const QStringList filters = {QStringLiteral("*.esp"), QStringLiteral("*.esm"),
                                 QStringLiteral("*.esl")};
    // A plugin below a mod's root is deployed below Data/'s root, where the
    // game never loads it - so only the root counts.
    for (const QString &root : std::as_const(s.profile.modRoots))
        for (const QString &f : QDir(root).entryList(filters, QDir::Files))
            s.plugins.insert(f.toLower());
    if (s.profile.dataDir.isEmpty()) return;
    for (const QString &f : QDir(s.profile.dataDir).entryList(filters, QDir::Files)) {
        const QString lower = f.toLower();
        // Deployed from a mod: that mod's own folder answers for it, and one
        // since unticked must not answer from a stale link.
        if (!s.profile.deployed.contains(lower)) s.plugins.insert(lower);
    }
}

std::optional<FileState> answer(State &s, const QString &path)
{
    const QString rel   = normalized(path);
    const QString lower = rel.toLower();
    if (rel.isEmpty()) return std::nullopt;

    switch (s.profile.layout) {
    case Profile::Layout::Unknown:
        return std::nullopt;

    case Profile::Layout::DataFolder: {
        const bool sure = s.profile.complete && !s.profile.dataDir.isEmpty();
        if (!lower.contains(QLatin1Char('/')) && isDataPlugin(lower)) {
            if (!s.listed) listPlugins(s);
            if (s.plugins.contains(lower)) return FileState::Active;
            return sure ? std::optional(FileState::Missing) : std::nullopt;
        }
        auto it = s.paths.constFind(lower);
        if (it == s.paths.constEnd()) {
            bool found = false;
            for (const QString &root : std::as_const(s.profile.modRoots))
                if (!fomod::resolvePath(root, rel).isEmpty()) { found = true; break; }
            if (!found && !s.profile.dataDir.isEmpty()
                && !s.profile.deployed.contains(lower))
                found = !fomod::resolvePath(s.profile.dataDir, rel).isEmpty();
            it = s.paths.insert(lower, found);
        }
        if (*it) return FileState::Active;
        return sure ? std::optional(FileState::Missing) : std::nullopt;
    }

    case Profile::Layout::OpenMW: {
        if (lower.contains(QLatin1Char('/')) || !isOpenMwContent(lower))
            return std::nullopt;
        // Always there: OpenMW is not run without them, and openmw.cfg is
        // written on that assumption (openmwconfigwriter.cpp).
        static const QSet<QString> kBaseMasters = {
            QStringLiteral("morrowind.esm"), QStringLiteral("tribunal.esm"),
            QStringLiteral("bloodmoon.esm")};
        if (s.profile.heldBack.contains(lower)) return FileState::Inactive;
        if (s.profile.content.contains(lower) || kBaseMasters.contains(lower))
            return FileState::Active;
        return s.profile.complete ? std::optional(FileState::Missing) : std::nullopt;
    }
    }
    return std::nullopt;
}

} // namespace

fomod::FileStateFn fileState(const Profile &profile)
{
    if (profile.layout == Profile::Layout::Unknown) return {};
    auto state = std::make_shared<State>();
    state->profile = profile;
    return [state](const QString &path) { return answer(*state, path); };
}

} // namespace fomod_facts
