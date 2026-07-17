#include "mod_naming.h"
#include "mod_sharing.h"
#include "modentry.h"
#include "modlist_serializer.h"
#include "modlist_summary.h"
#include "install_layout.h"
#include "post_install.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <iostream>

static int s_passed = 0;
static int s_failed = 0;

// Optional hint shown in parens on failure. Defaults to null so an empty
// QString() result doesn't print a blank diagnostic.
static void check(const char *name, bool ok, const QString &hint = QString())
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name;
        if (!hint.isNull() && !hint.isEmpty())
            std::cout << " (" << hint.toStdString() << ")";
        std::cout << "\n";
        ++s_failed;
    }
}

// === mod_naming ===

static void testFindStaleSiblings_basicNexusReinstall()
{
    std::cout << "\n[findStaleSiblings: nexus reinstall picks up old timestamp folder]\n";
    QStringList siblings = {
        "Foo-12345",              // older same-mod install
        "Foo-12345_1776202250",   // just installed, don't return
        "UnrelatedMod-67890",     // different mod
    };
    auto stale = mod_naming::findStaleSiblings("Foo-12345_1776202250", siblings);
    check("returns the older same-mod install", stale.contains("Foo-12345"));
    check("does NOT return self",           !stale.contains("Foo-12345_1776202250"));
    check("does NOT return unrelated mod",  !stale.contains("UnrelatedMod-67890"));
}

static void testFindStaleSiblings_userNamedFolderProtected()
{
    std::cout << "\n[findStaleSiblings: user-named folder ending in _<digits> NOT cleaned]\n";
    // "Save" isn't the Nexus name-<id> shape, so a user folder ending in
    // _<digits> must not get cleaned.
    QStringList siblings = { "Save", "Save_1234", "Save_5678" };
    auto stale = mod_naming::findStaleSiblings("Save_5678", siblings);
    check("user-named folder produces no stale entries",
          stale.isEmpty(),
          "would have been " + QString::number(stale.size()));
}

static void testFindStaleSiblings_strippedSuffixHonored()
{
    std::cout << "\n[findStaleSiblings: prefix without trailing _<ts> still matches]\n";
    // current has no _<ts> suffix, older ones do.
    QStringList siblings = { "Foo-12345_1234567", "Foo-12345_2345678", "Foo-12345" };
    auto stale = mod_naming::findStaleSiblings("Foo-12345", siblings);
    check("matches older _<ts>-suffixed siblings",
          stale.contains("Foo-12345_1234567")
            && stale.contains("Foo-12345_2345678"));
}

static void testFindStaleSiblings_emptyInputs()
{
    std::cout << "\n[findStaleSiblings: empty inputs → empty output]\n";
    check("empty current name → empty",
          mod_naming::findStaleSiblings(QString(), {"a", "b"}).isEmpty());
    check("empty siblings → empty",
          mod_naming::findStaleSiblings("Foo-12345", {}).isEmpty());
}

static void testGeneric_exactMatchList()
{
    std::cout << "\n[folderNameLooksGeneric: curated names match (case-insensitive)]\n";
    check("'scripts' generic",          mod_naming::folderNameLooksGeneric("scripts"));
    check("'Scripts' generic",          mod_naming::folderNameLooksGeneric("Scripts"));
    check("'Data Files' generic",       mod_naming::folderNameLooksGeneric("Data Files"));
    check("'00 Core' generic",          mod_naming::folderNameLooksGeneric("00 Core"));
    check("'Complete pack' generic",    mod_naming::folderNameLooksGeneric("Complete pack"));
    check("'mygui' generic",            mod_naming::folderNameLooksGeneric("mygui"));
}

static void testGeneric_genericPrefixes()
{
    std::cout << "\n[folderNameLooksGeneric: 'sound*'/'audio*'/'mesh*'/'fix*' all match]\n";
    check("'sound' generic",       mod_naming::folderNameLooksGeneric("sound"));
    check("'soundFX' generic",     mod_naming::folderNameLooksGeneric("soundFX"));
    check("'meshes_replacer' generic",
          mod_naming::folderNameLooksGeneric("meshes_replacer"));
    check("'fixes_pack' generic",  mod_naming::folderNameLooksGeneric("fixes_pack"));
    check("'audio01' generic",     mod_naming::folderNameLooksGeneric("audio01"));
}

