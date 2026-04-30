// tests/test_openmw_config_writer_fuzz.cpp
//
// Property/fuzz tests for openmw::renderOpenMWConfig.
//
// Complements the golden-file tests in test_openmw_config_writer.cpp.
// The golden tests pin specific historical regressions; this file
// hammers the writer with random (mods, loadOrder, existingCfg) inputs
// and checks STRUCTURAL invariants on the output. Each iteration's
// inputs are derived from a deterministic seed printed at run start so
// any failure is reproducible.
//
// Invariants checked per iteration:
//   I1  BEGIN/END bracketing exactly once, BEGIN before END.
//   I2  No content= filename appears twice in the entire output.
//   I3  No groundcover= filename appears twice in the entire output.
//   I4  Suppressed plugins absent from BOTH content= and groundcover=.
//   I5  Disabled mods' plugins absent (the writer drops their content=
//       even though their filenames go into allManagedContent, because
//       Phase B only emits availableSet entries which are enabled).
//   I6  Load-order respected: for managed plugins X, Y both in
//       loadOrder with X before Y, X appears before Y in content=.
//   I7  Every enabled+installed mod's plugin dir becomes a data= line
//       inside the managed section.
//   I8  Idempotence: render(render(input)) == render(input) bytewise.
//   I9  Suppressed plugins' filenames don't end up as data=.
//
// Build + run:
//   ./build/tests/test_openmw_config_writer_fuzz
//   ./build/tests/test_openmw_config_writer_fuzz --seed=1234567

#include "openmwconfigwriter.h"

#include <QCoreApplication>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <iostream>
#include <random>
#include <string>

using openmw::ConfigMod;
using openmw::renderOpenMWConfig;

namespace {

int s_passed = 0;
int s_failed = 0;
uint32_t s_currentSeed = 0;

struct Scenario {
    QList<ConfigMod>           mods;
    QStringList                loadOrder;
    QString                    existingCfg;
    QSet<QString>              allEnabledInstalledFilenames;
    QSet<QString>              allDisabledOrUninstalledFilenames;
    QSet<QString>              allSuppressed;
    QSet<QString>              allGroundcover;
    QList<QPair<QString, int>> enabledPluginDirs; // (path, modIdx) for I7
};

// Deterministic mod-and-cfg generator. Same seed → same scenario.
class Gen {
public:
    explicit Gen(uint32_t seed) : m_rng(seed) {}

    int     uniform(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(m_rng); }
    bool    coin(double p = 0.5) { return std::uniform_real_distribution<double>(0.0, 1.0)(m_rng) < p; }
    QString pluginName(int n) {
        // Mix of .esp and .esm so the within-mod sort path gets exercised.
        const char *exts[] = {".esp", ".esm", ".omwaddon", ".omwscripts"};
        return QString("Plugin%1%2").arg(n).arg(exts[uniform(0, 3)]);
    }

