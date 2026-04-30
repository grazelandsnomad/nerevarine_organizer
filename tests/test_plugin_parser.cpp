// tests/test_plugin_parser.cpp
//
// Exercises the pure pieces of include/pluginparser.h.
// Runs on a tmpdir fixture; no network, no UI, no state.
//
// Build + run:
//   ./build/tests/test_plugin_parser

#include "pluginparser.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstring>
#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok)
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name << "\n";
        ++s_failed;
    }
}

// -- Helpers to build a minimal TES3 plugin on disk ---
//
// Header record: "TES3" (4) + recSize (4 LE) + 8 bytes of padding.
// Then each subrecord: tag(4) + size(4 LE) + `size` bytes of data.
//
// For our purposes we only need MAST subrecords - we never populate HEDR,
// since readTes3Masters doesn't require it to be present or well-formed.

static QByteArray uint32le(quint32 v) {
    QByteArray b(4, '\0');
    std::memcpy(b.data(), &v, 4);
    return b;
}

static QByteArray masterSubrecord(const QByteArray &name)
{
    QByteArray payload = name;
    payload.append('\0');  // MAST is null-terminated
    QByteArray sub;
    sub.append("MAST", 4);
    sub.append(uint32le(payload.size()));
    sub.append(payload);
    return sub;
}

static QByteArray dataSubrecord(quint64 fileSize)
{
    QByteArray payload(8, '\0');
    std::memcpy(payload.data(), &fileSize, 8);
    QByteArray sub;
    sub.append("DATA", 4);
    sub.append(uint32le(8));
    sub.append(payload);
    return sub;
}

// Create an empty file, silently ignoring failures (tmpdir is always writable).
static void touchFile(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.close();
}

static void writeTes3(const QString &path, const QStringList &masters)
{
    QByteArray body;
    for (const QString &m : masters) {
        body.append(masterSubrecord(m.toLatin1()));
        body.append(dataSubrecord(0));  // engine expects DATA after each MAST
    }

    QByteArray file;
    file.append("TES3", 4);
    file.append(uint32le(body.size()));
    file.append(QByteArray(8, '\0'));  // padding
    file.append(body);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(file);
    f.close();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "=== plugin parser tests ===\n";

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::cout << "Could not create temp dir\n";
        return 2;
    }
    QDir root(tmp.path());

    // -- readTes3Masters ---
    {
        QString f = root.filePath("one_master.esp");
        writeTes3(f, {"Morrowind.esm"});
        auto m = plugins::readTes3Masters(f);
        check("single-master TES3 parses",
              m.size() == 1 && m[0] == "Morrowind.esm");
    }
    {
        QString f = root.filePath("multi_master.esp");
        writeTes3(f, {"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"});
        auto m = plugins::readTes3Masters(f);
        check("three-master TES3 parses in order",
              m == QStringList{"Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"});
    }
    {
        // Non-TES3 file (wrong magic) → empty list, no throw.
        QString f = root.filePath("garbage.bin");
        QFile g(f);
        if (g.open(QIODevice::WriteOnly)) {
            g.write(QByteArray(256, 'X'));
            g.close();
        }
        check("non-TES3 file returns empty masters",
              plugins::readTes3Masters(f).isEmpty());
    }
    {
        // Missing file - empty, no crash.
        check("missing file returns empty masters",
              plugins::readTes3Masters(root.filePath("nope.esm")).isEmpty());
    }

    // -- collectDataFolders ---
    {
        // Layout:
        //   modA/         contains modA.esp
        //   modB/00 Core/ contains OAAB_Data.esm
        //   modB/fomod/   contains ModuleConfig.xml  (must be skipped)
        //   modB/docs/    contains README.md         (must be skipped)
        //   modC/Data Files/nested/mod.esp
        QDir(root.filePath("modA")).mkpath(".");
        touchFile(root.filePath("modA/modA.esp"));

        QDir(root.filePath("modB/00 Core")).mkpath(".");
        touchFile(root.filePath("modB/00 Core/OAAB_Data.esm"));
        QDir(root.filePath("modB/fomod")).mkpath(".");
        touchFile(root.filePath("modB/fomod/ModuleConfig.xml"));
        // (ModuleConfig.xml wouldn't match the ext filter anyway, but put an .esp
        //  inside fomod/ to make sure we don't accidentally pick it up)
        touchFile(root.filePath("modB/fomod/plugin_inside_fomod.esp"));

        QDir(root.filePath("modC/Data Files/nested")).mkpath(".");
        touchFile(root.filePath("modC/Data Files/nested/mod.esp"));

        auto exts = plugins::contentExtensions();

        auto fa = plugins::collectDataFolders(root.filePath("modA"), exts);
        check("modA: root-level .esp picked up",
              fa.size() == 1 && fa[0].second == QStringList{"modA.esp"});

        auto fb = plugins::collectDataFolders(root.filePath("modB"), exts);
        // Expect exactly one hit: 00 Core/OAAB_Data.esm.  fomod/ must be
        // skipped despite containing a plugin file.
        bool fbOk = (fb.size() == 1)
                 && fb[0].second == QStringList{"OAAB_Data.esm"}
                 && fb[0].first.endsWith("00 Core");
        check("modB: fomod/ is skipped, 00 Core/ picked up", fbOk);

        auto fc = plugins::collectDataFolders(root.filePath("modC"), exts);
        bool fcOk = (fc.size() == 1)
                 && fc[0].second == QStringList{"mod.esp"};
        check("modC: two-deep nesting is found", fcOk);
    }
    {
        // Depth cap.  Files AT depth N get scanned when maxDepth=N (scan
        // happens before the "should I descend?" check).  So to exercise
        // the cap we need a plugin one level BEYOND the cap - depth 7 with
        // default maxDepth=6.
        QString deep = root.filePath("deep/a/b/c/d/e/f/g/"); // 7 subdirs below "deep"
        QDir().mkpath(deep);
        touchFile(deep + "plugin.esp");

        auto exts = plugins::contentExtensions();
        auto miss = plugins::collectDataFolders(root.filePath("deep"), exts, 6);
        auto hit  = plugins::collectDataFolders(root.filePath("deep"), exts, 8);
        check("maxDepth=6 skips plugin at depth 7",
              miss.isEmpty() || (miss.size() == 1 && !miss[0].second.contains("plugin.esp")));
        check("maxDepth=8 finds deep plugin",
              hit.size() == 1 && hit[0].second == QStringList{"plugin.esp"});
    }
    {
        // Non-existent dir returns empty, no crash.
        check("non-existent root returns empty list",
              plugins::collectDataFolders(
                  root.filePath("absolutely_not_there"),
                  plugins::contentExtensions()).isEmpty());
    }

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