static void testGeneric_nexusArchiveShape()
{
    std::cout << "\n[folderNameLooksGeneric: Nexus-archive slug shapes match]\n";
    check("'main-12345-1' generic",
          mod_naming::folderNameLooksGeneric("main-12345-1"));
    check("'main-54985-0-6-6-1775044149' generic",
          mod_naming::folderNameLooksGeneric("main-54985-0-6-6-1775044149"));
    check("'main-54985-0-6-6-1775044149_1776202250' generic",
          mod_naming::folderNameLooksGeneric("main-54985-0-6-6-1775044149_1776202250"));
    check("'complete pack-99-1-2' generic",
          mod_naming::folderNameLooksGeneric("complete pack-99-1-2"));
}

static void testGeneric_versionedArchive()
{
    std::cout << "\n[folderNameLooksGeneric: 4+ trailing version segments match]\n";
    // Five numeric segments after the first hyphen.
    check("'OAAB_Data-49042-2-5-1-1764958680' generic",
          mod_naming::folderNameLooksGeneric("OAAB_Data-49042-2-5-1-1764958680"));
    // Three segments must not match, or it catches user-named mods.
    check("'Foo-1-2-3' NOT generic (only 3 segs)",
          !mod_naming::folderNameLooksGeneric("Foo-1-2-3"));
}

static void testGeneric_bareUuidToken()
{
    std::cout << "\n[folderNameLooksGeneric: bare CDN UUID tokens are generic]\n";
    // Extensionless Nexus CDN downloads land in a bare UUID folder; without
    // this the mod name shows up as "4c9017a6-...".
    check("bare UUID generic",
          mod_naming::folderNameLooksGeneric(
              "4c9017a6-9af8-40b9-acb9-d95d6cff091f"));
    check("UUID with extractor _<ts> suffix generic",
          mod_naming::folderNameLooksGeneric(
              "4c9017a6-9af8-40b9-acb9-d95d6cff091f_1781900000"));
    check("uppercase UUID generic",
          mod_naming::folderNameLooksGeneric(
              "C96CE3C0-9219-43B3-A5FC-BB23F0E1F8F4"));
    // A real mod whose name has hyphens/hex must not be caught.
    check("'Beautiful Cities-12-3' NOT a UUID",
          !mod_naming::folderNameLooksGeneric("Beautiful Cities-12-3"));
    check("'deadbeef' (too short) NOT a UUID",
          !mod_naming::folderNameLooksGeneric("deadbeef"));
}

static void testGeneric_realModsNotMisclassified()
{
    std::cout << "\n[folderNameLooksGeneric: user-named mods are NOT generic]\n";
    check("'Tamriel Rebuilt' NOT generic",
          !mod_naming::folderNameLooksGeneric("Tamriel Rebuilt"));
    check("'OAAB_Data' NOT generic",
          !mod_naming::folderNameLooksGeneric("OAAB_Data"));
    check("'Authentic Signs IT' NOT generic",
          !mod_naming::folderNameLooksGeneric("Authentic Signs IT"));
    // Trailing number alone isn't the Nexus shape (needs -<id>).
    check("'Mod 2' NOT generic",
          !mod_naming::folderNameLooksGeneric("Mod 2"));
}

static void testStripVersionChain_typical()
{
    std::cout << "\n[stripTrailingVersionChain: peel '<id>-v-v-ts' tail]\n";
    check("real-world example",
          mod_naming::stripTrailingVersionChain(
              "Shishi - Redoran Outpost-57535-v1-1-1760726463")
            == "Shishi - Redoran Outpost");
    check("underscore separators also accepted",
          mod_naming::stripTrailingVersionChain(
              "Foo Mod_57535_v1_1_1760726463")
            == "Foo Mod");
}

