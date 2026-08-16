#include "mod_aliases.h"

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
        // -- Fallout / Starfield --------------------------------------
        {"F4SE", "Fallout 4 Script Extender"},
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

} // namespace mod_aliases
