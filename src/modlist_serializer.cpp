#include "modlist_serializer.h"

#include "annotation_codec.h"
#include "modentry.h"

#include <QColor>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace modlist_serializer {

// v2 JSONL writer

namespace {

QJsonObject modlistHeader()
{
    return QJsonObject{
        {QStringLiteral("format"),  QStringLiteral("nerevarine_modlist")},
        {QStringLiteral("version"), kModlistVersion},
    };
}

QJsonObject loadOrderHeader()
{
    return QJsonObject{
        {QStringLiteral("format"),  QStringLiteral("nerevarine_loadorder")},
        {QStringLiteral("version"), kLoadOrderVersion},
    };
}

// One JSON object as a compact single-line JSONL record.
QString jsonLine(const QJsonObject &obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact))
         + QLatin1Char('\n');
}

QJsonObject separatorToJson(const ModEntry &e)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("sep"));
    o.insert(QStringLiteral("name"), e.displayName);
    if (e.bgColor.isValid())
        o.insert(QStringLiteral("bg"), e.bgColor.name(QColor::HexArgb));
    if (e.fgColor.isValid())
        o.insert(QStringLiteral("fg"), e.fgColor.name(QColor::HexArgb));
    if (e.collapsed)
        o.insert(QStringLiteral("collapsed"), true);
    return o;
}

QJsonObject modToJson(const ModEntry &e)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"),    QStringLiteral("mod"));
    o.insert(QStringLiteral("enabled"), e.checked);

    // Mid-install placeholder: same shape as a normal mod plus an
    // `installing` flag so the loader can tell a saved-mid-install row from a
    // fully-populated one. No path yet, so it's omitted.
    if (e.installStatus == 2) {
        o.insert(QStringLiteral("installing"), true);
        if (!e.customName.isEmpty())
            o.insert(QStringLiteral("name"), e.customName);
        else if (!e.displayName.isEmpty())
            o.insert(QStringLiteral("name"), e.displayName);
        if (!e.nexusUrl.isEmpty())  o.insert(QStringLiteral("url"),  e.nexusUrl);
        if (e.dateAdded.isValid())  o.insert(QStringLiteral("date"),
                                              e.dateAdded.toString(Qt::ISODate));
        // Persist the per-install token so a relaunch can match the pending
        // row back up with whatever signal arrives next. 8-4-4-4-12 hex, no
        // braces.
        if (!e.installToken.isNull())
            o.insert(QStringLiteral("token"),
                     e.installToken.toString(QUuid::WithoutBraces));
        return o;
    }

    // intendedModPath, when set, is the missing-on-this-machine path that
    // repairEmptyModPaths is papering over with a sibling. Persist that (not
    // the sibling) so the modlist stays stable across machines.
    const QString modPath = !e.intendedModPath.isEmpty()
        ? e.intendedModPath
        : e.modPath;
    if (!modPath.isEmpty())          o.insert(QStringLiteral("path"),   modPath);
    if (!e.customName.isEmpty())     o.insert(QStringLiteral("name"),   e.customName);
    if (!e.annotation.isEmpty())     o.insert(QStringLiteral("annot"),  e.annotation);
    if (!e.nexusUrl.isEmpty())       o.insert(QStringLiteral("url"),    e.nexusUrl);
    if (e.dateAdded.isValid())       o.insert(QStringLiteral("date"),
                                               e.dateAdded.toString(Qt::ISODate));
    if (!e.dependsOn.isEmpty()) {
        QJsonArray a;
        for (const QString &d : e.dependsOn) a.append(d);
        o.insert(QStringLiteral("deps"), a);
    }
    if (e.updateAvailable)           o.insert(QStringLiteral("update"),   true);
    if (e.isUtility)                 o.insert(QStringLiteral("utility"),  true);
    if (e.isFavorite)                o.insert(QStringLiteral("favorite"), true);
    if (!e.fomodChoices.isEmpty())   o.insert(QStringLiteral("fomod"),    e.fomodChoices);
    if (!e.bainChoices.isEmpty())    o.insert(QStringLiteral("bain"),     e.bainChoices);
    if (!e.videoUrl.isEmpty())       o.insert(QStringLiteral("video"),    e.videoUrl);
    if (!e.sourceUrl.isEmpty())      o.insert(QStringLiteral("source"),   e.sourceUrl);
    return o;
}

} // namespace

QString serializeModlist(const QList<ModEntry> &entries)
{
    QString out = jsonLine(modlistHeader());
    for (const ModEntry &e : entries) {
        if (e.isSeparator())
            out += jsonLine(separatorToJson(e));
        else
            out += jsonLine(modToJson(e));
    }
    return out;
}

QString serializeLoadOrder(const QStringList &plugins)
{
    QString out = jsonLine(loadOrderHeader());
    out += QStringLiteral("# Plugin load order for this game profile - one filename per line.\n"
                          "# Edit via: Mods menu → Edit Load Order…\n");
    for (const QString &p : plugins) out += p + QLatin1Char('\n');
    return out;
}

// v2 JSONL parser

