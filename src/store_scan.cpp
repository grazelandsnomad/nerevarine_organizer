#include "store_scan.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace store_scan {
namespace {

// Words a store adds and we do not, or the reverse. Dropped from both sides so
// "Gothic 2 Gold Edition" and "Gothic II" compare as the same game.
const QStringList kNoiseWords = {
    QStringLiteral("edition"), QStringLiteral("editon"),
    QStringLiteral("gold"),    QStringLiteral("goty"),
    QStringLiteral("game"),    QStringLiteral("of"),
    QStringLiteral("the"),     QStringLiteral("year"),
    QStringLiteral("complete"),QStringLiteral("enhanced"),
    QStringLiteral("definitive"), QStringLiteral("deluxe"),
    QStringLiteral("remastered"), QStringLiteral("anthology"),
    QStringLiteral("classic"), QStringLiteral("ultimate"),
};

// Only the low numerals, which is all a game title ever uses. Folded to digits
// because the same game is "Gothic II" in one place and "Gothic 2" in another.
QString romanToDigit(const QString &token)
{
    static const QHash<QString, QString> kRoman = {
        {QStringLiteral("ii"),   QStringLiteral("2")},
        {QStringLiteral("iii"),  QStringLiteral("3")},
        {QStringLiteral("iv"),   QStringLiteral("4")},
        {QStringLiteral("v"),    QStringLiteral("5")},
        {QStringLiteral("vi"),   QStringLiteral("6")},
        {QStringLiteral("vii"),  QStringLiteral("7")},
        {QStringLiteral("viii"), QStringLiteral("8")},
        {QStringLiteral("ix"),   QStringLiteral("9")},
        {QStringLiteral("x"),    QStringLiteral("10")},
    };
    return kRoman.value(token, token);
}

QJsonArray installedArray(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (doc.isArray()) return doc.array();
    if (doc.isObject()) {
        const QJsonObject o = doc.object();
        if (o.value(QStringLiteral("installed")).isArray())
            return o.value(QStringLiteral("installed")).toArray();
    }
    return {};
}

} // namespace

// -- pure parsers ------------------------------------------------------

QList<HeroicInstall> parseHeroicInstalled(const QByteArray &json)
{
    QList<HeroicInstall> out;
    const QJsonArray arr = installedArray(json);
    for (const QJsonValue &v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        HeroicInstall e;
        e.appName     = o.value(QStringLiteral("appName")).toString();
        e.installPath = o.value(QStringLiteral("install_path")).toString();
        e.executable  = o.value(QStringLiteral("executable")).toString();
        if (e.installPath.isEmpty()) continue;
        out.append(e);
    }
    return out;
}

QHash<QString, StoreTitle> parseHeroicLibrary(const QByteArray &json)
{
    QHash<QString, StoreTitle> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    QJsonArray games;
    if (doc.isArray()) games = doc.array();
    else if (doc.isObject()) games = doc.object().value(QStringLiteral("games")).toArray();

    for (const QJsonValue &v : games) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("app_name")).toString();
        if (id.isEmpty()) continue;
        StoreTitle t;
        t.title      = o.value(QStringLiteral("title")).toString();
        t.folderName = o.value(QStringLiteral("folder_name")).toString();
        if (t.title.isEmpty() && t.folderName.isEmpty()) continue;
        // Deliberately NOT gated on the entry's is_installed flag: it reads
        // false for the author's installed Gothic II. installed.json is the
        // truth about what is installed; this file is only a source of names.
        out.insert(id, t);
    }
    return out;
}

QString steamInstallDir(const QByteArray &acf)
{
    static const QRegularExpression rx(
        QStringLiteral("\"installdir\"\\s*\"([^\"]+)\""),
        QRegularExpression::CaseInsensitiveOption);
    const auto m = rx.match(QString::fromUtf8(acf));
    return m.hasMatch() ? m.captured(1) : QString();
}

// -- name comparison ---------------------------------------------------

QString normalizeTitle(const QString &s)
{
    static const QRegularExpression rxNonWord(QStringLiteral("[^a-z0-9]+"));
    const QStringList raw =
        s.toLower().split(rxNonWord, Qt::SkipEmptyParts);

    QStringList tokens;
    for (const QString &t : raw) {
        const QString folded = romanToDigit(t);
        if (kNoiseWords.contains(folded)) continue;
        tokens << folded;
    }
    return tokens.join(QLatin1Char(' '));
}

bool titleMatches(const QString &wanted, const QString &candidate)
{
    const QStringList want = normalizeTitle(wanted).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QStringList have = normalizeTitle(candidate).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (want.isEmpty() || have.isEmpty()) return false;
    for (const QString &t : want)
        if (!have.contains(t)) return false;
    return true;
}

// -- the stores on this machine ----------------------------------------

QStringList heroicConfigDirs()
{
    const QString home = QDir::homePath();
    return {
        home + QStringLiteral("/.config/heroic"),
        home + QStringLiteral("/.var/app/com.heroicgameslauncher.hgl/config/heroic"),
    };
}

QList<HeroicInstall> heroicInstalls(const QStringList &configDirs)
{
    QList<HeroicInstall> out;
    for (const QString &cfg : configDirs) {
        QFile f(cfg + QStringLiteral("/gog_store/installed.json"));
        if (!f.open(QIODevice::ReadOnly)) continue;
        out += parseHeroicInstalled(f.readAll());
    }
    return out;
}

