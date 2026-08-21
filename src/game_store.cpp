#include "game_store.h"

#include <QDir>
#include <QRegularExpression>

namespace game_store {
namespace {

// The store words as they appear in Nexus file names. Whole words only.
const QRegularExpression &rxGog()
{
    static const QRegularExpression rx(QStringLiteral("\\bgog\\b"),
                                       QRegularExpression::CaseInsensitiveOption);
    return rx;
}

const QRegularExpression &rxSteam()
{
    static const QRegularExpression rx(QStringLiteral("\\bsteam\\b"),
                                       QRegularExpression::CaseInsensitiveOption);
    return rx;
}

} // namespace

Store fromFileName(const QString &name)
{
    const bool gog   = rxGog().match(name).hasMatch();
    const bool steam = rxSteam().match(name).hasMatch();
    // A file naming both stores is describing something else - a comparison,
    // a merged package - and is no evidence for either.
    if (gog == steam) return Store::Unknown;
    return gog ? Store::Gog : Store::Steam;
}

QString stripStoreWords(const QString &s)
{
    QString out = s;
    out.remove(rxGog());
    out.remove(rxSteam());
    return out;
}

Store fromInstallPath(const QString &path, const QStringList &gogRoots)
{
    if (path.isEmpty()) return Store::Unknown;
    const QString clean = QDir::cleanPath(path);

    // Steam names the directory itself. Old libraries spell it "SteamApps",
    // so the component is compared without case.
    for (const QString &part : clean.split(QLatin1Char('/'), Qt::SkipEmptyParts))
        if (part.compare(QLatin1String("steamapps"), Qt::CaseInsensitive) == 0)
            return Store::Steam;

    for (const QString &root : gogRoots) {
        if (root.isEmpty()) continue;
        const QString r = QDir::cleanPath(root);
        if (r == QLatin1String("/")) continue;   // a root of "/" owns everything
        if (clean == r || clean.startsWith(r + QLatin1Char('/')))
            return Store::Gog;
    }

    return Store::Unknown;
}

QString name(Store s)
{
    switch (s) {
    case Store::Steam:   return QStringLiteral("Steam");
    case Store::Gog:     return QStringLiteral("GOG");
    case Store::Unknown: break;
    }
    return {};
}

} // namespace game_store
