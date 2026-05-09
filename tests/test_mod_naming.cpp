// tests/test_mod_naming.cpp
//
// Pins mod_naming - the folder-name heuristics carved out of
// MainWindow::addModFromPath in 0.4.  Three concerns:
//   1. findStaleSiblings: which "<base>(_<ts>)?" folders should be
//      removed when a freshly-installed dir lands next to them.
//   2. folderNameLooksGeneric: which folder names (curated list +
//      Nexus-archive shapes + generic prefixes) trigger a Nexus-title
//      rename.
//   3. stripTrailingVersionChain: peel the "<id>-v-v-v-ts" tail off a
//      slug-shaped folder name.
//
// All Qt-Core-only.  No FS access.

#include "mod_naming.h"

#include <iostream>

namespace {

int s_passed = 0;
int s_failed = 0;

void check(const char *name, bool ok, const QString &hint = {})
{
    if (ok) {
        std::cout << "  \033[32m✓\033[0m " << name << "\n";
        ++s_passed;
    } else {
        std::cout << "  \033[31m✗\033[0m " << name;
        if (!hint.isEmpty()) std::cout << " (" << hint.toStdString() << ")";
        std::cout << "\n";
        ++s_failed;
    }
}

// -- findStaleSiblings --------------------------------------------

void testFindStaleSiblings_basicNexusReinstall()
{
    std::cout << "\n[findStaleSiblings: nexus reinstall picks up old timestamp folder]\n";
    // Freshly-installed: "Foo-12345_1776202250"
    // Sibling bucket includes the prior install "Foo-12345" and a
    // pre-existing unrelated mod.
    QStringList siblings = {
        "Foo-12345",              // older same-mod install
        "Foo-12345_1776202250",   // freshly-installed (don't return)
        "UnrelatedMod-67890",     // different mod
    };
    auto stale = mod_naming::findStaleSiblings("Foo-12345_1776202250", siblings);
    check("returns the older same-mod install", stale.contains("Foo-12345"));
    check("does NOT return self",           !stale.contains("Foo-12345_1776202250"));
    check("does NOT return unrelated mod",  !stale.contains("UnrelatedMod-67890"));
}

void testFindStaleSiblings_userNamedFolderProtected()
{
    std::cout << "\n[findStaleSiblings: user-named folder ending in _<digits> NOT cleaned]\n";
    // "Save_1234" looks like it could be "Save" with a timestamp, but
    // the stripped prefix "Save" doesn't match the Nexus shape
    // "name-<id>" - so the cleanup must NOT fire.
    QStringList siblings = { "Save", "Save_1234", "Save_5678" };
    auto stale = mod_naming::findStaleSiblings("Save_5678", siblings);
    check("user-named folder produces no stale entries",
          stale.isEmpty(),
          "would have been " + QString::number(stale.size()));
}

void testFindStaleSiblings_strippedSuffixHonored()
{
    std::cout << "\n[findStaleSiblings: prefix without trailing _<ts> still matches]\n";
    // currentFolder has no _<ts> suffix; older ones DO.
    QStringList siblings = { "Foo-12345_1234567", "Foo-12345_2345678", "Foo-12345" };
    auto stale = mod_naming::findStaleSiblings("Foo-12345", siblings);
    check("matches older _<ts>-suffixed siblings",
          stale.contains("Foo-12345_1234567")
            && stale.contains("Foo-12345_2345678"));
}

void testFindStaleSiblings_emptyInputs()
{
    std::cout << "\n[findStaleSiblings: empty inputs → empty output]\n";
    check("empty current name → empty",
          mod_naming::findStaleSiblings(QString(), {"a", "b"}).isEmpty());
    check("empty siblings → empty",
          mod_naming::findStaleSiblings("Foo-12345", {}).isEmpty());
}

// -- folderNameLooksGeneric ---------------------------------------

void testGeneric_exactMatchList()
{
    std::cout << "\n[folderNameLooksGeneric: curated names match (case-insensitive)]\n";
    check("'scripts' generic",          mod_naming::folderNameLooksGeneric("scripts"));
    check("'Scripts' generic",          mod_naming::folderNameLooksGeneric("Scripts"));
    check("'Data Files' generic",       mod_naming::folderNameLooksGeneric("Data Files"));
    check("'00 Core' generic",          mod_naming::folderNameLooksGeneric("00 Core"));
    check("'Complete pack' generic",    mod_naming::folderNameLooksGeneric("Complete pack"));
    check("'mygui' generic",            mod_naming::folderNameLooksGeneric("mygui"));
}

void testGeneric_genericPrefixes()
{
    std::cout << "\n[folderNameLooksGeneric: 'sound*'/'audio*'/'mesh*'/'fix*' all match]\n";
    check("'sound' generic",       mod_naming::folderNameLooksGeneric("sound"));
    check("'soundFX' generic",     mod_naming::folderNameLooksGeneric("soundFX"));
    check("'meshes_replacer' generic",
          mod_naming::folderNameLooksGeneric("meshes_replacer"));
    check("'fixes_pack' generic",  mod_naming::folderNameLooksGeneric("fixes_pack"));
    check("'audio01' generic",     mod_naming::folderNameLooksGeneric("audio01"));
}

void testGeneric_nexusArchiveShape()
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

void testGeneric_versionedArchive()
{
    std::cout << "\n[folderNameLooksGeneric: 4+ trailing version segments match]\n";
    // OAAB_Data-49042-2-5-1-1764958680 - five trailing numeric
    // segments after the first hyphen.
    check("'OAAB_Data-49042-2-5-1-1764958680' generic",
          mod_naming::folderNameLooksGeneric("OAAB_Data-49042-2-5-1-1764958680"));
    // Three segments isn't enough (would catch user-named mods).
    check("'Foo-1-2-3' NOT generic (only 3 segs)",
          !mod_naming::folderNameLooksGeneric("Foo-1-2-3"));
}

void testGeneric_realModsNotMisclassified()
{
    std::cout << "\n[folderNameLooksGeneric: user-named mods are NOT generic]\n";
    check("'Tamriel Rebuilt' NOT generic",
          !mod_naming::folderNameLooksGeneric("Tamriel Rebuilt"));
    check("'OAAB_Data' NOT generic",
          !mod_naming::folderNameLooksGeneric("OAAB_Data"));
    check("'Authentic Signs IT' NOT generic",
          !mod_naming::folderNameLooksGeneric("Authentic Signs IT"));
    // Edge-case: name ending with a number doesn't match the Nexus
    // shape (which needs <id> after a hyphen).
    check("'Mod 2' NOT generic",
          !mod_naming::folderNameLooksGeneric("Mod 2"));
}

// -- stripTrailingVersionChain ------------------------------------

void testStripVersionChain_typical()
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

void testStripVersionChain_insufficientChain()
{
    std::cout << "\n[stripTrailingVersionChain: short chains return empty]\n";
    check("single -<id> doesn't qualify",
          mod_naming::stripTrailingVersionChain("Foo Mod-12345").isEmpty());
    check("two trailing segs doesn't qualify",
          mod_naming::stripTrailingVersionChain("Foo Mod-12345-1").isEmpty());
    check("plain folder name returns empty",
          mod_naming::stripTrailingVersionChain("Tamriel Rebuilt").isEmpty());
}

// -- hardcodedRename ----------------------------------------------

void testHardcodedRename()
{
    std::cout << "\n[hardcodedRename: known folder names get the override]\n";
    check("'restock' renamed",
          mod_naming::hardcodedRename("restock") == "(OpenMW 0.49) Restocking");
    check("'Restock' (case-insensitive)",
          mod_naming::hardcodedRename("Restock") == "(OpenMW 0.49) Restocking");
    check("unknown name returns empty",
          mod_naming::hardcodedRename("Tamriel Rebuilt").isEmpty());
}

} // namespace

int main(int /*argc*/, char ** /*argv*/)
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
    testGeneric_realModsNotMisclassified();

    testStripVersionChain_typical();
    testStripVersionChain_insufficientChain();

    testHardcodedRename();

    std::cout << "\n" << s_passed << " passed, " << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
