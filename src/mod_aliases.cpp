#include "mod_aliases.h"

#include <QRegularExpression>

#include <algorithm>

#include <QHash>
#include <QList>

namespace mod_aliases {
namespace {

// Each row is one mod, written every way the scene writes it. Order within a
// row does not matter; every spelling maps to every other.
const QList<QStringList> &table()
{
    static const QList<QStringList> kTable = {
        // -- Skyrim ----------------------------------------------------
        {"SMIM", "Static Mesh Improvement Mod"},
        {"CACO", "Complete Alchemy and Cooking Overhaul", "Complete Alchemy"},
        {"USSEP", "Unofficial Skyrim Special Edition Patch"},
        {"USLEEP", "Unofficial Skyrim Legendary Edition Patch"},
        {"SKSE", "SKSE64", "Skyrim Script Extender"},
        {"SkyUI"},
        {"SkyPatcher"},
        {"CDF", "Container Distribution Framework"},
        {"BOS", "Base Object Swapper"},
        {"DynDOLOD", "Dynamic Distant Objects LOD"},
        {"FNIS", "Fores New Idles in Skyrim"},
        {"XPMSE", "XPMSSE", "XP32 Maximum Skeleton"},
        {"MCM Helper"},
        {"LOTD", "Legacy of the Dragonborn"},
        {"COTN", "Cities of the North"},
        {"3DNPC", "Interesting NPCs"},
        {"RSC", "Realistic Water Two"},
        {"ELFX", "Enhanced Lights and FX"},
        {"JKs Skyrim", "JK's Skyrim"},
        // -- Morrowind / OpenMW ---------------------------------------
        {"MWSE", "Morrowind Script Extender"},
        {"MGE XE", "MGEXE", "Morrowind Graphics Extender XE"},
        {"TR", "Tamriel Rebuilt"},
        {"OAAB", "OAAB_Data", "OAAB Data"},
        {"PT", "Project Tamriel"},
        {"Tamriel_Data", "Tamriel Data"},
        {"GITD", "Glow in the Dahrk"},
        {"BCOM", "Beautiful Cities of Morrowind"},
        {"MOP", "Morrowind Optimization Patch"},
        {"SHOTN", "Skyrim Home of the Nords"},
        {"SSQN", "Skyrim Style Quest Notifications"},
        {"TOTSP", "Tomb of the Snow Prince", "Solstheim - Tomb of the Snow Prince"},
        // One spelling, no acronym worth the risk. Here so isKnownMod() can
        // vouch for a single-word name: a BAIN package called "98 Ashfall
        // Compatibility" leaves "Ashfall" behind, and one word is only enough
        // to act on when the table already knows it.
        {"Project Atlas", "Atlas"},
        {"Ashfall"},
        // -- Fallout / Starfield --------------------------------------
        {"F4SE", "Fallout 4 Script Extender"},
        // PRP is the Previs Repair Pack and, in the Fallout 4 scene, nothing
        // else - which is the bar this table sets. Here because installers
        // offer it as an alternative to the game's own previs data and the
        // wizard has to know whether the user actually has it: Vehicle
        // Overhaul Continued defaults its "Previs Plugins" group to "PRP v81
        // Previs", which on a list without PRP writes plugins keyed to a
        // framework that is not there.
        {"PRP", "Previs Repair Pack"},
        {"NVSE", "New Vegas Script Extender"},
        {"SFSE", "Starfield Script Extender"},
        {"JIP LN", "JIP LN NVSE"},
    };
    return kTable;
}

// Lowercased spelling -> index of its row.
const QHash<QString, int> &index()
{
    static const QHash<QString, int> kIndex = [] {
        QHash<QString, int> m;
        for (int i = 0; i < table().size(); ++i)
            for (const QString &n : table()[i]) m.insert(n.toLower(), i);
        return m;
    }();
    return kIndex;
}

} // namespace

QStringList aliasesFor(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key.isEmpty()) return {};

    const auto it = index().constFind(key);
    if (it == index().constEnd()) return {};

    QStringList out;
    for (const QString &n : table()[*it])
        if (n.toLower() != key) out << n;
    return out;
}

bool isKnownMod(const QString &name)
{
    const QString key = name.trimmed().toLower();
    if (key.isEmpty()) return false;
    return index().constFind(key) != index().constEnd();
}

QString frameworkForToken(const QString &token)
{
    // The full FRAMEWORK name a variant-marker token stands for.
    //
    // Not another alias lookup - BOS already sits in the table above, and in
    // MODDING it reads Base Object Swapper almost without exception (the
    // Brotherhood of Steel sense lives in lore and content names, not in
    // installer markers). What this answers is narrower: is the token a
    // FRAMEWORK at all. The variant logic ("<Mod>" vs "<Mod> (No BOS)") must
    // only fire for frameworks, whose presence genuinely decides which half
    // works - not for every acronym the table knows.
    const QString t = token.trimmed().toLower();
    if (t == QLatin1String("bos"))
        return QStringLiteral("Base Object Swapper");
    if (t == QLatin1String("cdf"))
        return QStringLiteral("Container Distribution Framework");
    // Full names answer for themselves, so a spelled-out marker works too.
    for (const QString &full : frameworkPreference())
        if (t == full.toLower()) return full;
    return {};
}

QStringList frameworkPreference()
{
    // SkyPatcher first: it is depended upon by far more mods than the others
    // here, so it is the most exercised in the field. Not a stability claim -
    // see the header.
    return {QStringLiteral("SkyPatcher"),
            QStringLiteral("Container Distribution Framework"),
            QStringLiteral("Base Object Swapper")};
}

QStringList expand(const QStringList &names)
{
    QStringList out;
    for (const QString &n : names) {
        const QString t = n.trimmed();
        if (t.isEmpty() || out.contains(t, Qt::CaseInsensitive)) continue;
        out << t;
        for (const QString &a : aliasesFor(t))
            if (!out.contains(a, Qt::CaseInsensitive)) out << a;
    }
    return out;
}


QString knownModIn(const QString &text)
{
    // Longest entry first, so a name containing a shorter entry as a word is
    // not claimed by the shorter one.
    static const QStringList kNames = [] {
        QStringList out;
        for (const QStringList &row : table()) out += row;
        std::sort(out.begin(), out.end(),
                  [](const QString &a, const QString &b) {
                      return a.size() > b.size();
                  });
        return out;
    }();

    const QString subject = text.trimmed();
    if (subject.isEmpty()) return {};

    for (const QString &n : kNames) {
        // An ALL-CAPS entry is an acronym, and case is the whole of its
        // safety: "PRP" is a mod, and a three-letter needle matched loosely is
        // how a short name starts hitting ordinary words. The same argument
        // mod_match::installedUnderAnyName makes for anchoring, and
        // lore_overrides::protectedTermsFor for Blight over blight.
        const bool acronym = (n == n.toUpper());
        const QRegularExpression re(
            QStringLiteral("\\b") + QRegularExpression::escape(n) + QStringLiteral("\\b"),
            acronym ? QRegularExpression::NoPatternOption
                    : QRegularExpression::CaseInsensitiveOption);
        if (re.match(subject).hasMatch()) return n;
    }
    return {};
}
} // namespace mod_aliases
