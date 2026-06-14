// tests/test_mod_sharing.cpp
//
// Locks in the pure logic behind "Share a mod between profiles": building the
// shared row, deduping when the target already has it, and the cross-profile
// reference scan that protects a shared folder from being deleted while another
// profile still points at it.  QtCore-only (ModEntry + the serializer).

#include "mod_sharing.h"
#include "modentry.h"
#include "modlist_serializer.h"

#include <QCoreApplication>
#include <QList>
#include <QPair>
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
        if (!hint.isEmpty()) std::cout << " (" << hint.toStdString() << ")";
        std::cout << "\n";
        ++s_failed;
    }
}

static ModEntry mod(const QString &name, const QString &path,
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

static ModEntry separator(const QString &name)
{
    ModEntry e;
    e.itemType    = QStringLiteral("separator");
    e.displayName = name;
    return e;
}

// -- makeSharedRow ---

static void testMakeSharedRowCopyConfig()
{
    std::cout << "\n[makeSharedRow copyConfig=true keeps config, resets identity]\n";
    ModEntry src = mod("OAAB Data", "/mods/main/OAAB_Data",
                       "https://www.nexusmods.com/morrowind/mods/49042");
    src.checked      = true;
    src.fomodChoices = "0:1:2;";
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
    ModEntry src = mod("Patch", "/mods/main/Patch",
                       "https://www.nexusmods.com/morrowind/mods/7");
    src.checked      = true;
    src.fomodChoices = "1:1:1;";
    src.annotation   = "note";
    src.dependsOn    = QStringList{"https://y/mods/2"};

    const ModEntry e = mod_sharing::makeSharedRow(src, /*copyConfig=*/false);
    check("disabled",            !e.checked);
    check("no fomod choices",    e.fomodChoices.isEmpty());
    check("no annotation",       e.annotation.isEmpty());
    check("no deps",             e.dependsOn.isEmpty());
    check("but customName kept", e.customName == "Patch");
    check("and nexusUrl kept",   e.nexusUrl == src.nexusUrl);
    check("and modPath kept",    e.modPath == src.modPath);
}

// -- findExistingRow / appendSharedRow ---

static void testAppendAndDedup()
{
    std::cout << "\n[appendSharedRow appends once, dedups by path then url]\n";
    QList<ModEntry> target;
    target << separator("Section")
           << mod("Existing", "/mods/star/Existing", "https://n/mods/100");

    const ModEntry shareA = mod_sharing::makeSharedRow(
        mod("OAAB", "/mods/main/OAAB", "https://n/mods/49042"), true);

    auto r1 = mod_sharing::appendSharedRow(target, shareA);
    check("fresh mod appended", r1.added && r1.entries.size() == 3);

    // Same path again -> dedup.
    auto r2 = mod_sharing::appendSharedRow(r1.entries, shareA);
    check("same path deduped (no-op)", !r2.added && r2.entries.size() == 3);

    // Trailing-slash / dot path variant of the same folder -> still dedup.
    ModEntry slashVariant = mod_sharing::makeSharedRow(
        mod("OAAB", "/mods/main/sub/../OAAB/", "https://n/mods/49042"), true);
    auto r3 = mod_sharing::appendSharedRow(r1.entries, slashVariant);
    check("path-normalised variant deduped", !r3.added,
          mod_sharing::cleanModPath(slashVariant.modPath));

    // Same nexusUrl at a DIFFERENT path (a private fork already in target) -> dedup.
    ModEntry sameUrlOtherPath = mod_sharing::makeSharedRow(
        mod("OAAB copy", "/mods/star/OAAB_local", "https://n/mods/49042"), true);
    QList<ModEntry> withFork = r1.entries;  // already contains the /mods/main/OAAB path + url
    auto r4 = mod_sharing::appendSharedRow(withFork, sameUrlOtherPath);
    check("same nexusUrl other path deduped", !r4.added);

    // A genuinely different mod -> appended.
    ModEntry other = mod_sharing::makeSharedRow(
        mod("Other", "/mods/main/Other", "https://n/mods/200"), true);
    auto r5 = mod_sharing::appendSharedRow(r1.entries, other);
    check("different mod appended", r5.added && r5.entries.size() == 4);
}

// -- pathReferencedIn ---

static void testPathReferencedIn()
{
    std::cout << "\n[pathReferencedIn scans other profiles' parsed modlists]\n";
    QList<QPair<QString, QList<ModEntry>>> profiles;
    profiles << qMakePair(QStringLiteral("morrowind__main"),
                          QList<ModEntry>{ separator("S"),
                                           mod("OAAB", "/mods/main/OAAB", "https://n/mods/1") })
             << qMakePair(QStringLiteral("morrowind__other"),
                          QList<ModEntry>{ mod("Z", "/mods/other/Z") });

    check("referenced path found",
          mod_sharing::pathReferencedIn(mod_sharing::cleanModPath("/mods/main/OAAB"), profiles));
    check("normalised variant found",
          mod_sharing::pathReferencedIn(mod_sharing::cleanModPath("/mods/main/x/../OAAB/"), profiles));
    check("absent path not found",
          !mod_sharing::pathReferencedIn(mod_sharing::cleanModPath("/mods/main/Nope"), profiles));
    check("empty path not found",
          !mod_sharing::pathReferencedIn(QString(), profiles));
}

// -- persistence round-trip ---

static void testRoundTripPreservesSharedRow()
{
    std::cout << "\n[shared row survives serialize -> parse]\n";
    const ModEntry shared = mod_sharing::makeSharedRow(
        [] { ModEntry s = mod("OAAB Data", "/mods/main/OAAB_Data",
                              "https://www.nexusmods.com/morrowind/mods/49042");
             s.checked = true; s.fomodChoices = "0:0:0;"; s.annotation = "n"; return s; }(),
        /*copyConfig=*/true);

    QList<ModEntry> list;
    list << separator("Shared in") << shared;
    const QString text = modlist_serializer::serializeModlist(list);
    const QList<ModEntry> back = modlist_serializer::parseModlist(text);

    int idx = mod_sharing::findExistingRow(back, shared);
    check("shared row found after round-trip", idx >= 0);
    if (idx >= 0) {
        const ModEntry &e = back[idx];
        check("path round-trips",     e.modPath == shared.modPath);
        check("enabled round-trips",  e.checked == shared.checked);
        check("fomod round-trips",    e.fomodChoices == shared.fomodChoices);
        check("annotation round-trips", e.annotation == shared.annotation);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    std::cout << "=== mod_sharing ===\n";
    testMakeSharedRowCopyConfig();
    testMakeSharedRowDefault();
    testAppendAndDedup();
    testPathReferencedIn();
    testRoundTripPreservesSharedRow();

    std::cout << "\n"
              << s_passed << " passed, "
              << s_failed << " failed\n";
    return s_failed == 0 ? 0 : 1;
}
