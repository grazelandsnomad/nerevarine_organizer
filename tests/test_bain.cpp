// tests/test_bain.cpp
//
// Coverage for bain:: detection + staging - the BAIN (Wrye Bash numbered
// package) installer's pure logic.
//
// The crux is looksLikeBain(): BAIN is a naming convention, not a spec, so it
// can't be told apart with certainty from an "install everything" multi-data-
// root mod (Tamriel Rebuilt: "00 Core" + "01 Faction Integration"). The design
// accepts that - detection is conservative, the picker pre-checks everything,
// and a false positive costs one click. These tests pin the conservative
// boundary (no FOMOD, no asset roots, every folder numbered, >= 2) and the
// numeric-order last-writer-wins merge.
//
// Build + run:
//   cmake --build build && ./build/tests/test_bain

#include "bain.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

static void check(const char *name, bool ok, const QString &hint = {})
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name;
        if (!hint.isEmpty()) std::cout << "  (" << hint.toStdString() << ")";
        std::cout << "\n";
        ++s_failed;
    }
}

static void mkdirs(const QString &p) { QDir().mkpath(p); }
static void touch(const QString &p, const QByteArray &b = "x")
{
    QDir().mkpath(QFileInfo(p).absolutePath());
    QFile f(p);
    if (f.open(QIODevice::WriteOnly)) { f.write(b); f.close(); }
}
static QByteArray readAll(const QString &p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// -- Detection ---------------------------------------------------------------

static void testDetection()
{
    std::cout << "\n[looksLikeBain detection boundary]\n";

    {   // Classic BAIN: two numbered packages, each with data content.
        QTemporaryDir d;
        touch(d.filePath("00 Core/meshes/a.nif"));
        touch(d.filePath("01 Optional Textures/textures/b.dds"));
        check("two numbered packages -> BAIN", bain::looksLikeBain(d.path()));
        check("packages() returns 2", bain::packages(d.path()).size() == 2);
    }
    {   // FOMOD present -> never BAIN (FOMOD precedence), even if numbered.
        QTemporaryDir d;
        touch(d.filePath("00 Core/meshes/a.nif"));
        touch(d.filePath("01 Optional/textures/b.dds"));
        touch(d.filePath("fomod/ModuleConfig.xml"));
        check("fomod/ present -> NOT BAIN", !bain::looksLikeBain(d.path()));
    }
    {   // A bare asset root at top level -> plain mod data, not packages.
        QTemporaryDir d;
        touch(d.filePath("00 Core/x.esp"));
        mkdirs(d.filePath("meshes"));
        check("an asset-root sibling -> NOT BAIN", !bain::looksLikeBain(d.path()));
    }
    {   // Mixed: one numbered folder beside an ordinary one -> not a package set.
        QTemporaryDir d;
        touch(d.filePath("00 Core/x.esp"));
        touch(d.filePath("Docs/readme.txt"));
        check("numbered + non-numbered mix -> NOT BAIN",
              !bain::looksLikeBain(d.path()));
    }
    {   // Single numbered folder -> no real choice, not treated as BAIN.
        QTemporaryDir d;
        touch(d.filePath("00 Core/x.esp"));
        check("single package -> NOT BAIN", !bain::looksLikeBain(d.path()));
    }
    {   // Plain mod (just data roots) -> not BAIN.
        QTemporaryDir d;
        touch(d.filePath("meshes/a.nif"));
        touch(d.filePath("textures/b.dds"));
        check("plain meshes+textures -> NOT BAIN", !bain::looksLikeBain(d.path()));
    }
    {   // Documented accepted false positive: an all-numbered "install all"
        //   mod (Tamriel-Rebuilt shape) DOES match. That's intentional - the
        //   picker pre-checks everything so the result is identical to a plain
        //   install. We assert the behaviour so it's a conscious contract.
        QTemporaryDir d;
        touch(d.filePath("00 Core/Core.esm"));
        touch(d.filePath("01 Faction Integration/FI.esp"));
        check("all-numbered install-all mod matches (accepted, all pre-checked)",
              bain::looksLikeBain(d.path()));
    }
    {   // Missing dir.
        check("nonexistent path -> NOT BAIN",
              !bain::looksLikeBain("/nonexistent/path/nrv_bain_test"));
    }
}

// -- Ordering ----------------------------------------------------------------

static void testOrdering()
{
    std::cout << "\n[packages() numeric order]\n";
    QTemporaryDir d;
    touch(d.filePath("10 Late/textures/z.dds"));
    touch(d.filePath("02 Mid/textures/y.dds"));
    touch(d.filePath("00 Core/textures/x.dds"));
    const auto pkgs = bain::packages(d.path());
    bool order = pkgs.size() == 3
              && pkgs[0].name.startsWith("00")
              && pkgs[1].name.startsWith("02")
              && pkgs[2].name.startsWith("10");
    check("00 < 02 < 10 (numeric, not lexical)", order,
          pkgs.size() == 3 ? (pkgs[0].name + "," + pkgs[1].name + "," + pkgs[2].name)
                           : QString("size=%1").arg(pkgs.size()));
}

// -- Staging / merge ---------------------------------------------------------

static void testStageMerge()
{
    std::cout << "\n[stage() merges chosen packages, last writer wins]\n";
    QTemporaryDir d;
    // 00 Core ships base x + keep; 01 Patch overrides x and adds y.
    touch(d.filePath("00 Core/meshes/x.nif"), "base");
    touch(d.filePath("00 Core/meshes/keep.nif"), "keep");
    touch(d.filePath("01 Patch/meshes/x.nif"), "patch");
    touch(d.filePath("01 Patch/meshes/y.nif"), "new");
    touch(d.filePath("02 Unwanted/meshes/z.nif"), "nope");

    const QString modPath = d.filePath("mod");  // a sub so bain_install is a sibling
    // Re-root the packages under modPath so stage() puts bain_install beside it.
    QDir().mkpath(modPath);
    QDir(d.path()).rename("00 Core", "mod/00 Core");
    QDir(d.path()).rename("01 Patch", "mod/01 Patch");
    QDir(d.path()).rename("02 Unwanted", "mod/02 Unwanted");

    const QString staged = bain::stage(modPath, {"00 Core", "01 Patch"});
    check("stage returns a non-empty path", !staged.isEmpty(), staged);
    check("base-only file kept",
          QFileInfo::exists(staged + "/meshes/keep.nif"));
    check("patch's new file present",
          QFileInfo::exists(staged + "/meshes/y.nif"));
    check("unselected package NOT staged",
          !QFileInfo::exists(staged + "/meshes/z.nif"));
    check("later package overwrote earlier (last writer wins)",
          readAll(staged + "/meshes/x.nif") == "patch",
          QString::fromUtf8(readAll(staged + "/meshes/x.nif")));

    check("empty selection -> empty staged path",
          bain::stage(modPath, {}).isEmpty());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::cout << "=== bain tests ===\n";
    testDetection();
    testOrdering();
    testStageMerge();
    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
