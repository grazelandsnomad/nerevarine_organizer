#include "fomod_conditions.h"

#include <QXmlStreamReader>

#include <tuple>

namespace fomod {
namespace {

bool is(const QXmlStreamReader &xml, const char *name)
{
    return xml.name().compare(QLatin1String(name), Qt::CaseInsensitive) == 0;
}

QString attr(const QXmlStreamReader &xml, const char *name)
{
    return xml.attributes().value(QLatin1String(name)).toString();
}

bool recommends(const QString &type)
{
    return type == QLatin1String("Recommended") || type == QLatin1String("Required");
}

// "1.11.0.0" as four numbers, missing parts 0. nullopt for anything that is
// not dotted numbers, which leaves the leaf Unknown rather than guessed.
std::optional<std::tuple<int, int, int, int>> parseVersion(const QString &s)
{
    const QStringList parts = s.trimmed().split(QLatin1Char('.'));
    if (parts.isEmpty() || parts.size() > 4) return std::nullopt;
    int v[4] = {0, 0, 0, 0};
    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false;
        v[i] = parts[i].toInt(&ok);
        if (!ok) return std::nullopt;
    }
    return std::tuple(v[0], v[1], v[2], v[3]);
}

// What an evaluation looked at, for the verdict's lists and its fromFacts.
struct Trace {
    QStringList activeFound;     // File leaves asking "Active" that held
    QStringList activeMissing;   // ... and that did not
    bool        fact = false;    // a File or Game leaf answered True or False
    bool        game = false;    // a Game leaf answered
};

Truth eval(const FomodCondition &c, const Facts &facts, Trace *trace)
{
    using Kind = FomodCondition::Kind;
    switch (c.kind) {
    case Kind::All:
    case Kind::Any: {
        // Every child, no short cut: the lists want every file asked about,
        // and the cost is a handful of lookups.
        bool anyTrue = false, anyFalse = false, anyUnknown = false;
        for (const FomodCondition &child : c.children) {
            const Truth t = eval(child, facts, trace);
            anyTrue    |= (t == Truth::True);
            anyFalse   |= (t == Truth::False);
            anyUnknown |= (t == Truth::Unknown);
        }
        if (c.kind == Kind::All)
            return anyFalse ? Truth::False : anyUnknown ? Truth::Unknown : Truth::True;
        return anyTrue ? Truth::True : anyUnknown ? Truth::Unknown : Truth::False;
    }
    case Kind::File: {
        if (!facts.file) return Truth::Unknown;
        const std::optional<FileState> have = facts.file(c.subject);
        if (!have) return Truth::Unknown;
        FileState want = FileState::Active;
        if (c.value.compare(QLatin1String("Missing"), Qt::CaseInsensitive) == 0)
            want = FileState::Missing;
        else if (c.value.compare(QLatin1String("Inactive"), Qt::CaseInsensitive) == 0)
            want = FileState::Inactive;
        const bool holds = (*have == want);
        if (trace) {
            trace->fact = true;
            if (want == FileState::Active)
                (holds ? trace->activeFound : trace->activeMissing) << c.subject;
        }
        return holds ? Truth::True : Truth::False;
    }
    case Kind::Flag:
        // Exact, as the installer wrote it; a flag nobody raised is "".
        return facts.flags.value(c.subject) == c.value ? Truth::True : Truth::False;
    case Kind::Game: {
        const auto want = parseVersion(c.value);
        if (!want || !facts.game.valid) return Truth::Unknown;
        if (trace) { trace->fact = true; trace->game = true; }
        const auto have = std::tuple(facts.game.major, facts.game.minor,
                                     facts.game.build, facts.game.sub);
        return have >= *want ? Truth::True : Truth::False;
    }
    case Kind::Manager:
        return Truth::True;
    case Kind::Other:
        return Truth::Unknown;
    }
    return Truth::Unknown;
}

} // namespace

FomodCondition readDependencies(QXmlStreamReader &xml)
{
    using Kind = FomodCondition::Kind;
    FomodCondition c;
    c.kind = attr(xml, "operator").compare(QLatin1String("Or"), Qt::CaseInsensitive) == 0
                 ? Kind::Any : Kind::All;
    while (xml.readNextStartElement()) {
        if (is(xml, "dependencies")) {
            c.children << readDependencies(xml);
            continue;
        }
        FomodCondition leaf;
        if (is(xml, "fileDependency")) {
            leaf.kind    = Kind::File;
            leaf.subject = attr(xml, "file");
            leaf.value   = attr(xml, "state");
            if (leaf.value.isEmpty()) leaf.value = QStringLiteral("Active");
        } else if (is(xml, "flagDependency")) {
            leaf.kind    = Kind::Flag;
            leaf.subject = attr(xml, "flag");
            leaf.value   = attr(xml, "value");
        } else if (is(xml, "gameDependency")) {
            leaf.kind  = Kind::Game;
            leaf.value = attr(xml, "version");
        } else if (is(xml, "fommDependency")) {
            leaf.kind  = Kind::Manager;
            leaf.value = attr(xml, "version");
        } else {
            leaf.kind    = Kind::Other;
            leaf.subject = xml.name().toString();
        }
        xml.skipCurrentElement();
        c.children << leaf;
    }
    return c;
}

void readTypeDescriptor(QXmlStreamReader &xml, FomodPlugin &plugin)
{
    while (xml.readNextStartElement()) {
        if (is(xml, "type")) {
            plugin.type = attr(xml, "name");
            xml.skipCurrentElement();
            continue;
        }
        if (!is(xml, "dependencyType")) {
            xml.skipCurrentElement();
            continue;
        }
        while (xml.readNextStartElement()) {
            if (is(xml, "defaultType")) {
                plugin.type = attr(xml, "name");
                xml.skipCurrentElement();
                continue;
            }
            if (!is(xml, "patterns")) {
                xml.skipCurrentElement();
                continue;
            }
            while (xml.readNextStartElement()) {
                if (!is(xml, "pattern")) {
                    xml.skipCurrentElement();
                    continue;
                }
                FomodTypePattern p;
                while (xml.readNextStartElement()) {
                    if (is(xml, "dependencies")) {
                        p.when = readDependencies(xml);
                    } else if (is(xml, "type")) {
                        p.type = attr(xml, "name");
                        xml.skipCurrentElement();
                    } else {
                        xml.skipCurrentElement();
                    }
                }
                plugin.typePatterns << p;
            }
        }
    }
}

Truth evaluate(const FomodCondition &condition, const Facts &facts)
{
    return eval(condition, facts, nullptr);
}

TypeVerdict resolveType(const FomodPlugin &plugin, const Facts &facts)
{
    TypeVerdict v;
    v.type = plugin.type;
    if (plugin.typePatterns.isEmpty()) return v;

    bool unknownBefore = false;
    bool fact          = false;
    for (int i = 0; i < plugin.typePatterns.size(); ++i) {
        const FomodTypePattern &p = plugin.typePatterns[i];
        Trace t;
        const Truth r = eval(p.when, facts, &t);
        fact |= t.fact;
        if (r == Truth::Unknown) {
            unknownBefore = true;
            continue;
        }
        if (r == Truth::True) {
            v.type    = p.type;
            v.pattern = i;
            v.found   = t.activeFound;
            v.byGame  = t.game;
            break;
        }
        v.lacked << t.activeMissing;
        if (recommends(p.type)) v.lackedToRecommend << t.activeMissing;
    }
    v.fromFacts = fact && !unknownBefore;
    v.found.removeDuplicates();
    v.lacked.removeDuplicates();
    v.lackedToRecommend.removeDuplicates();
    return v;
}

} // namespace fomod
