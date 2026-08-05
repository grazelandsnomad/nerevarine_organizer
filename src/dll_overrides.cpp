// Pure text surgery on a Proton prefix's user.reg. See dll_overrides.h for why
// this is needed at all (short version: a mod's dxgi.dll next to the .exe is
// inert under Wine until the prefix is told to prefer it).
//
// The file is Wine's own registry dump, one section per key:
//
//   [Software\\Wine\\DllOverrides] 1785137682
//   #time=1dd1d9a637ce01c
//   "atiadlxx"="disabled"
//   "atl100"="native,builtin"
//
// so we splice single lines into the section Wine already keeps sorted, rather
// than rewriting a file that holds the whole prefix's configuration.

#include "dll_overrides.h"

#include <QLatin1Char>
#include <QSet>
#include <QStringView>

#include <algorithm>

namespace {

// Names Wine implements itself, which a game would load from its own folder on
// Windows. Graphics wrappers (ReShade, ENB, DS2LightingEngine), input wrappers
// (Souls Mod Engine), and the generic proxies a loader can hide behind.
bool inWhitelist(const QString &base)
{
    static const QSet<QString> kNames = {
        QStringLiteral("d3d8"),           QStringLiteral("d3d9"),
        QStringLiteral("d3d10"),          QStringLiteral("d3d10core"),
        QStringLiteral("d3d11"),          QStringLiteral("d3d12"),
        QStringLiteral("d3d12core"),      QStringLiteral("dxgi"),
        QStringLiteral("ddraw"),          QStringLiteral("opengl32"),
        QStringLiteral("d3dcompiler_43"), QStringLiteral("d3dcompiler_47"),
        QStringLiteral("dinput"),         QStringLiteral("dinput8"),
        QStringLiteral("dsound"),         QStringLiteral("xinput1_1"),
        QStringLiteral("xinput1_2"),      QStringLiteral("xinput1_3"),
        QStringLiteral("xinput1_4"),      QStringLiteral("xinput9_1_0"),
        QStringLiteral("winmm"),          QStringLiteral("version"),
        QStringLiteral("wininet"),        QStringLiteral("winhttp"),
        QStringLiteral("dbghelp"),
    };
    return kNames.contains(base);
}

// "dxgi.dll" / "DXGI" -> "dxgi". Registry keys carry no extension.
QString baseName(const QString &fileName)
{
    QString s = fileName.trimmed().toLower();
    if (s.endsWith(QLatin1String(".dll"))) s.chop(4);
    return s;
}

// The section header, e.g. "[Software\\Wine\\DllOverrides] 1785137682". The
// closing bracket in the literal is what stops it matching a deeper key path -
// per-exe overrides live under [Software\\Wine\\AppDefaults\\<exe>\\...].
QString sectionHeader()
{
    return QStringLiteral("[Software\\\\Wine\\\\DllOverrides]");
}

// The name in `"dxgi"="native,builtin"`, or empty for anything that isn't an
// entry line (the "#time=" comment, blanks).
QString entryName(const QString &line)
{
    if (!line.startsWith(QLatin1Char('"'))) return {};
    const int close = line.indexOf(QLatin1Char('"'), 1);
    if (close < 0) return {};
    if (!QStringView(line).mid(close + 1).startsWith(QLatin1Char('='))) return {};
    return line.mid(1, close - 1);
}

bool isEntryFor(const QString &line, const QString &base)
{
    return QString::compare(entryName(line), base, Qt::CaseInsensitive) == 0;
}

QString entryLine(const QString &base)
{
    return QStringLiteral("\"%1\"=\"%2\"").arg(base,
                                               QLatin1String(dll_overrides::kNativeBuiltin));
}

// The section's line range as [first, last): `header` is the header's index, or
// -1 when the prefix has no such section.
struct Bounds { int header = -1; int end = 0; };

Bounds findSection(const QStringList &lines)
{
    Bounds b;
    const QString head = sectionHeader();
    for (int i = 0; i < lines.size(); ++i) {
        if (!lines.at(i).startsWith(head)) continue;
        b.header = i;
        b.end    = lines.size();
        for (int j = i + 1; j < lines.size(); ++j)
            if (lines.at(j).startsWith(QLatin1Char('['))) { b.end = j; break; }
        break;
    }
    return b;
}

// Normalize the caller's list to unique, extensionless, lowercase names.
QStringList wanted(const QStringList &dlls)
{
    QStringList out;
    for (const QString &d : dlls) {
        const QString b = baseName(d);
        if (!b.isEmpty() && !out.contains(b)) out << b;
    }
    return out;
}

} // namespace