static void testStripVersionChain_insufficientChain()
{
    std::cout << "\n[stripTrailingVersionChain: short chains return empty]\n";
    check("single -<id> doesn't qualify",
          mod_naming::stripTrailingVersionChain("Foo Mod-12345").isEmpty());
    check("two trailing segs doesn't qualify",
          mod_naming::stripTrailingVersionChain("Foo Mod-12345-1").isEmpty());
    check("plain folder name returns empty",
          mod_naming::stripTrailingVersionChain("Tamriel Rebuilt").isEmpty());
}

static void testHardcodedRename()
{
    std::cout << "\n[hardcodedRename: known folder names get the override]\n";
    check("'restock' renamed",
          mod_naming::hardcodedRename("restock") == "(OpenMW 0.49) Restocking");
    check("'Restock' (case-insensitive)",
          mod_naming::hardcodedRename("Restock") == "(OpenMW 0.49) Restocking");
    check("unknown name returns empty",
          mod_naming::hardcodedRename("Tamriel Rebuilt").isEmpty());
}

static void run_mod_naming()
{
    std::cout << "=== mod_naming ===\n";

    testFindStaleSiblings_basicNexusReinstall();
    testFindStaleSiblings_userNamedFolderProtected();
    testFindStaleSiblings_strippedSuffixHonored();
    testFindStaleSiblings_emptyInputs();

    testGeneric_exactMatchList();
    testGeneric_genericPrefixes();
    testGeneric_nexusArchiveShape();
    testGeneric_versionedArchive();
    testGeneric_bareUuidToken();
    testGeneric_realModsNotMisclassified();

    testStripVersionChain_typical();
    testStripVersionChain_insufficientChain();

    testHardcodedRename();
}

// === mod_sharing ===

static ModEntry sharing_mod(const QString &name, const QString &path,
                            const QString &url = {})
{
    ModEntry e;
    e.itemType     = QStringLiteral("mod");
    e.displayName  = name;
    e.customName   = name;
    e.modPath      = path;
    e.nexusUrl     = url;
    e.installStatus = 1;
    return e;
}

static ModEntry sharing_separator(const QString &name)
{
    ModEntry e;
    e.itemType    = QStringLiteral("separator");
    e.displayName = name;
    return e;
}

static void testMakeSharedRowCopyConfig()
{
    std::cout << "\n[makeSharedRow copyConfig=true keeps config, resets identity]\n";
    ModEntry src = sharing_mod("OAAB Data", "/mods/main/OAAB_Data",
                       "https://www.nexusmods.com/morrowind/mods/49042");
    src.checked      = true;
    src.fomodChoices = "0:1:2;";
    src.bainChoices  = "00 Core;01 Patch";
    src.annotation   = "great";
    src.dependsOn    = QStringList{"https://x/mods/1"};
    src.installToken = QUuid::createUuid();
    src.prevModPath  = "/old";
    src.mergeTargetPath = "/merge";
    src.hasConflict  = true;

    const ModEntry e = mod_sharing::makeSharedRow(src, /*copyConfig=*/true);
    check("modPath kept (shared folder)", e.modPath == src.modPath);
    check("nexusUrl kept",      e.nexusUrl == src.nexusUrl);
    check("customName kept",    e.customName == "OAAB Data");
    check("enabled copied",     e.checked);
    check("fomod choices copied", e.fomodChoices == "0:1:2;");
    check("bain choices copied", e.bainChoices == "00 Core;01 Patch");
    check("annotation copied",  e.annotation == "great");
    check("deps copied",        e.dependsOn == QStringList{"https://x/mods/1"});
    check("status is installed", e.installStatus == 1);
    check("install token reset", e.installToken.isNull());
    check("prevModPath reset",  e.prevModPath.isEmpty());
    check("mergeTargetPath reset", e.mergeTargetPath.isEmpty());
    check("transient conflict reset", !e.hasConflict);
}

