// tests/test_fomod_path.cpp
//
// Regression coverage for fomod::resolvePath - the chokepoint every FOMOD
// file/folder reference from ModuleConfig.xml passes through before landing
// on the filesystem.
//
// Why this test exists:
//   OAAB_Data.esm vanished from the OpenMW launcher because the ModuleConfig
//   used Windows-style backslashes AND mis-cased folder names
//   ("00 Core\OAAB_Data.esm" vs. on-disk "00 Core/OAAB_Data.esm"). The
//   previous installer fed that string straight to QFile::copy, which fails
//   silently on Linux, leaving an empty install folder. These tests lock in
//   that the resolver normalises separators, is case-insensitive per-segment,
//   and returns an empty QString (not a bogus path) when a segment is really
//   missing.
//
// Build + run:
//   cmake --build build -j$(nproc) && ./build/tests/test_fomod_path

#include "fomod_path.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok, const QString &got = QString())
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name;
        if (!got.isNull()) std::cout << "  - got: \"" << got.toStdString() << "\"";
        std::cout << "\n";
        ++s_failed;
    }
}

// Build a realistic OAAB-shaped archive tree inside `root`.
static void seedArchive(const QString &root)
{
    QDir().mkpath(root + "/00 Core/bookart");
    QDir().mkpath(root + "/00 Core/meshes");
    QDir().mkpath(root + "/00 Core/Textures");
    QDir().mkpath(root + "/01 Epic Plants Patch/Meshes");
    QDir().mkpath(root + "/fomod");
    auto touch = [](const QString &p) {
        QFile f(p);
        (void)f.open(QIODevice::WriteOnly);
        f.close();
    };
    touch(root + "/00 Core/OAAB_Data.esm");
    touch(root + "/00 Core/bookart/tome.dds");
    touch(root + "/fomod/ModuleConfig.xml");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "=== fomod_path tests ===\n";

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        std::cout << "  \033[31m✗\033[0m could not create temp dir\n";
        return 1;
    }
    const QString root = tmp.path();
    seedArchive(root);

    auto expectResolves = [&](const char *name, const QString &rel, const QString &expectedSuffix) {
        QString got = fomod::resolvePath(root, rel);
        bool ok = !got.isEmpty()
               && QFileInfo::exists(got)
               && got.endsWith(expectedSuffix);
        check(name, ok, got);
    };
    auto expectEmpty = [&](const char *name, const QString &rel) {
        QString got = fomod::resolvePath(root, rel);
        check(name, got.isEmpty(), got);
    };

    // -- Happy path: exact matches ---
    expectResolves("exact file",
                   "00 Core/OAAB_Data.esm",
                   "00 Core/OAAB_Data.esm");
    expectResolves("exact folder",
                   "00 Core/bookart",
                   "00 Core/bookart");

    // -- The bug: Windows backslashes must normalise to forward slashes ---
    expectResolves("backslash path (OAAB bug)",
                   "00 Core\\OAAB_Data.esm",
                   "00 Core/OAAB_Data.esm");
    expectResolves("mixed separators",
                   "00 Core\\bookart/tome.dds",
                   "00 Core/bookart/tome.dds");

    // -- Per-segment case-insensitivity ---
    expectResolves("case-insensitive folder (Meshes ↔ meshes)",
                   "00 Core\\Meshes",
                   "00 Core/meshes");
    expectResolves("case-insensitive leaf (textures ↔ Textures)",
                   "00 Core/textures",
                   "00 Core/Textures");
    expectResolves("case-insensitive every segment",
                   "00 CORE\\BOOKART\\TOME.DDS",
                   "00 Core/bookart/tome.dds");

    // -- Empty / trivial ---
    {
        QString got = fomod::resolvePath(root, "");
        check("empty relative path returns root", got == root, got);
    }

    // -- Missing segments must return empty (not the parent directory) ---
    //   The original bug swallowed failures.  Callers rely on "" meaning
    //   "genuinely not found" so they can count failures and warn the user.
    expectEmpty("missing leaf",
                "00 Core/NOT_THERE.esm");
    expectEmpty("missing intermediate folder",
                "99 Missing Folder/OAAB_Data.esm");
    expectEmpty("backslashed missing path",
                "99 Missing\\thing.esp");

    std::cout << "\n";
    std::cout << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
