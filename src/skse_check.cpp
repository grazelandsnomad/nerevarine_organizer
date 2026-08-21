#include "skse_check.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace skse_check {
namespace {

quint32 le32(const QByteArray &b, int at)
{
    const auto *p = reinterpret_cast<const quint8 *>(b.constData()) + at;
    return quint32(p[0]) | (quint32(p[1]) << 8)
         | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

// Formats 1 and 2 are what the address library shipped for every runtime up
// to 1.6.1179, and what every plugin released so far was compiled to read.
// Anything past that is a format the older builds refuse.
constexpr int kLastWidelyReadFormat = 2;

} // namespace

Database parseAddressLibraryHeader(const QByteArray &head, const QString &file)
{
    Database db;
    db.file = file;
    if (head.size() < 20) return db;

    // format, then the four parts of the runtime it describes:
    //   1 1 5 97 0   ->  format 1, Skyrim 1.5.97
    //   2 1 6 1170 0 ->  format 2, Skyrim 1.6.1170
    //   5 1 7 99 0   ->  format 5, Skyrim 1.7.99
    const quint32 format = le32(head, 0);
    if (format == 0 || format > 64) return db;   // not a database header

    db.format         = int(format);
    db.runtime.major  = int(le32(head, 4));
    db.runtime.minor  = int(le32(head, 8));
    db.runtime.build  = int(le32(head, 12));
    db.runtime.sub    = int(le32(head, 16));
    db.runtime.valid  = true;
    return db;
}

namespace {

pe_info::Info readPe(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return pe_info::read(f.readAll());
}

} // namespace

Facts gather(const QString &gameExePath, const QString &gameRoot,
             const QString &pluginsDir, const QHash<QString, QString> &owners)
{
    Facts facts;

    if (QFileInfo::exists(gameExePath)) {
        const pe_info::Info gi = readPe(gameExePath);
        facts.game      = gi.fileVersion;
        facts.gameBuilt = gi.built;
    }
    // Without a version for the game there is nothing to compare against, and
    // reading nine DLLs to reach that conclusion would be wasted work.
    if (!facts.game.valid) return facts;

    // The script extender names the runtime it hooks in its own file name,
    // which is the only place that number is written down.
    for (const QString &dll : QDir(gameRoot).entryList(
             { QStringLiteral("*se64_*_*_*.dll"), QStringLiteral("*se_*_*_*.dll") },
             QDir::Files, QDir::Name)) {
        const pe_info::Version v = pe_info::runtimeFromLoaderName(dll);
        if (!v.valid) continue;
        facts.loaderFile    = dll;
        facts.loaderRuntime = v;
        break;
    }

    const QDir plugins(pluginsDir);
    if (!plugins.exists()) return facts;

    for (const QString &bin : plugins.entryList({ QStringLiteral("version*.bin") },
                                                QDir::Files, QDir::Name)) {
        QFile f(plugins.filePath(bin));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const Database db = parseAddressLibraryHeader(f.read(20), bin);
        if (!db.valid()) continue;
        facts.databases.append(db);
        const QDateTime when = QFileInfo(f).lastModified();
        if (when.isValid()
            && (!facts.newestDatabase.isValid() || when > facts.newestDatabase))
            facts.newestDatabase = when;
    }

    for (const QString &dll : plugins.entryList({ QStringLiteral("*.dll") },
                                                QDir::Files, QDir::Name)) {
        const pe_info::Info pi = readPe(plugins.filePath(dll));
        Plugin p;
        p.file     = dll;
        p.built    = pi.built;
        p.declared = pi.skse;
        p.mod      = owners.value(QStringLiteral("skse/plugins/") + dll.toLower());
        facts.plugins.append(p);
    }

    return facts;
}

Findings evaluate(const Facts &facts)
{
    Findings out;
    out.game          = facts.game;
    out.loaderFile    = facts.loaderFile;
    out.loaderRuntime = facts.loaderRuntime;

    // No version for the game means nothing to compare anything against, and
    // a guess would put mods on a list for no reason.
    if (!facts.game.valid) return out;

    // The script extender itself names the runtime it hooks. When that is not
    // the runtime on disk, the game was updated and SKSE was not, and every
    // other finding below would be noise on top of it.
    if (facts.loaderRuntime.valid && !facts.loaderRuntime.sameRuntime(facts.game))
        out.loaderMismatch = true;

    // Only plugins that say they use the address library are part of this. A
    // plugin with no version record at all says nothing, and silence is not
    // evidence: it is left alone rather than assumed guilty.
    const bool anyAddressLibrary =
        std::any_of(facts.plugins.begin(), facts.plugins.end(),
                    [](const Plugin &p) { return p.declared.usesAddressLibrary(); });
    if (!anyAddressLibrary) return out;

    const Database *db = nullptr;
    for (const Database &d : facts.databases)
        if (d.valid() && d.runtime.sameRuntime(facts.game)) { db = &d; break; }
    if (!db) {
        out.missingDatabase = true;
        return out;
    }
    out.databaseFormat = db->format;

    // A format the released plugins all read. Their age is then their own
    // business and none of ours.
    if (db->format <= kLastWidelyReadFormat) return out;

    // Anchored on the game's own build stamp: a plugin compiled before the
    // game version existed was not compiled for it. The database file's stamp
    // stands in when the executable carries no usable one, since that file is
    // the thing the plugin has to read.
    const QDateTime anchor =
        facts.gameBuilt.isValid() ? facts.gameBuilt : facts.newestDatabase;
    if (!anchor.isValid()) return out;

    for (const Plugin &p : facts.plugins) {
        if (!p.declared.usesAddressLibrary()) continue;
        if (!p.built.isValid() || p.built >= anchor) continue;

        Stale s;
        s.mod   = p.mod;
        s.file  = p.file;
        s.built = p.built;
        for (const pe_info::Version &v : p.declared.compatibleVersions) {
            // build == 0 is a placeholder, not a runtime: no Skyrim was ever
            // 1.0.0, and several plugins park that value there once an
            // independence flag makes the list advisory.
            if (v.build <= 0 || v.sameRuntime(facts.game)) continue;
            s.declaredFor = v;
            break;
        }
        out.stale.append(s);
    }

    std::sort(out.stale.begin(), out.stale.end(),
              [](const Stale &a, const Stale &b) {
                  if (a.mod != b.mod) return a.mod.localeAwareCompare(b.mod) < 0;
                  return a.file < b.file;
              });
    return out;
}

} // namespace skse_check