static void testMakeSharedRowDefault()
{
    std::cout << "\n[makeSharedRow copyConfig=false starts disabled/default]\n";
    ModEntry src = sharing_mod("Patch", "/mods/main/Patch",
                       "https://www.nexusmods.com/morrowind/mods/7");
    src.checked      = true;
    src.fomodChoices = "1:1:1;";
    src.bainChoices  = "00 Core";
    src.annotation   = "note";
    src.dependsOn    = QStringList{"https://y/mods/2"};

    const ModEntry e = mod_sharing::makeSharedRow(src, /*copyConfig=*/false);
    check("disabled",            !e.checked);
    check("no fomod choices",    e.fomodChoices.isEmpty());
    check("no bain choices",     e.bainChoices.isEmpty());
    check("no annotation",       e.annotation.isEmpty());
    check("no deps",             e.dependsOn.isEmpty());
    check("but customName kept", e.customName == "Patch");
    check("and nexusUrl kept",   e.nexusUrl == src.nexusUrl);
    check("and modPath kept",    e.modPath == src.modPath);
}

static void testAppendAndDedup()
{
    std::cout << "\n[appendSharedRow appends once, dedups by path then url]\n";
    QList<ModEntry> target;
    target << sharing_separator("Section")
           << sharing_mod("Existing", "/mods/star/Existing", "https://n/mods/100");

    const ModEntry shareA = mod_sharing::makeSharedRow(
        sharing_mod("OAAB", "/mods/main/OAAB", "https://n/mods/49042"), true);

    auto r1 = mod_sharing::appendSharedRow(target, shareA);
    check("fresh mod appended", r1.added && r1.entries.size() == 3);

    // Same path again, deduped.
    auto r2 = mod_sharing::appendSharedRow(r1.entries, shareA);
    check("same path deduped (no-op)", !r2.added && r2.entries.size() == 3);

    // Trailing-slash / dot variant of the same folder also dedups.
    ModEntry slashVariant = mod_sharing::makeSharedRow(
        sharing_mod("OAAB", "/mods/main/sub/../OAAB/", "https://n/mods/49042"), true);
    auto r3 = mod_sharing::appendSharedRow(r1.entries, slashVariant);
    check("path-normalised variant deduped", !r3.added,
          mod_sharing::cleanModPath(slashVariant.modPath));

    // Same nexusUrl, different path (private fork): still dedups.
    ModEntry sameUrlOtherPath = mod_sharing::makeSharedRow(
        sharing_mod("OAAB copy", "/mods/star/OAAB_local", "https://n/mods/49042"), true);
    QList<ModEntry> withFork = r1.entries;
    auto r4 = mod_sharing::appendSharedRow(withFork, sameUrlOtherPath);
    check("same nexusUrl other path deduped", !r4.added);

    // A genuinely different mod appends.
    ModEntry other = mod_sharing::makeSharedRow(
        sharing_mod("Other", "/mods/main/Other", "https://n/mods/200"), true);
    auto r5 = mod_sharing::appendSharedRow(r1.entries, other);
    check("different mod appended", r5.added && r5.entries.size() == 4);
}

static void testPathReferencedIn()
{
    std::cout << "\n[pathReferencedIn scans other profiles' parsed modlists]\n";
    QList<QPair<QString, QList<ModEntry>>> profiles;
    profiles << qMakePair(QStringLiteral("morrowind__main"),
                          QList<ModEntry>{ sharing_separator("S"),
                                           sharing_mod("OAAB", "/mods/main/OAAB", "https://n/mods/1") })
             << qMakePair(QStringLiteral("morrowind__other"),
                          QList<ModEntry>{ sharing_mod("Z", "/mods/other/Z") });

    check("referenced path found",
          mod_sharing::pathReferencedIn(mod_sharing::cleanModPath("/mods/main/OAAB"), profiles));
    check("normalised variant found",
          mod_sharing::pathReferencedIn(mod_sharing::cleanModPath("/mods/main/x/../OAAB/"), profiles));
    check("absent path not found",
          !mod_sharing::pathReferencedIn(mod_sharing::cleanModPath("/mods/main/Nope"), profiles));
    check("empty path not found",
          !mod_sharing::pathReferencedIn(QString(), profiles));
}