QList<HeroicInstall> heroicInstalls() { return heroicInstalls(heroicConfigDirs()); }

QHash<QString, StoreTitle> heroicTitles(const QStringList &configDirs)
{
    QHash<QString, StoreTitle> out;
    for (const QString &cfg : configDirs) {
        QFile f(cfg + QStringLiteral("/store_cache/gog_library.json"));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const auto one = parseHeroicLibrary(f.readAll());
        for (auto it = one.constBegin(); it != one.constEnd(); ++it)
            if (!out.contains(it.key())) out.insert(it.key(), it.value());
    }
    return out;
}

QHash<QString, StoreTitle> heroicTitles() { return heroicTitles(heroicConfigDirs()); }

QStringList steamLibraryRoots()
{
    // Two shapes of libraryfolders.vdf in the wild, match both:
    //   modern: "0" { "path" "/mnt/.../SteamLibrary" ... }
    //   legacy: "1"  "/mnt/.../SteamLibrary"
    // Without this, a game on a second drive fails detection and the user is
    // sent to find the .exe by hand.
    QStringList roots;
    auto pushIfNew = [&roots](const QString &p) {
        if (!p.isEmpty() && !roots.contains(p) && QFileInfo::exists(p))
            roots.append(p);
    };

    const QString home = QDir::homePath();
    pushIfNew(home + QStringLiteral("/.steam/steam/steamapps"));
    pushIfNew(home + QStringLiteral("/.local/share/Steam/steamapps"));
    pushIfNew(QStringLiteral("/mnt/games/Steam/steamapps"));

    const QStringList vdfCandidates = {
        home + QStringLiteral("/.steam/steam/steamapps/libraryfolders.vdf"),
        home + QStringLiteral("/.local/share/Steam/steamapps/libraryfolders.vdf"),
        home + QStringLiteral("/.steam/root/steamapps/libraryfolders.vdf"),
        home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/libraryfolders.vdf"),
    };

    static const QRegularExpression rxPath(
        QStringLiteral("\"path\"\\s+\"([^\"]+)\""),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rxLegacy(
        QStringLiteral("^\\s*\"\\d+\"\\s+\"(/[^\"]+)\"\\s*$"),
        QRegularExpression::MultilineOption);

    for (const QString &vdf : vdfCandidates) {
        QFile f(vdf);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString contents = QString::fromUtf8(f.readAll());
        f.close();

        auto it = rxPath.globalMatch(contents);
        while (it.hasNext())
            pushIfNew(it.next().captured(1) + QStringLiteral("/steamapps"));
        auto it2 = rxLegacy.globalMatch(contents);
        while (it2.hasNext())
            pushIfNew(it2.next().captured(1) + QStringLiteral("/steamapps"));
    }
    return roots;
}

QString steamAppInstallPath(const QStringList &steamAppsDirs, const QString &appId)
{
    if (appId.isEmpty()) return {};
    for (const QString &dir : steamAppsDirs) {
        QFile f(QDir(dir).filePath(QStringLiteral("appmanifest_") + appId
                                   + QStringLiteral(".acf")));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString installDir = steamInstallDir(f.readAll());
        if (installDir.isEmpty()) continue;
        const QString path = QDir(dir).filePath(QStringLiteral("common/") + installDir);
        if (QFileInfo(path).isDir()) return path;
    }
    return {};
}

QString steamAppInstallPath(const QString &appId)
{
    return steamAppInstallPath(steamLibraryRoots(), appId);
}

QStringList lutrisExePaths()
{
    QStringList out;
    const QString home = QDir::homePath();
    const QStringList dirs = {
        home + QStringLiteral("/.config/lutris/games"),
        home + QStringLiteral("/.var/app/net.lutris.Lutris/config/lutris/games"),
    };
    static const QRegularExpression rxExe(
        QStringLiteral("^\\s+exe:\\s*[\"']?([^\"'\\n]+)[\"']?\\s*$"),
        QRegularExpression::MultilineOption);

    for (const QString &dir : dirs) {
        QDir d(dir);
        if (!d.exists()) continue;
        const QStringList ymls =
            d.entryList({QStringLiteral("*.yml"), QStringLiteral("*.yaml")}, QDir::Files);
        for (const QString &name : ymls) {
            QFile f(d.absoluteFilePath(name));
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            const QString text = QString::fromUtf8(f.readAll());
            auto it = rxExe.globalMatch(text);
            while (it.hasNext()) {
                const QString exe = it.next().captured(1).trimmed();
                if (!exe.isEmpty() && !out.contains(exe)) out << exe;
            }
        }
    }
    return out;
}

QStringList allInstallPaths()
{
    QStringList out;
    QSet<QString> seen;
    auto push = [&out, &seen](const QString &p) {
        if (p.isEmpty()) return;
        const QString clean = QDir::cleanPath(p);
        if (seen.contains(clean)) return;
        if (!QFileInfo::exists(clean)) return;
        seen.insert(clean);
        out << clean;
    };

    for (const HeroicInstall &h : heroicInstalls()) push(h.installPath);
    for (const QString &lib : steamLibraryRoots()) {
        const QDir common(QDir(lib).filePath(QStringLiteral("common")));
        if (!common.exists()) continue;
        for (const QString &sub : common.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            push(common.filePath(sub));
    }
    for (const QString &exe : lutrisExePaths()) push(exe);
    return out;
}

} // namespace store_scan