    Scenario build()
    {
        Scenario sc;
        const int modCount = uniform(0, 8);
        int nextPluginNum = 0;

        for (int i = 0; i < modCount; ++i) {
            ConfigMod m;
            m.installed = coin(0.85);            // mostly installed
            m.enabled   = m.installed && coin(0.7); // ~60% of mods enabled

            // Decide resource-only upfront so we don't populate the "managed
            // plugins" sets with plugins the writer can never see.
            const bool resourceOnly = m.installed && coin(0.15);
            const int  dirCount     = (m.installed && !resourceOnly) ? uniform(1, 3) : 0;
            for (int d = 0; d < dirCount; ++d) {
                const QString dir = QString("/mods/M%1/d%2").arg(i).arg(d);
                QStringList files;
                const int fileCount = uniform(0, 4);
                for (int f = 0; f < fileCount; ++f) {
                    const QString name = pluginName(nextPluginNum++);
                    files << name;
                    if (m.enabled && m.installed)
                        sc.allEnabledInstalledFilenames.insert(name);
                    else
                        sc.allDisabledOrUninstalledFilenames.insert(name);

                    // 10% chance to mark as groundcover; mutually exclusive
                    // with suppression in the same dir.
                    if (coin(0.1)) {
                        m.groundcoverFiles.insert(name);
                        sc.allGroundcover.insert(name);
                    } else if (coin(0.05)) {
                        m.suppressedPlugins.insert(name);
                        sc.allSuppressed.insert(name);
                    }
                }
                m.pluginDirs.append({dir, files});
                if (m.enabled && m.installed)
                    sc.enabledPluginDirs.append({dir, i});
            }

            if (resourceOnly) {
                const int rootCount = uniform(1, 2);
                for (int r = 0; r < rootCount; ++r)
                    m.resourceRoots << QString("/mods/M%1/res%2").arg(i).arg(r);
            }

            sc.mods.append(m);
        }

        // Build loadOrder from a permutation of the enabled plugin filenames
        // that aren't suppressed/groundcover, plus a few "unknown" entries
        // that exercise the "load order references unknown plugin" path.
        QStringList managedContentPool;
        for (const QString &n : sc.allEnabledInstalledFilenames) {
            if (sc.allSuppressed.contains(n)) continue;
            if (sc.allGroundcover.contains(n)) continue;
            managedContentPool << n;
        }
        // Shuffle (deterministic, mt19937-driven).
        for (int i = managedContentPool.size() - 1; i > 0; --i)
            std::swap(managedContentPool[i], managedContentPool[uniform(0, i)]);

        sc.loadOrder = managedContentPool;
        // 30% of the time, drop one entry to exercise the modlist-fallback path
        // (writer falls back to availableContentOrdered for plugins missing
        // from loadOrder).
        if (!sc.loadOrder.isEmpty() && coin(0.3))
            sc.loadOrder.removeAt(uniform(0, sc.loadOrder.size() - 1));
        // 20% of the time, append a phantom "unknown" plugin name that
        // doesn't correspond to any installed mod - must be ignored.
        if (coin(0.2))
            sc.loadOrder << QString("Unknown_%1.esp").arg(uniform(0, 999));

        // Optionally generate an existingCfg with random preamble + an
        // external data= line + previous content= entries (some matching our
        // managed plugins, some external base-game plugins).
        if (coin(0.7)) {
            QStringList lines;
            if (coin(0.5)) lines << "# user comment";
            if (coin(0.7)) lines << "fallback=PixelLighting,1";
            if (coin(0.7)) lines << "data=/usr/share/games/morrowind/Data Files";
            // Base-game externals.
            const QStringList vanilla = {"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"};
            for (const QString &v : vanilla)
                if (coin(0.6)) lines << "content=" + v;
            // Some prior content= for managed plugins (out of order - writer
            // must rewrite).
            for (const QString &n : sc.allEnabledInstalledFilenames)
                if (coin(0.3)) lines << "content=" + n;
            // CRLF for variety.
            const QString sep = coin(0.2) ? "\r\n" : "\n";
            sc.existingCfg = lines.join(sep);
            if (coin(0.5)) sc.existingCfg += sep; // trailing newline sometimes
        }

        return sc;
    }

private:
    std::mt19937 m_rng;
};

// -- Invariant checks ---

QStringList contentValues(const QString &out)
{
    QStringList r;
    for (const QString &l : out.split('\n'))
        if (l.startsWith(QStringLiteral("content="))) r << l.mid(8);
    return r;
}

QStringList groundcoverValues(const QString &out)
{
    QStringList r;
    for (const QString &l : out.split('\n'))
        if (l.startsWith(QStringLiteral("groundcover="))) r << l.mid(12);
    return r;
}

QStringList managedDataPaths(const QString &out)
{
    QStringList r;
    bool inManaged = false;
    for (const QString &l : out.split('\n')) {
        if (l == "# --- Nerevarine Organizer BEGIN ---") { inManaged = true; continue; }
        if (l == "# --- Nerevarine Organizer END ---")   { inManaged = false; continue; }
        if (inManaged && l.startsWith(QStringLiteral("data="))) {
            QString p = l.mid(5);
            if (p.startsWith('"') && p.endsWith('"')) p = p.mid(1, p.size() - 2);
            r << p;
        }
    }
    return r;
}

void dumpScenarioOnFail(const Scenario &sc, const QString &out, const char *invariant)
{
    std::cerr << "\n  ✗ INVARIANT VIOLATION: " << invariant << "\n";
    std::cerr << "    seed=" << s_currentSeed << "\n";
    std::cerr << "    mods=" << sc.mods.size()
              << " loadOrder.size=" << sc.loadOrder.size()
              << " existingCfg.bytes=" << sc.existingCfg.size() << "\n";
    std::cerr << "    --- loadOrder ---\n";
    for (const QString &p : sc.loadOrder)
        std::cerr << "      " << p.toStdString() << "\n";
    std::cerr << "    --- output ---\n" << out.toStdString() << "\n";
    std::cerr << "    --- end output ---\n";
}

bool runOne(uint32_t seed)
{
    s_currentSeed = seed;
    Gen gen(seed);
    Scenario sc = gen.build();

    const QString out = renderOpenMWConfig(sc.mods, sc.loadOrder, sc.existingCfg);

    // I1 - bracketing.
    int beginCount = 0, endCount = 0, beginPos = -1, endPos = -1;
    {
        const QStringList lines = out.split('\n');
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i] == "# --- Nerevarine Organizer BEGIN ---") { ++beginCount; beginPos = i; }
            if (lines[i] == "# --- Nerevarine Organizer END ---")   { ++endCount;   endPos   = i; }
        }
    }
    if (beginCount != 1 || endCount != 1 || beginPos > endPos) {
        dumpScenarioOnFail(sc, out, "I1 BEGIN/END bracketing");
        return false;
    }

    // I2 - no duplicate content=.
    {
        const QStringList cs = contentValues(out);
        QSet<QString> seen;
        for (const QString &c : cs) {
            if (seen.contains(c)) {
                dumpScenarioOnFail(sc, out, "I2 duplicate content= entry");
                return false;
            }
            seen.insert(c);
        }
    }

    // I3 - no duplicate groundcover=.
    {
        const QStringList gs = groundcoverValues(out);
        QSet<QString> seen;
        for (const QString &g : gs) {
            if (seen.contains(g)) {
                dumpScenarioOnFail(sc, out, "I3 duplicate groundcover= entry");
                return false;
            }
            seen.insert(g);
        }
    }

    // I4 - suppressed plugins absent from content/groundcover.
    {
        // Save lists in locals first - passing .begin()/.end() of two
        // separate temporaries gives the QSet ctor iterators into different
        // (already-destroyed) objects.
        const QStringList csList = contentValues(out);
        const QStringList gsList = groundcoverValues(out);
        const QSet<QString> cs(csList.begin(), csList.end());
        const QSet<QString> gs(gsList.begin(), gsList.end());
        for (const QString &s : sc.allSuppressed) {
            if (cs.contains(s) || gs.contains(s)) {
                dumpScenarioOnFail(sc, out, "I4 suppressed plugin leaked into output");
                return false;
            }
        }
    }

    // I5 - disabled-or-uninstalled mods' plugins absent from content=.
    // (Exception: a plugin name shared by an enabled mod is OK; check that
    // each filename appearing as content= is in the enabled+installed set
    // OR is an external/base-game plugin from existingCfg.)
    {
        // Collect filenames that came from the existingCfg's content= lines
        // and aren't claimed by any installed mod - those are externals
        // that should pass through.
        QSet<QString> externalFilenames;
        for (const QString &l : sc.existingCfg.split('\n')) {
            QString line = l;
            if (line.endsWith('\r')) line.chop(1);
            if (!line.startsWith(QStringLiteral("content="))) continue;
            const QString cf = line.mid(8);
            if (sc.allEnabledInstalledFilenames.contains(cf)) continue;
            if (sc.allDisabledOrUninstalledFilenames.contains(cf)) continue;
            externalFilenames.insert(cf);
        }
        for (const QString &c : contentValues(out)) {
            const bool ok = sc.allEnabledInstalledFilenames.contains(c) ||
                            externalFilenames.contains(c);
            if (!ok) {
                dumpScenarioOnFail(sc, out, "I5 disabled/uninstalled plugin leaked into content=");
                return false;
            }
        }
    }

    // I6 - load-order respected for managed plugins.
    {
        const QStringList cs = contentValues(out);
        // For each pair (X, Y) in loadOrder where X before Y, BOTH visible
        // in cs as managed: assert X comes first in cs.
        for (int i = 0; i < sc.loadOrder.size(); ++i) {
            const QString &x = sc.loadOrder[i];
            if (!sc.allEnabledInstalledFilenames.contains(x)) continue;
            if (sc.allSuppressed.contains(x)) continue;
            if (sc.allGroundcover.contains(x)) continue;
            int xIdx = cs.indexOf(x);
            if (xIdx < 0) continue;
            for (int j = i + 1; j < sc.loadOrder.size(); ++j) {
                const QString &y = sc.loadOrder[j];
                if (!sc.allEnabledInstalledFilenames.contains(y)) continue;
                if (sc.allSuppressed.contains(y)) continue;
                if (sc.allGroundcover.contains(y)) continue;
                int yIdx = cs.indexOf(y);
                if (yIdx < 0) continue;
                if (yIdx < xIdx) {
                    dumpScenarioOnFail(sc, out, "I6 load-order inversion");
                    return false;
                }
            }
        }
    }

    // I7 - every enabled+installed mod's plugin dir appears as data=.
    {
        const QStringList ds = managedDataPaths(out);
        const QSet<QString> dsSet(ds.begin(), ds.end());
        for (const auto &p : sc.enabledPluginDirs) {
            if (!dsSet.contains(p.first)) {
                dumpScenarioOnFail(sc, out, "I7 enabled+installed plugin dir missing from data=");
                return false;
            }
        }
    }

    // I8 - idempotence: render(out) is byte-identical to out.
    {
        const QString out2 = renderOpenMWConfig(sc.mods, sc.loadOrder, out);
        if (out2 != out) {
            dumpScenarioOnFail(sc, out, "I8 not idempotent (re-render differs)");
            std::cerr << "    --- second render ---\n" << out2.toStdString() << "\n";
            return false;
        }
    }

    // I9 - suppressed plugin filenames don't appear as bare data=
    // (sanity: a suppressed plugin's *file* name shouldn't end up as a
    // data= path; data= only carries directory paths).
    {
        const QStringList ds = managedDataPaths(out);
        for (const QString &s : sc.allSuppressed)
            for (const QString &d : ds)
                if (d.endsWith('/' + s)) {
                    dumpScenarioOnFail(sc, out, "I9 suppressed filename appeared in data= path");
                    return false;
                }
    }

    return true;
}