static void testRoundTripPreservesSharedRow()
{
    std::cout << "\n[shared row survives serialize -> parse]\n";
    const ModEntry shared = mod_sharing::makeSharedRow(
        [] { ModEntry s = sharing_mod("OAAB Data", "/mods/main/OAAB_Data",
                              "https://www.nexusmods.com/morrowind/mods/49042");
             s.checked = true; s.fomodChoices = "0:0:0;"; s.bainChoices = "00 Core;01 Patch";
             s.annotation = "n"; return s; }(),
        /*copyConfig=*/true);

    QList<ModEntry> list;
    list << sharing_separator("Shared in") << shared;
    const QString text = modlist_serializer::serializeModlist(list);
    const QList<ModEntry> back = modlist_serializer::parseModlist(text);

    int idx = mod_sharing::findExistingRow(back, shared);
    check("shared row found after round-trip", idx >= 0);
    if (idx >= 0) {
        const ModEntry &e = back[idx];
        check("path round-trips",     e.modPath == shared.modPath);
        check("enabled round-trips",  e.checked == shared.checked);
        check("fomod round-trips",    e.fomodChoices == shared.fomodChoices);
        check("bain round-trips",     e.bainChoices == shared.bainChoices);
        check("annotation round-trips", e.annotation == shared.annotation);
    }
}

static void run_mod_sharing()
{
    std::cout << "=== mod_sharing ===\n";
    testMakeSharedRowCopyConfig();
    testMakeSharedRowDefault();
    testAppendAndDedup();
    testPathReferencedIn();
    testRoundTripPreservesSharedRow();
}

// === install_layout ===

static void expectDive(const char *name,
                       const QStringList &subs,
                       const QStringList &files,
                       const QString &expected)
{
    const QString got = install_layout::diveTarget(subs, files);
    check(name, got == expected, got);
}

static const char *variantName(install_layout::OaabVariant v)
{
    switch (v) {
    case install_layout::OaabVariant::None:      return "None";
    case install_layout::OaabVariant::NeedsOaab: return "NeedsOaab";
    case install_layout::OaabVariant::NoOaab:    return "NoOaab";
    }
    return "?";
}

static void expectVariant(const char *name, const QString &fileName,
                          install_layout::OaabVariant expected)
{
    const auto got = install_layout::classifyOaabVariant(fileName);
    check(name, got == expected, QString::fromLatin1(variantName(got)));
}