namespace {

ModEntry separatorFromJson(const QJsonObject &o)
{
    ModEntry e;
    e.itemType    = QStringLiteral("separator");
    e.displayName = o.value(QStringLiteral("name")).toString();
    const QString bg = o.value(QStringLiteral("bg")).toString();
    const QString fg = o.value(QStringLiteral("fg")).toString();
    if (!bg.isEmpty())  e.bgColor = QColor(bg);
    if (!fg.isEmpty())  e.fgColor = QColor(fg);
    e.collapsed = o.value(QStringLiteral("collapsed")).toBool(false);
    return e;
}

ModEntry modFromJson(const QJsonObject &o)
{
    ModEntry e;
    e.itemType = QStringLiteral("mod");
    e.checked  = o.value(QStringLiteral("enabled")).toBool(false);

    const bool installing = o.value(QStringLiteral("installing")).toBool(false);
    if (installing) {
        e.installStatus = 2;
        e.customName    = o.value(QStringLiteral("name")).toString();
        e.displayName   = e.customName;
        e.nexusUrl      = o.value(QStringLiteral("url")).toString();
        e.dateAdded     = QDateTime::fromString(
                              o.value(QStringLiteral("date")).toString(), Qt::ISODate);
        const QString tok = o.value(QStringLiteral("token")).toString();
        if (!tok.isEmpty()) e.installToken = QUuid(tok);
        return e;
    }

    e.modPath    = o.value(QStringLiteral("path")).toString();
    e.customName = o.value(QStringLiteral("name")).toString();
    e.annotation = o.value(QStringLiteral("annot")).toString();
    e.nexusUrl   = o.value(QStringLiteral("url")).toString();
    e.dateAdded  = QDateTime::fromString(
                       o.value(QStringLiteral("date")).toString(), Qt::ISODate);

    if (o.contains(QStringLiteral("deps"))) {
        const QJsonArray a = o.value(QStringLiteral("deps")).toArray();
        for (const QJsonValue &v : a)
            if (v.isString()) e.dependsOn << v.toString();
    }
    e.updateAvailable = o.value(QStringLiteral("update")).toBool(false);
    e.isUtility       = o.value(QStringLiteral("utility")).toBool(false);
    e.isFavorite      = o.value(QStringLiteral("favorite")).toBool(false);
    e.fomodChoices    = o.value(QStringLiteral("fomod")).toString();
    e.bainChoices     = o.value(QStringLiteral("bain")).toString();
    e.videoUrl        = o.value(QStringLiteral("video")).toString();
    e.sourceUrl       = o.value(QStringLiteral("source")).toString();

    // No custom name: displayName falls back to modPath's basename, same as
    // loadModList always has.
    if (!e.customName.isEmpty()) {
        e.displayName = e.customName;
    } else if (!e.modPath.isEmpty()) {
        const int slash = e.modPath.lastIndexOf(QLatin1Char('/'));
        e.displayName = (slash >= 0) ? e.modPath.mid(slash + 1) : e.modPath;
    }
    return e;
}

QList<ModEntry> parseV2(const QStringList &lines)
{
    QList<ModEntry> out;
    // Skip header + blank lines between records. Header is line 0 by
    // construction, but scan for it in case the file was hand-edited.
    bool sawHeader = false;
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        if (!line.startsWith(QLatin1Char('{'))) continue;

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) continue;
        const QJsonObject o = doc.object();

        if (!sawHeader && o.value(QStringLiteral("format")).toString()
                              == QStringLiteral("nerevarine_modlist")) {
            sawHeader = true;
            continue;
        }
        const QString t = o.value(QStringLiteral("type")).toString();
        if (t == QStringLiteral("sep"))      out << separatorFromJson(o);
        else if (t == QStringLiteral("mod")) out << modFromJson(o);
        // Unknown types dropped silently: a future v3 record type just gets
        // skipped here instead of crashing a v2 reader.
    }
    return out;
}

// v1 (legacy tab) parser
//
// Same logic as pre-v2 saveModList, byte-for-byte. Keep it until we're sure
// no user file in the wild is still on the legacy format, so first launch
// under v2 code loads the existing modlist without manual conversion.

ModEntry separatorFromLegacyLine(const QString &line)
{
    ModEntry e;
    e.itemType = QStringLiteral("separator");
    QString rest = line.mid(2);   // drop "# "

    static const QRegularExpression colorRe(
        QStringLiteral(R"(<color>(#[0-9a-fA-F]+)</color>)"));
    static const QRegularExpression fgColorRe(
        QStringLiteral(R"(<fgcolor>(#[0-9a-fA-F]+)</fgcolor>)"));
    static const QRegularExpression stripTagsRe(
        QStringLiteral(R"(<color>#[0-9a-fA-F]+</color>|<fgcolor>#[0-9a-fA-F]+</fgcolor>|<collapsed>\d+</collapsed>)"));

    QColor bg(55, 55, 75);
    QColor fg(Qt::white);
    {
        auto it = colorRe.globalMatch(rest);
        QRegularExpressionMatch last;
        while (it.hasNext()) last = it.next();
        if (last.hasMatch()) bg = QColor(last.captured(1));
    }
    {
        auto it = fgColorRe.globalMatch(rest);
        QRegularExpressionMatch last;
        while (it.hasNext()) last = it.next();
        if (last.hasMatch()) fg = QColor(last.captured(1));
    }
    e.collapsed = rest.contains(QStringLiteral("<collapsed>1</collapsed>"));
    QString name = rest;
    name.remove(stripTagsRe);
    e.displayName = name.trimmed();
    e.bgColor = bg;
    e.fgColor = fg;
    return e;
}