void runFuzz(uint32_t startSeed, int iterations)
{
    std::cout << "runFuzz: starting at seed=" << startSeed
              << ", iterations=" << iterations << "\n";

    int firstFailure = -1;
    int passed = 0, failed = 0;
    for (int i = 0; i < iterations; ++i) {
        const uint32_t seed = startSeed + uint32_t(i);
        if (runOne(seed)) {
            ++passed;
        } else {
            ++failed;
            if (firstFailure < 0) firstFailure = int(seed);
            // Keep going so we discover multiple failure modes per run, but
            // cap the noise.
            if (failed > 5) {
                std::cerr << "  (suppressing further failures)\n";
                break;
            }
        }
    }

    if (failed == 0) {
        std::cout << "  ✓ " << passed << "/" << iterations << " random scenarios passed\n";
        ++s_passed;
    } else {
        std::cout << "  ✗ " << failed << " failure(s); first at seed=" << firstFailure
                  << " (reproduce: --seed=" << firstFailure << ")\n";
        ++s_failed;
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // Parse --seed=N to reproduce a single failing scenario.
    uint32_t startSeed = 0xC0FFEE;
    int iterations = 500;
    bool single = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a.rfind("--seed=", 0) == 0) {
            startSeed = static_cast<uint32_t>(std::stoul(a.substr(7)));
            single = true;
        } else if (a.rfind("--iterations=", 0) == 0) {
            iterations = std::stoi(a.substr(13));
        }
    }

    if (single) {
        std::cout << "single scenario: seed=" << startSeed << "\n";
        const bool ok = runOne(startSeed);
        return ok ? 0 : 1;
    }

    runFuzz(startSeed, iterations);

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