static void run_install_layout()
{
    std::cout << "install_layout::diveTarget\n";

    // Standard <ModName>/<contents> wrapper: dive.
    expectDive("dives into normal mod-name wrapper",
               {"My Cool Mod"}, {},
               "My Cool Mod");

    // Single subdir that's itself an asset folder: don't dive. Diving meshes/
    // gives data="<...>/meshes", then OpenMW looks for meshes/foo.nif inside
    // meshes/ and never finds it.
    expectDive("does not dive into bare meshes/",   {"meshes"},   {}, QString());
    expectDive("does not dive into bare textures/", {"textures"}, {}, QString());
    expectDive("does not dive into bare sound/",    {"sound"},    {}, QString());
    expectDive("does not dive into bare music/",    {"music"},    {}, QString());
    expectDive("does not dive into bare fonts/",    {"fonts"},    {}, QString());
    expectDive("does not dive into bare bookart/",  {"bookart"},  {}, QString());
    expectDive("does not dive into bare splash/",   {"splash"},   {}, QString());
    expectDive("does not dive into bare icons/",    {"icons"},    {}, QString());
    expectDive("does not dive into bare scripts/",  {"scripts"},  {}, QString());

    // Windows-cased archives behave like lowercase ones.
    expectDive("asset-name check is case-insensitive (Meshes)",
               {"Meshes"}, {}, QString());
    expectDive("asset-name check is case-insensitive (TEXTURES)",
               {"TEXTURES"}, {}, QString());

    // FOMOD installer at root: don't dive. Diving pushes ModuleConfig.xml to
    // <modPath>/fomod/fomod/... so hasFomod() silently skips the wizard.
    expectDive("does not dive into bare fomod/",
               {"fomod"}, {}, QString());

    // Sibling files next to a subdir: don't dive. Dubdilla Location Fix had a
    // docs folder plus an .esp at root; diving orphaned the .esp.
    expectDive("does not dive when an .esp sits at the root",
               {"Documentation"}, {"Dubdilla Location Fix.esp"},
               QString());
    expectDive("does not dive when a .bsa sits at the root",
               {"Extras"}, {"MyMod.bsa"},
               QString());
    expectDive("does not dive when a readme sits at the root",
               {"Data"}, {"README.txt"},
               QString());

    // Multiple subdirs: no single root to pick.
    expectDive("does not dive when two subdirs are present",
               {"meshes", "textures"}, {},
               QString());
    expectDive("does not dive when many subdirs are present",
               {"00 Core", "01 Optional", "fomod"}, {},
               QString());

    // Empty / pathological inputs.
    expectDive("empty input → empty result", {}, {}, QString());
    expectDive("only files, no subdirs → empty result",
               {}, {"loose.esp"}, QString());

    // Only an exact bare-asset-name match blocks the dive; "MyMeshes" /
    // "ScriptPack" still dive.
    expectDive("dives when subdir merely contains 'meshes' as substring",
               {"MyMeshes"}, {},
               "MyMeshes");
    expectDive("dives when subdir merely contains 'scripts' as substring",
               {"ScriptPack"}, {},
               "ScriptPack");

    // Sixth House Minor Bases Refit ships "- OAAB" and "- No OAAB" mains; the
    // picker recommends whichever matches the modlist, so OAAB/No-OAAB
    // punctuation variants must each classify right.
    std::cout << "\ninstall_layout::classifyOaabVariant\n";
    using V = install_layout::OaabVariant;

    expectVariant("'- OAAB' build is NeedsOaab",
                  "Sixth House Minor Bases Refit - OAAB", V::NeedsOaab);
    expectVariant("'- No OAAB' build is NoOaab",
                  "Sixth House Minor Bases Refit - No OAAB", V::NoOaab);

    // Negation wins over the bare OAAB match.
    expectVariant("'No-OAAB' (hyphen)",      "Foo No-OAAB",      V::NoOaab);
    expectVariant("'No_OAAB' (underscore)",  "Foo No_OAAB",      V::NoOaab);
    expectVariant("'NoOAAB' (no separator)", "Foo NoOAAB",       V::NoOaab);
    expectVariant("'Non-OAAB'",              "Foo Non-OAAB",     V::NoOaab);
    expectVariant("'without OAAB'",          "Foo without OAAB", V::NoOaab);
    expectVariant("case-insensitive 'no oaab'", "foo no oaab",   V::NoOaab);

    // Plain OAAB builds, various punctuation.
    expectVariant("'OAAB' standalone token", "MyMod OAAB.7z",    V::NeedsOaab);
    expectVariant("'(OAAB)' parenthesised",  "MyMod (OAAB)",     V::NeedsOaab);
    expectVariant("lowercase 'oaab'",        "mymod-oaab.zip",   V::NeedsOaab);

    // Not a variant at all.
    expectVariant("no OAAB mention → None",  "Some Other Mod v2", V::None);
    // OAAB matches as a whole word, not a substring.
    expectVariant("'Oaaberration' substring → None", "Oaaberration", V::None);

    std::cout << "\n";
}

// === post_install ===

static void post_install_touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.close();
}

static void testLooksLikeGroundcover()
{
    std::cout << "\n[looksLikeGroundcover]\n";
    using post_install::looksLikeGroundcover;

    check("matches 'grass' in path",
          looksLikeGroundcover("/mods/Remiros Grass Vol1", "Remiros"));
    check("matches 'groundcover' in display name",
          looksLikeGroundcover("/mods/whatever", "Aesthesia Groundcover"));
    check("case-insensitive",
          looksLikeGroundcover("/mods/GRASS", "X"));
    check("named-hint mod (Lush Synthesis) matches with neither word",
          looksLikeGroundcover("/mods/lush_synthesis_v2", "Lush Synthesis"));
    check("unrelated mod does not match",
          !looksLikeGroundcover("/mods/Patch for Purists", "Patch for Purists"));
}