ModEntry modFromLegacyLine(const QString &line)
{
    ModEntry e;
    e.itemType = QStringLiteral("mod");
    e.checked  = (line[0] == QLatin1Char('+'));

    const QStringList parts = line.mid(2).split(QLatin1Char('\t'));
    e.modPath    = parts.value(0);
    e.customName = parts.value(1);
    e.annotation = parts.size() > 2 ? decodeAnnot(parts.value(2)) : QString();
    e.nexusUrl   = parts.value(3);
    e.dateAdded  = parts.size() > 4
        ? QDateTime::fromString(parts.value(4), Qt::ISODate) : QDateTime();
    // parts[5] reserved
    if (parts.size() > 6 && !parts.value(6).isEmpty()) {
        e.dependsOn = parts.value(6).split(QLatin1Char(','), Qt::SkipEmptyParts);
    }
    e.updateAvailable = (parts.size() > 7 && parts.value(7).toInt() == 1);
    e.isUtility       = (parts.size() > 8 && parts.value(8).toInt() == 1);
    e.isFavorite      = (parts.size() > 9 && parts.value(9).toInt() == 1);
    e.fomodChoices    = parts.size() > 10 ? parts.value(10) : QString();
    e.videoUrl        = parts.size() > 11 ? parts.value(11) : QString();
    e.sourceUrl       = parts.size() > 12 ? parts.value(12) : QString();

    // Mid-install placeholder shape: v1 saver writes "- \tname\t\turl\tdate"
    // for status==2 rows. parts[0] empty, [1] name, [3] url, [4] date.
    if (e.modPath.isEmpty() && parts.size() >= 5
        && !e.customName.isEmpty() && !e.nexusUrl.isEmpty()) {
        e.installStatus = 2;
        e.displayName   = e.customName;
        return e;
    }

    if (!e.customName.isEmpty()) {
        e.displayName = e.customName;
    } else if (!e.modPath.isEmpty()) {
        const int slash = e.modPath.lastIndexOf(QLatin1Char('/'));
        e.displayName = (slash >= 0) ? e.modPath.mid(slash + 1) : e.modPath;
    }
    return e;
}

QList<ModEntry> parseV1(const QStringList &lines)
{
    QList<ModEntry> out;
    for (const QString &raw : lines) {
        const QString line = raw;   // already split, may have a stray \r
        QString clean = line;
        if (clean.endsWith(QLatin1Char('\r'))) clean.chop(1);
        if (clean.isEmpty()) continue;
        if (clean.startsWith(QStringLiteral("# "))) {
            out << separatorFromLegacyLine(clean);
        } else if (clean.size() >= 2
                && (clean[0] == QLatin1Char('+') || clean[0] == QLatin1Char('-'))
                && clean[1] == QLatin1Char(' ')) {
            out << modFromLegacyLine(clean);
        }
        // anything else skipped (matches old loadModList)
    }
    return out;
}

bool looksLikeV2Header(const QString &line)
{
    // Header is JSON; sniff the format string instead of parsing every line.
    // Tolerate leading whitespace.
    return line.contains(QStringLiteral("\"format\""))
        && line.contains(QStringLiteral("\"nerevarine_modlist\""));
}

bool looksLikeV2LoadOrderHeader(const QString &line)
{
    return line.contains(QStringLiteral("\"format\""))
        && line.contains(QStringLiteral("\"nerevarine_loadorder\""));
}

} // namespace

QList<ModEntry> parseModlist(const QString &contents)
{
    if (contents.isEmpty()) return {};
    const QStringList lines = contents.split(QLatin1Char('\n'));

    // First non-empty line tells us v2 vs legacy.
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        if (looksLikeV2Header(line))  return parseV2(lines);
        return parseV1(lines);   // first non-empty line was a legacy row
    }
    return {};
}

QStringList parseLoadOrder(const QString &contents)
{
    QStringList out;
    if (contents.isEmpty()) return out;
    const QStringList lines = contents.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        QString line = raw;
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())                      continue;
        if (trimmed.startsWith(QLatin1Char('#')))   continue;
        // v2 header is a JSON object on its own line - skip it, it's not a
        // plugin name. Legacy files have no `{`-prefixed lines, so no false hit.
        if (trimmed.startsWith(QLatin1Char('{'))
            && looksLikeV2LoadOrderHeader(trimmed))
            continue;
        out << trimmed;
    }
    return out;
}

} // namespace modlist_serializer
