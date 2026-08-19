#include "opengothic.h"

#include "fomod_path.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimeZone>

#include <sys/stat.h>

namespace opengothic {
namespace {

// VDFS header, as the engine reads it (Resources::vdfTimestamp):
//   [0]   256 bytes  comment
//   [256]  16 bytes  signature
//   [272]   4 bytes  entry count
//   [276]   4 bytes  file count
//   [280]   4 bytes  timestamp, MS-DOS packed, little endian
constexpr qint64 kSigOffset   = 256;
constexpr qint64 kSigLen      = 16;
constexpr qint64 kStampOffset = 280;
const char       kSignature[] = "PSVDSC_V2.00\n\r\n\r";

// QTimeZone::utc(), not QTimeZone::UTC: the enum constant is Qt 6.5, and CI
// builds against Ubuntu 24.04's qt6-base-dev, which is 6.4.2. The static has
// been there since Qt 5.2 and means the same thing.

// Positional stamps start here. Far past anything a human authored (Gothic II
// itself is 2002, its addon 2003, and a mod released today is still 20-odd
// years short of this), so a deployed archive always outranks the base game.
// Still well inside the DOS date range, which runs to 2107.
const QDate  kStampBaseDate(2038, 1, 1);
// MS-DOS packs seconds in two-second units, so consecutive positions have to be
// two seconds apart or two mods would tie and the mount order would decide.
constexpr int kStampStepSecs = 2;

bool readHeader(const QString &path, QByteArray &head)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    head = f.read(kStampOffset + 4);
    return head.size() >= kStampOffset + 4;
}

bool signatureOk(const QByteArray &head)
{
    return head.size() >= kSigOffset + kSigLen
        && head.mid(kSigOffset, kSigLen) == QByteArray(kSignature, kSigLen);
}

// How many names point at this file. Deploy hardlinks the game's copy to the
// one in the mod store, and both names are then the same bytes: writing a new
// header through one of them edits the user's stored archive too.
int hardLinkCount(const QString &path)
{
    struct stat st = {};
    if (::stat(QFile::encodeName(path).constData(), &st) != 0) return -1;
    return int(st.st_nlink);
}

// Give `path` a private copy of its bytes, so writing to it cannot reach any
// other name for the same inode.
bool breakHardLink(const QString &path)
{
    const QString tmp = path + QStringLiteral(".nerevarine-tmp");
    QFile::remove(tmp);
    if (!QFile::copy(path, tmp)) return false;
    if (!QFile::remove(path))  { QFile::remove(tmp); return false; }
    if (!QFile::rename(tmp, path)) { return false; }
    return true;
}

} // namespace

// -- the Gothic II install ---------------------------------------------

bool isGameRoot(const QString &dir)
{
    if (dir.isEmpty()) return false;
    // The engine's own test, in the engine's own order
    // (CommandLine::validateGothicPath). fomod::resolvePath is reused for the
    // case-insensitive segment walk: it is not FOMOD-specific, and OpenGothic
    // resolves its paths exactly the same way (FileUtil::caseInsensitiveSegment),
    // so a Data/ unpacked as data/ has to be found here too.
    static const QStringList kMust = {
        QStringLiteral("Data"),
        QStringLiteral("_work/Data"),
        QStringLiteral("_work/Data/Scripts/_compiled"),
    };
    for (const QString &rel : kMust) {
        const QString resolved = fomod::resolvePath(dir, rel);
        if (resolved.isEmpty() || !QFileInfo(resolved).isDir()) return false;
    }
    return true;
}

QString gameRootFor(const QString &path)
{
    if (path.isEmpty()) return {};
    QString dir = QFileInfo(path).isDir() ? QDir::cleanPath(path)
                                          : QFileInfo(path).absolutePath();
    // Four levels is enough for every real layout: the locators hand over
    // <root>/system/Gothic2.exe, and a Steam/GOG tree adds no more depth.
    for (int i = 0; i < 4 && !dir.isEmpty(); ++i) {
        if (isGameRoot(dir)) return dir;
        const QString up = QFileInfo(dir).absolutePath();
        if (up == dir) break;
        dir = up;
    }
    return {};
}