static void testFindSplashDir()
{
    std::cout << "\n[findSplashDir]\n";
    QTemporaryDir dir;

    // Mod root is itself a Splash/ with images.
    const QString rootIsSplash = dir.filePath("ModA/Splash");
    post_install_touch(rootIsSplash + "/load1.tga");
    check("mod root that is Splash/ with images is found",
          QFileInfo(post_install::findSplashDir(rootIsSplash)).fileName()
              .compare("splash", Qt::CaseInsensitive) == 0);

    // Splash/ nested a couple levels down.
    const QString nested = dir.filePath("ModB");
    post_install_touch(nested + "/Data Files/Splash/intro.png");
    check("nested Splash/ with images is found",
          !post_install::findSplashDir(nested).isEmpty());

    // Splash/ with no images is not a splash replacer.
    const QString empty = dir.filePath("ModC");
    QDir().mkpath(empty + "/Splash");
    check("Splash/ with no images is not matched",
          post_install::findSplashDir(empty).isEmpty());

    // No splash anywhere.
    const QString none = dir.filePath("ModD");
    post_install_touch(none + "/meshes/x.nif");
    check("mod without any Splash/ returns empty",
          post_install::findSplashDir(none).isEmpty());
}

static void testNormalizeModName()
{
    std::cout << "\n[normalizeModName]\n";
    check("lowercases + drops non-alnum",
          post_install::normalizeModName("Remiros' Groundcover!") == "remirosgroundcover");
    check("keeps digits",
          post_install::normalizeModName("OAAB 2 Data") == "oaab2data");
}

static void testBundledPatchMatchesMod()
{
    std::cout << "\n[bundledPatchMatchesMod]\n";
    const QString target = post_install::normalizeModName("Remiros' Groundcover");

    check("'01 Grass for Remiros Groundcover' matches",
          post_install::bundledPatchMatchesMod("01 Grass for Remiros Groundcover", target));
    check("numbered+lettered prefix '10a Patch for Remiros Groundcover' matches",
          post_install::bundledPatchMatchesMod("10a Patch for Remiros Groundcover", target));
    check("non-numbered subfolder does not match",
          !post_install::bundledPatchMatchesMod("Grass for Remiros Groundcover", target));
    check("subfolder without 'for <target>' does not match",
          !post_install::bundledPatchMatchesMod("01 Some Textures", target));
    check("different target does not match",
          !post_install::bundledPatchMatchesMod("01 Grass for Vurt's Trees", target));
    check("short normalized name never matches",
          !post_install::bundledPatchMatchesMod("01 X for ab", post_install::normalizeModName("ab")));
}

static void run_post_install()
{
    std::cout << "=== post_install ===\n";
    testLooksLikeGroundcover();
    testFindSplashDir();
    testNormalizeModName();
    testBundledPatchMatchesMod();
}

// -- modlist_summary ----------------------------------------------------------

static ModEntry mkSummaryMod(const QString &name, int status, bool checked,
                             qint64 size, const QString &path = {})
{
    ModEntry e;
    e.itemType      = QStringLiteral("mod");
    e.displayName   = name;
    e.installStatus = status;
    e.checked       = checked;
    e.modSize       = size;
    e.modPath       = path;
    return e;
}

static ModEntry mkSummarySep(const QString &name)
{
    ModEntry e;
    e.itemType    = QStringLiteral("separator");
    e.displayName = name;
    return e;
}

static void testFormatBytes()
{
    using modlist_summary::formatBytes;
    check("0 bytes renders as 0 B",  formatBytes(0) == "0 B",       formatBytes(0));
    check("negative renders as 0 B", formatBytes(-5) == "0 B",      formatBytes(-5));
    check("sub-KB keeps bytes",      formatBytes(512) == "512 B",   formatBytes(512));
    check("1023 B does not round up",formatBytes(1023) == "1023 B", formatBytes(1023));
    check("exactly 1 KB",            formatBytes(1024) == "1 KB",   formatBytes(1024));
    check("1.5 MB",  formatBytes(1024LL * 1024 * 3 / 2) == "1.5 MB",
          formatBytes(1024LL * 1024 * 3 / 2));
    check("2.00 GB", formatBytes(2LL * 1024 * 1024 * 1024) == "2.00 GB",
          formatBytes(2LL * 1024 * 1024 * 1024));
}

