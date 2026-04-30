// tests/test_install_layout.cpp
//
// Regression coverage for install_layout::diveTarget - the post-extraction
// "where's the real mod root?" decision used by InstallController.
//
// Why this test exists:
//   A user on OpenSUSE Tumbleweed reported that single-folder mod archives
//   (Ingredients Mesh Replacer, Fixed Bonelord Arms, …) never functioned
//   after install, and that mods like "Dubdilla Location Fix" - single
//   subfolder + a sibling .esp at the archive root - silently dropped the
//   .esp.  Both stem from the original dive heuristic blindly recursing
//   into the only top-level subdirectory.
//
//   These cases lock in:
//     · don't dive into asset-named subdirs (meshes/, textures/, …) - the
//       data root IS the extract dir and OpenMW's data= path needs to
//       point one level higher than the asset folder
//     · don't dive when loose top-level files sit alongside the subdir -
//       diving would orphan them
//     · DO dive into a normal "<ModName>/…" wrapper so the folder name
//       and FOMOD detection see the real mod root
//
// Build + run:
//   cmake --build build -j$(nproc) && ./build/tests/test_install_layout

#include "install_layout.h"

#include <QString>
#include <QStringList>

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

static void expectDive(const char *name,
                       const QStringList &subs,
                       const QStringList &files,
                       const QString &expected)
{
    const QString got = install_layout::diveTarget(subs, files);
    check(name, got == expected, got);
}

int main()
{
    std::cout << "install_layout::diveTarget\n";

    // -- Standard "<ModName>/<contents>" wrapper - dive ---
    expectDive("dives into normal mod-name wrapper",
               {"My Cool Mod"}, {},
               "My Cool Mod");

    // -- Single subdir IS an asset folder - DO NOT dive ---
    // The "Ingredients Mesh Replacer" / "Fixed Bonelord Arms" case: archive
    // root is literally `meshes/…`.  Diving makes data="<…>/meshes" and
    // OpenMW would search for "meshes/foo.nif" inside the meshes/ folder
    // itself, never finding it.
    expectDive("does not dive into bare meshes/",   {"meshes"},   {}, QString());
    expectDive("does not dive into bare textures/", {"textures"}, {}, QString());
    expectDive("does not dive into bare sound/",    {"sound"},    {}, QString());
    expectDive("does not dive into bare music/",    {"music"},    {}, QString());
    expectDive("does not dive into bare fonts/",    {"fonts"},    {}, QString());
    expectDive("does not dive into bare bookart/",  {"bookart"},  {}, QString());
    expectDive("does not dive into bare splash/",   {"splash"},   {}, QString());
    expectDive("does not dive into bare icons/",    {"icons"},    {}, QString());
    expectDive("does not dive into bare scripts/",  {"scripts"},  {}, QString());

    // Casing should not matter - Windows-cased archives must be treated
    // identically to lowercase Linux ones.
    expectDive("asset-name check is case-insensitive (Meshes)",
               {"Meshes"}, {}, QString());
    expectDive("asset-name check is case-insensitive (TEXTURES)",
               {"TEXTURES"}, {}, QString());

    // -- FOMOD installer at the root - DO NOT dive ---
    // Diving would push ModuleConfig.xml to <modPath>/fomod/fomod/… and
    // FomodWizard::hasFomod() would silently skip the wizard.
    expectDive("does not dive into bare fomod/",
               {"fomod"}, {}, QString());

    // -- Sibling files alongside a subdir - DO NOT dive ---
    // The reported "Dubdilla Location Fix" case: archive holds one folder
    // (docs / changelog / extras) plus the actual .esp at the root.
    // Diving previously left the .esp orphaned outside the chosen modPath.
    expectDive("does not dive when an .esp sits at the root",
               {"Documentation"}, {"Dubdilla Location Fix.esp"},
               QString());
    expectDive("does not dive when a .bsa sits at the root",
               {"Extras"}, {"MyMod.bsa"},
               QString());
    expectDive("does not dive when a readme sits at the root",
               {"Data"}, {"README.txt"},
               QString());

    // -- Multiple subdirs - DO NOT dive (no single root to pick) ---
    expectDive("does not dive when two subdirs are present",
               {"meshes", "textures"}, {},
               QString());
    expectDive("does not dive when many subdirs are present",
               {"00 Core", "01 Optional", "fomod"}, {},
               QString());

    // -- Empty / pathological inputs ---
    expectDive("empty input → empty result", {}, {}, QString());
    expectDive("only files, no subdirs → empty result",
               {}, {"loose.esp"}, QString());

    // -- Wrapper-named-after-mod is fine even if it shares an asset
    //    suffix.  Only the EXACT bare-asset-name match suppresses the dive,
    //    so "MyMeshes" or "ScriptPack" still get the dive.
    expectDive("dives when subdir merely contains 'meshes' as substring",
               {"MyMeshes"}, {},
               "MyMeshes");
    expectDive("dives when subdir merely contains 'scripts' as substring",
               {"ScriptPack"}, {},
               "ScriptPack");

    std::cout << "\n";
    if (s_failed == 0) {
        std::cout << "\033[32mAll " << s_passed << " checks passed.\033[0m\n";
        return 0;
    }
    std::cout << "\033[31m" << s_failed << " checks failed (" << s_passed
              << " passed).\033[0m\n";
    return 1;
}