QStringList findGameRoots(const QString &startDir, int maxDepth, int maxDirs)
{
    QStringList found;
    if (startDir.isEmpty() || !QFileInfo(startDir).isDir()) return found;

    QStringList level{QDir::cleanPath(startDir)};
    int examined = 0;
    for (int depth = 0; depth <= maxDepth && !level.isEmpty(); ++depth) {
        QStringList next;
        for (const QString &dir : std::as_const(level)) {
            if (++examined > maxDirs) return found;
            if (isGameRoot(dir)) {
                found << dir;
                continue;              // no point descending into an install
            }
            if (depth == maxDepth) continue;
            const QDir d(dir);
            for (const QString &sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                next << d.filePath(sub);
        }
        // A handful is all any caller can offer; stop rather than enumerate a
        // whole library of installs.
        if (found.size() >= 5) break;
        level = next;
    }
    return found;
}

// -- the engine binary -------------------------------------------------

QStringList engineNames()
{
    // One binary per game edition; Gothic II: Night of the Raven is the one
    // the project supports and the one this game profile is about. The .sh is
    // the portable build's wrapper, which is what a CI download unpacks.
    return {QStringLiteral("Gothic2Notr"),
            QStringLiteral("Gothic2Notr.sh"),
            QStringLiteral("opengothic")};
}

QString findEngine(const QStringList &extraDirs)
{
    QStringList dirs = extraDirs;
    // Where the packages put it, plus the layout of a source build, since
    // building it is how you get it on a distro with no package.
    dirs << QStringLiteral("/usr/bin")
         << QStringLiteral("/usr/local/bin")
         << QStringLiteral("/opt/opengothic")
         << QDir::homePath() + QStringLiteral("/.local/bin");

    for (const QString &d : std::as_const(dirs)) {
        if (d.isEmpty()) continue;
        for (const QString &name : engineNames()) {
            for (const QString &rel : {QString(),
                                       QStringLiteral("build/opengothic/"),
                                       QStringLiteral("opengothic/"),
                                       QStringLiteral("OpenGothic/"),
                                       QStringLiteral("OpenGothic/build/opengothic/"),
                                       QStringLiteral("opengothic/build/opengothic/")}) {
                const QString cand = QDir(d).filePath(rel + name);
                const QFileInfo fi(cand);
                if (fi.isFile() && fi.isExecutable()) return fi.absoluteFilePath();
            }
        }
    }
    for (const QString &name : engineNames()) {
        const QString onPath = QStandardPaths::findExecutable(name);
        if (!onPath.isEmpty()) return onPath;
    }
    return {};
}

QStringList engineSearchHints(const QString &gameRoot)
{
    QStringList out;
    // Up from the game folder: the author's copy sits two levels up from the
    // install, next to the games directory rather than inside it.
    QString dir = gameRoot;
    for (int i = 0; i < 3 && !dir.isEmpty(); ++i) {
        const QString up = QFileInfo(dir).absolutePath();
        if (up == dir) break;
        out << up;
        dir = up;
    }
    out << QDir::homePath()
        << QDir::homePath() + QStringLiteral("/Games")
        << QDir::homePath() + QStringLiteral("/.local/share");
    out.removeDuplicates();
    return out;
}

QStringList launchArgs(const QString &gameRoot, const QString &modIni)
{
    QStringList args;
    if (!gameRoot.isEmpty()) args << QStringLiteral("-g") << gameRoot;
    // One token, no space: the engine matches the "-game:" prefix on a single
    // argument and takes the rest of it as the file name.
    if (!modIni.isEmpty()) args << QStringLiteral("-game:") + modIni;
    return args;
}

// -- VDFS archives -----------------------------------------------------

bool isArchive(const QString &path)
{
    QByteArray head;
    return readHeader(path, head) && signatureOk(head);
}

qint64 readStamp(const QString &path)
{
    QByteArray head;
    if (!readHeader(path, head) || !signatureOk(head)) return -1;
    const uchar *p = reinterpret_cast<const uchar *>(head.constData()) + kStampOffset;
    return qint64(quint32(p[0]) | (quint32(p[1]) << 8)
                | (quint32(p[2]) << 16) | (quint32(p[3]) << 24));
}

bool writeStamp(const QString &path, quint32 stamp)
{
    if (!isArchive(path)) return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite)) return false;
    if (!f.seek(kStampOffset)) return false;
    const char bytes[4] = {
        char(stamp & 0xff), char((stamp >> 8) & 0xff),
        char((stamp >> 16) & 0xff), char((stamp >> 24) & 0xff),
    };
    return f.write(bytes, 4) == 4;
}