static void testComputeStats()
{
    const QList<ModEntry> rows{
        mkSummarySep("GUI"),
        mkSummaryMod("a", 1, true,  1000),
        mkSummaryMod("b", 1, false, 2000),
        mkSummarySep("World"),
        mkSummaryMod("c", 0, true,  9999),   // not installed -> ignored
        mkSummaryMod("d", 2, true,  9999),   // mid-install   -> ignored
    };
    const auto s = modlist_summary::computeStats(rows);
    check("separators counted",           s.sepCount == 2,      QString::number(s.sepCount));
    check("only installed mods counted",  s.modCount == 2,      QString::number(s.modCount));
    check("enabled mods counted",         s.enabledCount == 1,  QString::number(s.enabledCount));
    check("total bytes summed",           s.totalBytes == 3000, QString::number(s.totalBytes));
    check("enabled bytes exclude unticked",
          s.enabledBytes == 1000, QString::number(s.enabledBytes));
}

static void testComputeStatsSizeFallback()
{
    const QList<ModEntry> rows{
        mkSummaryMod("known",   1, true, 4096, "/x/known"),
        mkSummaryMod("unknown", 1, true, 0,    "/x/unknown"),
    };

    // No resolver: an unmeasured mod must still count, it just adds no bytes -
    // otherwise the counts under-report while the async size scan is in flight.
    const auto bare = modlist_summary::computeStats(rows);
    check("unmeasured mod still counts toward modCount",
          bare.modCount == 2, QString::number(bare.modCount));
    check("unmeasured mod still counts as enabled",
          bare.enabledCount == 2, QString::number(bare.enabledCount));
    check("unmeasured mod contributes no bytes",
          bare.totalBytes == 4096, QString::number(bare.totalBytes));

    // With a resolver, only the unmeasured row is looked up.
    QStringList asked;
    const auto filled = modlist_summary::computeStats(
        rows, [&](const QString &p) { asked << p; return 1024; });
    check("resolver consulted only for unknown sizes",
          asked == QStringList{"/x/unknown"}, asked.join(","));
    check("resolved size is summed",
          filled.totalBytes == 4096 + 1024, QString::number(filled.totalBytes));
}

static void testCountOutsideModsDir()
{
    QTemporaryDir tmp;
    const QString root = tmp.filePath("mods");
    QDir().mkpath(root + "/inside");
    QDir().mkpath(tmp.filePath("elsewhere/outside"));
    QDir().mkpath(tmp.filePath("mods_old/sibling"));   // prefix trap

    const QList<ModEntry> rows{
        mkSummaryMod("inside",  1, true, 0, root + "/inside"),
        mkSummaryMod("outside", 1, true, 0, tmp.filePath("elsewhere/outside")),
        mkSummaryMod("sibling", 1, true, 0, tmp.filePath("mods_old/sibling")),
        mkSummaryMod("gone",    1, true, 0, tmp.filePath("does/not/exist")),
        mkSummaryMod("uninst",  0, true, 0, tmp.filePath("elsewhere/outside")),
        mkSummarySep("sep"),
    };

    // outside + sibling. "mods_old" must not read as a child of "mods".
    check("counts installed mods outside modsDir (incl. the _old prefix trap)",
          modlist_summary::countOutsideModsDir(rows, root) == 2,
          QString::number(modlist_summary::countOutsideModsDir(rows, root)));
    check("missing folders are not counted",
          modlist_summary::countOutsideModsDir({rows[3]}, root) == 0);
    check("empty modsDir yields 0",
          modlist_summary::countOutsideModsDir(rows, QString()) == 0);
}

static void run_modlist_summary()
{
    std::cout << "=== modlist_summary ===\n";
    testFormatBytes();
    testComputeStats();
    testComputeStatsSizeFallback();
    testCountOutsideModsDir();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    run_mod_naming();
    run_mod_sharing();
    run_install_layout();
    run_post_install();
    run_modlist_summary();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
