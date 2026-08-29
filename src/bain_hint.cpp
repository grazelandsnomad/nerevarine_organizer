#include "bain_hint.h"
#include "mod_aliases.h"
#include "mod_match.h"

#include <QRegularExpression>

#include <algorithm>

namespace bain {
namespace {

// The BAIN index and whatever punctuation follows it: "03- ", "01 - ", "02_".
const QRegularExpression &indexRe()
{
    static const QRegularExpression re(
        QStringLiteral("^\\s*\\d+[A-Za-z]?\\s*[ ._\\-\u2013\u2014)]+\\s*"));
    return re;
}

// A marker is REQUIRED. Without one a folder name is just a folder name, and
// reading a mod out of every "01 Stratified Rocks" is how a picker starts
// unticking things nobody asked it to.
//
// "fix" is deliberately absent: "03 Chuzei Fix" fixes a vanilla mesh, and the
// target would come out as "Chuzei". So are "version", "edition", "replacer"
// and "support" - "01 MWSE Version" is a build variant, not a dependency.
const QRegularExpression &trailingMarkerRe()
{
    static const QRegularExpression re(
        QStringLiteral("\\s*[-\u2013\u2014]?\\s*\\b(?:patch(?:es)?|add[- ]?on|"
                       "compatibility|compat)\\b\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// The same markers at the front, joined to the target by "for", "with" or a
// dash.
//
// A bare "for"/"with" is handled separately, by bareJoinRe below - it is not
// a marker, because a marker is itself evidence that what follows names a
// mod and "for" is not. What holds "01 Icons for OpenMW" back there is the
// alias table, not a refusal to split; do not "simplify" that gate away.
const QRegularExpression &leadingMarkerRe()
{
    static const QRegularExpression re(
        QStringLiteral("^\\s*(?:(?:patch(?:es)?|add[- ]?on|compatibility|compat)"
                       "\\s*(?:for|with|[-\u2013\u2014])|"
                       "(?:to|for)\\s+use\\s+with|compatible\\s+with)\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// A bare "for"/"with" joining two things: "Icons for OpenMW SSQN", "Riders -
// to use with OAAB Grazelands". Whitespace is required on BOTH sides, so a
// name that merely opens with "For" - "02 For WIP Detailed Correct UV Rocks" -
// is not a claim about a mod.
//
// This is the loosest reading in the file, and it is only allowed to say
// anything because targetOfPackageName makes the alias table vouch for the
// tail before acting on it.
const QRegularExpression &bareJoinRe()
{
    static const QRegularExpression re(QStringLiteral("\\s(?:for|with)\\s"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Names that are never a modlist answer, in EITHER direction.
//
// Vetoing the positive is the point: "03 OpenMW Patch" otherwise matches
// "OpenMW Skyrim Style Quest Notifications" and reports a dependency that
// isn't one. Engines and tooling live in the game folder, not the modlist, so
// the modlist cannot answer for them - which is also why a miss on them means
// nothing.
bool isStopWord(const QString &s)
{
    static const QSet<QString> kStop = {
        // engines and out-of-manager tooling
        "openmw", "mwse", "mge", "mge xe", "mgexe", "mcp",
        "morrowind code patch", "tes3mp", "lua",
        // base game
        "morrowind", "tribunal", "bloodmoon",
        // installer boilerplate - the same vocabulary fomod_hint.cpp uses
        "core", "core files", "main", "main files", "data files",
        "optional", "optional files", "resources", "mod resources", "vanilla",
    };
    return kStop.contains(s.trimmed().toLower());
}

} // namespace

PackageTarget targetOfPackageName(const QString &packageName,
                                  const QString &ownModName)
{
    PackageTarget out;

    QString cleaned = packageName;
    cleaned.remove(indexRe());
    // Parenthesised qualifiers are about the build, not the mod:
    // "02 Ashfall Compatibility (MWSE)", "02 BCOM Patch (Optional)".
    cleaned.remove(QRegularExpression(QStringLiteral("\\s*\\([^)]*\\)")));
    cleaned = cleaned.trimmed();
    if (cleaned.isEmpty()) return out;

    // Strip a marker at most twice, so "Patch - TR Patch" gives up "TR".
    QString residue = cleaned;
    bool sawMarker = false;
    for (int pass = 0; pass < 2; ++pass) {
        const QString before = residue;
        residue.remove(leadingMarkerRe());
        residue.remove(trailingMarkerRe());
        residue = residue.trimmed();
        if (residue == before) break;
        sawMarker = true;
    }
    // No marker. A bare "for"/"with" still points at something - take the tail
    // and let the alias gate below decide whether it is a mod.
    bool viaBareJoin = false;
    if (!sawMarker) {
        const QRegularExpressionMatch m = bareJoinRe().match(cleaned);
        if (!m.hasMatch()) return out;                 // no marker, no join
        residue     = cleaned.mid(m.capturedEnd()).trimmed();
        viaBareJoin = true;
    }
    if (residue.isEmpty()) return out;

    // A stop-word target is silence, not a verdict either way.
    if (isStopWord(residue)) return out;

    // The alias table is the WHOLE licence for the bare-join path. A marker
    // ("Patch for X") is itself evidence that X names a mod; "for" alone is
    // not, so the table is the only evidence there is - which is exactly what
    // the header says about folder names carrying no requirement sentence.
    //
    // A stop-word cannot vouch even when the table knows it: "01 Grass for
    // MGEXE and OpenMW" would otherwise be judged against a tool that lives in
    // the game folder and can never appear in a modlist, and unticking it
    // takes the user's grass with it. Measured on a real corpus, that is the
    // one false untick this gate prevents.
    if (viaBareJoin) {
        bool vouched = false;
        for (const QString &tok :
             residue.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (mod_aliases::isKnownMod(tok) && !isStopWord(tok)) {
                vouched = true;
                break;
            }
        }
        if (!vouched) return out;
    }

    const QString phrase = mod_match::titleRun(residue);
    if (isStopWord(phrase)) return out;

    // A package that names its own mod is not naming another one.
    if (!ownModName.isEmpty()
        && !mod_match::installedUnderAnyName(residue, {ownModName}).isEmpty())
        return out;

    out.display = phrase.isEmpty() ? residue : phrase;

    // Order matters, first hit wins:
    //   1. the residue itself
    //   2. any residue token the alias table vouches for, which is what saves
    //      "01 - Vanilla GITD Patch" (residue is a qualifier plus an acronym)
    //   3. the whole cleaned name, which is what saves "01 - Patch for
    //      Purists" - a mod literally called Patch for Purists
    out.candidates << residue;
    for (const QString &tok : residue.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        if (mod_aliases::isKnownMod(tok) && !out.candidates.contains(tok))
            out.candidates << tok;
    if (!out.candidates.contains(cleaned)) out.candidates << cleaned;

    // The bar for declaring a MISS. Several title-cased words read as a name;
    // a single word only does when the alias table already knows it. This is
    // deliberately tighter than fomod::requiredMods, which takes any all-caps
    // token: there the acronym came out of a stated requirement sentence,
    // which is itself evidence that it names a mod. A folder name carries no
    // such sentence, so the table is the only evidence there is.
    //
    // The bare-join path is already past that bar: the table vouched for the
    // tail above, which is a stronger statement than "two title-cased words".
    out.confident = viaBareJoin
        || (!phrase.isEmpty()
            && (phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() >= 2
                || mod_aliases::isKnownMod(phrase)));
    return out;
}

PackageVerdict judgeOne(const QString      &packageName,
                        const QStringList  &foreignMasters,
                        const QStringList  &installedModNames,
                        const QSet<QString> &availablePluginsLower,
                        const QString      &ownModName)
{
    PackageVerdict v;

    // Hard evidence first. An unsatisfiable foreign master outranks anything
    // the name says: OpenMW will refuse to load the plugin, and the reason
    // names the file rather than a guessed mod.
    if (!availablePluginsLower.isEmpty()) {
        for (const QString &m : foreignMasters) {
            if (!availablePluginsLower.contains(m.toLower())) {
                v.state  = PackageVerdict::State::Missing;
                v.source = PackageVerdict::Source::Master;
                v.master = m;
                v.target = targetOfPackageName(packageName, ownModName).display;
                return v;
            }
        }
    }
    // Masters that all resolve are evidence of nothing. Fall through.

    const PackageTarget t = targetOfPackageName(packageName, ownModName);
    if (t.candidates.isEmpty()) return v;          // Unknown

    v.target = t.display;

    for (const QString &cand : t.candidates) {
        const QString hit =
            mod_match::installedUnderAnyName(cand, installedModNames);
        if (!hit.isEmpty()) {
            v.state   = PackageVerdict::State::Installed;
            v.source  = PackageVerdict::Source::Name;
            v.matched = hit;
            return v;
        }
    }

    // Nothing answered. That is only "not installed" when the name was clear
    // enough to be asking about a mod at all, and when there is a modlist to
    // ask - an empty one is no evidence about anything.
    if (t.confident && !installedModNames.isEmpty()) {
        v.state  = PackageVerdict::State::Missing;
        v.source = PackageVerdict::Source::Name;
    }
    return v;
}

QList<PackageVerdict> judgePackages(const QList<Package> &packages,
                                    const QStringList    &installedModNames,
                                    const QSet<QString>  &availablePluginsLower,
                                    const QString        &ownModName)
{
    QList<PackageVerdict> out;
    out.reserve(packages.size());

    for (int i = 0; i < packages.size(); ++i)
        out << judgeOne(packages[i].name, foreignMasters(packages, i),
                        installedModNames, availablePluginsLower, ownModName);

    // Never leave nothing ticked. stage() returns "" for an empty selection
    // and the caller reads "" as a cancel, so a pass confident about every
    // package would silently abort the install instead of recommending
    // anything. If it has no good news at all, it has nothing worth saying.
    const bool anythingLeft =
        std::any_of(out.cbegin(), out.cend(), [](const PackageVerdict &v) {
            return v.state != PackageVerdict::State::Missing;
        });
    if (!anythingLeft)
        for (PackageVerdict &v : out) v = PackageVerdict{};

    return out;
}

} // namespace bain