quint32 toDosStamp(const QDateTime &t)
{
    const QDate d = t.date();
    const QTime tm = t.time();
    const int year = qBound(1980, d.year(), 2107) - 1980;
    const quint32 date = quint32(year) << 9 | quint32(d.month()) << 5 | quint32(d.day());
    const quint32 time = quint32(tm.hour()) << 11 | quint32(tm.minute()) << 5
                       | quint32(tm.second() / 2);
    return date << 16 | time;
}

QDateTime fromDosStamp(quint32 stamp)
{
    const quint32 date = stamp >> 16;
    const quint32 time = stamp & 0xffff;
    const QDate d(int(date >> 9) + 1980, int((date >> 5) & 0xf), int(date & 0x1f));
    const QTime t(int(time >> 11), int((time >> 5) & 0x3f), int((time & 0x1f) * 2));
    if (!d.isValid() || !t.isValid()) return {};
    return QDateTime(d, t, QTimeZone::utc());
}

quint32 stampForIndex(int index)
{
    const QDateTime base(kStampBaseDate, QTime(0, 0), QTimeZone::utc());
    return toDosStamp(base.addSecs(qint64(qMax(0, index)) * kStampStepSecs));
}

StampResult applyOrder(const QStringList &archives)
{
    StampResult r;
    for (int i = 0; i < archives.size(); ++i) {
        const QString &path = archives[i];
        const qint64 have = readStamp(path);
        if (have < 0) {
            r.errors << QStringLiteral("%1: not a readable VDFS archive")
                            .arg(QFileInfo(path).fileName());
            continue;
        }
        const quint32 want = stampForIndex(i);
        if (quint32(have) == want) { ++r.alreadyRight; continue; }

        if (hardLinkCount(path) > 1 && !breakHardLink(path)) {
            r.errors << QStringLiteral("%1: cannot detach from the mod store copy")
                            .arg(QFileInfo(path).fileName());
            continue;
        }
        if (!writeStamp(path, want)) {
            r.errors << QStringLiteral("%1: cannot write the archive header")
                            .arg(QFileInfo(path).fileName());
            continue;
        }
        ++r.stamped;
    }
    return r;
}

// -- mod inis ----------------------------------------------------------

ModIni parseModIni(const QString &text)
{
    ModIni out;
    QString section;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                         Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        // The engine's own parser ends a name or a value at ';', so a trailing
        // comment is not part of the value.
        const int comment = line.indexOf(QLatin1Char(';'));
        if (comment >= 0) line = line.left(comment).trimmed();
        if (line.isEmpty()) continue;

        if (line.startsWith(QLatin1Char('['))) {
            const int close = line.indexOf(QLatin1Char(']'));
            section = (close > 1 ? line.mid(1, close - 1) : line.mid(1)).trimmed().toUpper();
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = line.left(eq).trimmed().toUpper();
        const QString val = line.mid(eq + 1).trimmed();
        if (val.isEmpty()) continue;

        if (section == QLatin1String("FILES")) {
            if (key == QLatin1String("VDF"))
                out.vdf = val.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            else if (key == QLatin1String("GAME"))
                out.gameDat = val;
            else if (key == QLatin1String("OUTPUTUNITS"))
                out.outputUnits = val;
        } else if (section == QLatin1String("SETTINGS")) {
            if (key == QLatin1String("WORLD"))       out.world = val;
            else if (key == QLatin1String("PLAYER")) out.player = val;
        } else if (section == QLatin1String("INFO")) {
            if (key == QLatin1String("TITLE")) out.title = val;
        }
    }
    return out;
}