namespace dll_overrides {

bool isWrapperDll(const QString &fileName)
{
    return inWhitelist(baseName(fileName));
}

QStringList wrapperDllsIn(const QStringList &relPaths)
{
    QStringList out;
    for (const QString &rel : relPaths) {
        // Only what sits at the deploy root: the loader searches the
        // application directory, not its subfolders.
        if (rel.contains(QLatin1Char('/'))) continue;
        if (!rel.endsWith(QLatin1String(".dll"), Qt::CaseInsensitive)) continue;
        const QString base = baseName(rel);
        if (!inWhitelist(base) || out.contains(base)) continue;
        out << base;
    }
    out.sort();
    return out;
}

QString addOverrides(const QString &userReg, const QStringList &dlls,
                     QStringList *added, QStringList *skipped)
{
    if (added)   added->clear();
    if (skipped) skipped->clear();

    const QStringList want = wanted(dlls);
    if (want.isEmpty()) return userReg;

    QStringList lines  = userReg.split(QLatin1Char('\n'));
    const Bounds bounds = findSection(lines);

    if (bounds.header < 0) {
        // A prefix with no overrides at all. Append the section; like an
        // imported .reg file it carries no timestamp, and Wine stamps one in
        // when it next saves the prefix.
        while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
            lines.removeLast();
        lines << QString() << sectionHeader();
        for (const QString &b : want) lines << entryLine(b);
        lines << QString();
        if (added) *added = want;
        return lines.join(QLatin1Char('\n'));
    }

    QStringList body = lines.mid(bounds.header + 1, bounds.end - bounds.header - 1);
    // Blank lines at the end belong to the gap before the next section, not to
    // the entries; hold them aside so inserts land among the entries.
    QStringList tail;
    while (!body.isEmpty() && body.last().trimmed().isEmpty())
        tail.prepend(body.takeLast());

    // Never insert above the header's own "#time=" comment.
    auto firstEntryRow = [&body] {
        int i = 0;
        while (i < body.size() && entryName(body.at(i)).isEmpty()) ++i;
        return i;
    };

    for (const QString &b : want) {
        const bool present = std::any_of(body.cbegin(), body.cend(),
                                         [&b](const QString &l) { return isEntryFor(l, b); });
        if (present) {                       // the user's own value - leave it
            if (skipped) *skipped << b;
            continue;
        }
        int at = body.size();
        for (int i = 0; i < body.size(); ++i) {
            const QString name = entryName(body.at(i));
            if (name.isEmpty()) continue;
            if (QString::compare(name, b, Qt::CaseInsensitive) > 0) { at = i; break; }
        }
        body.insert(qMax(at, firstEntryRow()), entryLine(b));
        if (added) *added << b;
    }

    QStringList out = lines.mid(0, bounds.header + 1);
    out += body;
    out += tail;
    out += lines.mid(bounds.end);
    return out.join(QLatin1Char('\n'));
}

QString removeOverrides(const QString &userReg, const QStringList &dlls,
                        QStringList *removed)
{
    if (removed) removed->clear();

    const QStringList want = wanted(dlls);
    if (want.isEmpty()) return userReg;

    QStringList lines  = userReg.split(QLatin1Char('\n'));
    const Bounds bounds = findSection(lines);
    if (bounds.header < 0) return userReg;

    QStringList out = lines.mid(0, bounds.header + 1);
    for (int i = bounds.header + 1; i < bounds.end; ++i) {
        const QString &line = lines.at(i);
        const QString name  = entryName(line);
        if (!name.isEmpty() && want.contains(name.toLower())) {
            if (removed) *removed << name.toLower();
            continue;
        }
        out << line;
    }
    out += lines.mid(bounds.end);
    return out.join(QLatin1Char('\n'));
}

} // namespace dll_overrides