QString buildModIni(const ModIni &ini)
{
    // CRLF and uppercase section names, the way GothicStarter writes them: the
    // file may well be opened by the original launcher on a dual-boot install,
    // and the engine reads either ending.
    const QString nl = QStringLiteral("\r\n");
    QString out;
    out += QStringLiteral("[INFO]") + nl;
    out += QStringLiteral("Title=")
         + (ini.title.isEmpty() ? QStringLiteral("Nerevarine Organizer") : ini.title) + nl;
    out += QStringLiteral("Description=Generated from the modlist. Edits are "
                          "overwritten on the next deploy.") + nl;
    out += nl;
    out += QStringLiteral("[FILES]") + nl;
    out += QStringLiteral("VDF=") + ini.vdf.join(QLatin1Char(' ')) + nl;
    if (!ini.gameDat.isEmpty())
        out += QStringLiteral("GAME=") + ini.gameDat + nl;
    if (!ini.outputUnits.isEmpty())
        out += QStringLiteral("OUTPUTUNITS=") + ini.outputUnits + nl;
    if (!ini.world.isEmpty() || !ini.player.isEmpty()) {
        out += nl;
        out += QStringLiteral("[SETTINGS]") + nl;
        if (!ini.world.isEmpty())  out += QStringLiteral("WORLD=")  + ini.world  + nl;
        if (!ini.player.isEmpty()) out += QStringLiteral("PLAYER=") + ini.player + nl;
    }
    return out;
}

ModIni mergeModInis(const QList<ModIni> &inis, const QStringList &archiveNames)
{
    ModIni out;
    for (const ModIni &in : inis) {
        for (const QString &v : in.vdf)
            if (!out.vdf.contains(v, Qt::CaseInsensitive)) out.vdf << v;
        // Last writer wins, like the list itself: a total conversion further
        // down overrides the one above it rather than being ignored.
        if (!in.gameDat.isEmpty())     out.gameDat     = in.gameDat;
        if (!in.outputUnits.isEmpty()) out.outputUnits = in.outputUnits;
        if (!in.world.isEmpty())       out.world       = in.world;
        if (!in.player.isEmpty())      out.player      = in.player;
    }
    // Archives nobody declared. Most Gothic mods are a bare .mod with no ini at
    // all, and an unlisted .mod is thrown away by the engine, so this is what
    // makes those mods load.
    for (const QString &name : archiveNames)
        if (!out.vdf.contains(name, Qt::CaseInsensitive)) out.vdf << name;
    return out;
}

FolderMapping mapModFolder(const QString &modDir)
{
    FolderMapping m;
    const QDir d(modDir);
    if (!d.exists()) return m;

    // Any of the game's own folders means the mod is packaged the game's way.
    // Case-insensitive, since these come out of Windows archives.
    for (const QString &sub : {QStringLiteral("Data"), QStringLiteral("system"),
                               QStringLiteral("_work")}) {
        if (!fomod::resolvePath(modDir, sub).isEmpty()) { m.overlay = true; return m; }
    }

    for (const QString &name : d.entryList(QDir::Files)) {
        const QString low = name.toLower();
        if (low.endsWith(QLatin1String(".mod")) || low.endsWith(QLatin1String(".vdf"))) {
            // By signature: a "readme.vdf" would otherwise be handed to the
            // engine as an archive and fail to mount.
            if (isArchive(d.filePath(name))) m.archives << name;
        } else if (low.endsWith(QLatin1String(".ini"))) {
            m.inis << name;
        }
    }
    // An ini with no archive beside it is not a mod ini worth moving into
    // system/ (a settings snippet, a readme in disguise), and putting it there
    // would offer the user a mod that loads nothing.
    if (m.archives.isEmpty()) m.inis.clear();
    return m;
}

ActivationPlan planActivation(const QString &gameRoot, const QStringList &deployedRels)
{
    ActivationPlan plan;
    const QDir root(gameRoot);
    for (const QString &rel : deployedRels) {
        const QString low = rel.toLower();
        const QString name = QFileInfo(rel).fileName();
        if (low.endsWith(QLatin1String(".mod")) || low.endsWith(QLatin1String(".vdf"))) {
            const QString abs = root.filePath(rel);
            if (!isArchive(abs)) continue;          // named like one, is not one
            if (name.contains(QLatin1Char(' '))) { plan.unusable << name; continue; }
            plan.archivePaths << abs;
            plan.archiveNames << name;
        } else if (low.startsWith(QLatin1String("system/"))
                && low.endsWith(QLatin1String(".ini"))
                && name.compare(generatedIniName(), Qt::CaseInsensitive) != 0) {
            QFile f(root.filePath(rel));
            if (!f.open(QIODevice::ReadOnly)) continue;
            const ModIni parsed = parseModIni(QString::fromUtf8(f.readAll()));
            f.close();
            if (!parsed.isEmpty()) plan.modInis << parsed;
        }
    }
    return plan;
}

QString generatedIniName()
{
    return QStringLiteral("Nerevarine.ini");
}

} // namespace opengothic
